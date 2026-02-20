// SPDX-License-Identifier: MIT
// mmr_memtap_loopback.c
//
// Loopback /dev/mmr_memtap implementation for early development.
// Provides the ioctl ABI defined in kernel/mmr_memtap.h and implements
// read()/llseek() over snapshot files on disk (nes_path/snes_path/gen_path).
//
// This enables daemon/mmr-daemon "device mode" testing without FPGA patches.
// Later: replace file-backed reads with FPGA bridge reads, keep ABI unchanged.

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/errno.h>

#include "../mmr_memtap.h"  // IMPORTANT: shared ABI header

// Module parameters: backing snapshot file paths
static char *nes_path  = NULL;
static char *snes_path = NULL;
static char *gen_path  = NULL;

module_param(nes_path, charp, 0644);
MODULE_PARM_DESC(nes_path,  "Path to NES CPU RAM snapshot file (2048 bytes)");
module_param(snes_path, charp, 0644);
MODULE_PARM_DESC(snes_path, "Path to SNES WRAM snapshot file (131072 bytes)");
module_param(gen_path, charp, 0644);
MODULE_PARM_DESC(gen_path,  "Path to Genesis 68K RAM snapshot file (65536 bytes)");

struct mmr_loopback_dev {
	struct mutex lock;

	u32 core_id;
	u32 map_version;

	struct mmr_region_desc regions[MMR_MAX_REGIONS];
	u32 region_count;

	u64 frame_counter;
	wait_queue_head_t wq;
};

static struct mmr_loopback_dev gdev;

struct mmr_file_state {
	u32 selected_region;
	u32 offset;
};

static const char *path_for_region(u32 region_id)
{
	switch (region_id) {
	case MMR_REGION_NES_CPU_RAM: return nes_path;
	case MMR_REGION_SNES_WRAM:   return snes_path;
	case MMR_REGION_GEN_68K_RAM: return gen_path;
	default: return NULL;
	}
}

static u32 size_for_region(u32 region_id)
{
	u32 i;
	for (i = 0; i < gdev.region_count; i++) {
		if (gdev.regions[i].region_id == region_id)
			return gdev.regions[i].size_bytes;
	}
	return 0;
}

static bool region_is_valid(u32 region_id)
{
	return size_for_region(region_id) != 0;
}

static ssize_t file_read_exact(const char *path, loff_t offset, void *buf, size_t len)
{
	struct file *filp;
	loff_t pos = offset;
	ssize_t r;

	if (!path)
		return -ENOENT;

	filp = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(filp))
		return PTR_ERR(filp);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
	r = kernel_read(filp, buf, len, &pos);
#else
	r = vfs_read(filp, buf, len, &pos);
#endif

	filp_close(filp, NULL);

	if (r < 0)
		return r;
	if ((size_t)r != len)
		return -EIO;

	return r;
}

/* ---------------- file ops ---------------- */

static int mmr_open(struct inode *inode, struct file *f)
{
	struct mmr_file_state *st;

	st = kzalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	/* default region: first exposed region */
	mutex_lock(&gdev.lock);
	st->selected_region = (gdev.region_count ? gdev.regions[0].region_id : MMR_REGION_NONE);
	st->offset = 0;
	mutex_unlock(&gdev.lock);

	f->private_data = st;
	return 0;
}

static int mmr_release(struct inode *inode, struct file *f)
{
	struct mmr_file_state *st = f->private_data;
	kfree(st);
	f->private_data = NULL;
	return 0;
}

static loff_t mmr_llseek(struct file *f, loff_t off, int whence)
{
	struct mmr_file_state *st = f->private_data;
	u32 size;
	loff_t newpos;

	if (!st)
		return -EINVAL;

	mutex_lock(&gdev.lock);
	size = size_for_region(st->selected_region);
	mutex_unlock(&gdev.lock);

	if (!size)
		return -EINVAL;

	switch (whence) {
	case SEEK_SET: newpos = off; break;
	case SEEK_CUR: newpos = (loff_t)st->offset + off; break;
	case SEEK_END: newpos = (loff_t)size + off; break;
	default: return -EINVAL;
	}

	if (newpos < 0)
		return -EINVAL;
	if (newpos > (loff_t)size)
		return -EINVAL;

	st->offset = (u32)newpos;
	return newpos;
}

static ssize_t mmr_read(struct file *f, char __user *ubuf, size_t len, loff_t *ppos)
{
	struct mmr_file_state *st = f->private_data;
	const char *path;
	u32 size;
	ssize_t r;
	void *kbuf;

	if (!st || !ubuf)
		return -EINVAL;

	mutex_lock(&gdev.lock);
	size = size_for_region(st->selected_region);
	path = path_for_region(st->selected_region);
	mutex_unlock(&gdev.lock);

	if (!size || !path)
		return -EINVAL;

	/* clamp len to remaining bytes */
	if (st->offset > size)
		return -EINVAL;

	if (len > (size_t)(size - st->offset))
		len = (size_t)(size - st->offset);

	/* standard read semantics: allow short read at EOF */
	if (len == 0)
		return 0;

	kbuf = kmalloc(len, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	r = file_read_exact(path, (loff_t)st->offset, kbuf, len);
	if (r < 0) {
		kfree(kbuf);
		return r;
	}

	if (copy_to_user(ubuf, kbuf, len)) {
		kfree(kbuf);
		return -EFAULT;
	}

	kfree(kbuf);

	/* advance */
	st->offset += (u32)len;

	/* mimic frame progress + wake waiters */
	mutex_lock(&gdev.lock);
	gdev.frame_counter++;
	wake_up_interruptible(&gdev.wq);
	mutex_unlock(&gdev.lock);

	return (ssize_t)len;
}

/* ---------------- ioctl ---------------- */

static long mmr_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct mmr_file_state *st = f->private_data;
	long ret = 0;

	switch (cmd) {
	case MMR_IOCTL_GET_INFO: {
		struct mmr_info info;
		memset(&info, 0, sizeof(info));

		mutex_lock(&gdev.lock);
		info.abi_version   = MMR_ABI_VERSION;
		info.core_id       = gdev.core_id;
		info.map_version   = gdev.map_version;
		info.region_count  = gdev.region_count;
		info.frame_counter = gdev.frame_counter;
		mutex_unlock(&gdev.lock);

		if (copy_to_user((void __user *)arg, &info, sizeof(info)))
			ret = -EFAULT;
		break;
	}

	case MMR_IOCTL_GET_REGIONS: {
		struct mmr_region_desc out[MMR_MAX_REGIONS];
		memset(out, 0, sizeof(out));

		mutex_lock(&gdev.lock);
		memcpy(out, gdev.regions, sizeof(gdev.regions));
		mutex_unlock(&gdev.lock);

		if (copy_to_user((void __user *)arg, out, sizeof(out)))
			ret = -EFAULT;
		break;
	}

	case MMR_IOCTL_SELECT_REGION: {
		u32 region_id;

		if (!st)
			return -EINVAL;

		if (copy_from_user(&region_id, (void __user *)arg, sizeof(region_id)))
			return -EFAULT;

		mutex_lock(&gdev.lock);
		if (!region_is_valid(region_id)) {
			mutex_unlock(&gdev.lock);
			return -EINVAL;
		}
		mutex_unlock(&gdev.lock);

		st->selected_region = region_id;
		st->offset = 0;
		break;
	}

	case MMR_IOCTL_SEEK: {
		struct mmr_seek_req req;
		u32 size;

		if (!st)
			return -EINVAL;

		if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
			return -EFAULT;

		mutex_lock(&gdev.lock);
		size = size_for_region(st->selected_region);
		mutex_unlock(&gdev.lock);

		if (!size)
			return -EINVAL;
		if (req.offset > size)
			return -EINVAL;

		st->offset = req.offset;
		break;
	}

	case MMR_IOCTL_WAIT_FRAME: {
		u64 last;

		if (copy_from_user(&last, (void __user *)arg, sizeof(last)))
			return -EFAULT;

		/* wait until frame_counter > last */
		ret = wait_event_interruptible(gdev.wq, READ_ONCE(gdev.frame_counter) > last);
		if (ret)
			return ret;
		break;
	}

	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

static const struct file_operations mmr_fops = {
	.owner          = THIS_MODULE,
	.open           = mmr_open,
	.release        = mmr_release,
	.read           = mmr_read,
	.llseek         = mmr_llseek,
	.unlocked_ioctl = mmr_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = mmr_ioctl,
#endif
};

static struct miscdevice mmr_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "mmr_memtap",
	.fops  = &mmr_fops,
	/* mode is not always honored depending on kernel config, but it's a good default */
	.mode  = 0600,
};

static int __init mmr_init(void)
{
	int r;

	mutex_init(&gdev.lock);
	init_waitqueue_head(&gdev.wq);

	/* Default to NES core_id=1 to match userspace mapping. */
	gdev.core_id = MMR_CORE_NES;
	gdev.map_version = 0;

	gdev.region_count = 0;

	mutex_lock(&gdev.lock);
	if (nes_path) {
		gdev.regions[gdev.region_count++] = (struct mmr_region_desc){
			.region_id  = MMR_REGION_NES_CPU_RAM,
			.flags      = MMR_RF_SNAPSHOT,
			.size_bytes = 2048,
		};
	}
	if (snes_path) {
		gdev.regions[gdev.region_count++] = (struct mmr_region_desc){
			.region_id  = MMR_REGION_SNES_WRAM,
			.flags      = MMR_RF_SNAPSHOT,
			.size_bytes = 131072,
		};
	}
	if (gen_path) {
		gdev.regions[gdev.region_count++] = (struct mmr_region_desc){
			.region_id  = MMR_REGION_GEN_68K_RAM,
			.flags      = MMR_RF_SNAPSHOT,
			.size_bytes = 65536,
		};
	}

	gdev.frame_counter = 0;
	mutex_unlock(&gdev.lock);

	if (gdev.region_count == 0) {
		pr_err("mmr_memtap_loopback: no regions enabled. Pass nes_path=... (and/or snes_path/gen_path)\n");
		return -EINVAL;
	}

	r = misc_register(&mmr_misc);
	if (r) {
		pr_err("mmr_memtap_loopback: misc_register failed: %d\n", r);
		return r;
	}

	pr_info("mmr_memtap_loopback: registered /dev/mmr_memtap (regions=%u)\n", gdev.region_count);
	return 0;
}

static void __exit mmr_exit(void)
{
	misc_deregister(&mmr_misc);
	pr_info("mmr_memtap_loopback: unloaded\n");
}

module_init(mmr_init);
module_exit(mmr_exit);

MODULE_LICENSE("MIT");
MODULE_AUTHOR("MiSTer Milestones");
MODULE_DESCRIPTION("MMR memtap loopback device (/dev/mmr_memtap) backed by snapshot files");
