// SPDX-License-Identifier: MIT
// mmr_memtap_loopback.c
//
// Loopback /dev/mmr_memtap implementation for early development.
// Provides the same ioctl ABI as kernel/mmr_memtap.h but reads region snapshots
// from regular files on disk (nes_path/snes_path/gen_path).
//
// This unblocks "real device mode" testing of daemon/mmr-daemon without FPGA patches.
// Later: replace file-backed reads with FPGA bridge reads, keep ABI unchanged.

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>
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

// Device state
struct mmr_loopback {
    struct mutex lock;
    u32 core_id;

    // Region descriptors we expose
    struct mmr_region_desc regions[MMR_MAX_REGIONS];
    u32 region_count;

    // Currently selected region
    u32 selected_region;

    // Simple frame counter to mimic cadence
    u64 frame_counter;
};

static struct mmr_loopback gdev;

static const char *path_for_region(u32 region_id)
{
    switch (region_id) {
        case MMR_REGION_NES_CPU_RAM: return nes_path;
        case MMR_REGION_SNES_WRAM:   return snes_path;
        case MMR_REGION_GEN_68K_RAM: return gen_path;
        default: return NULL;
    }
}

static ssize_t file_read_all(const char *path, loff_t offset, void *buf, size_t len)
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
    return r;
}

static long mmr_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
    long ret = 0;

    mutex_lock(&gdev.lock);

    switch (cmd) {
        case MMR_IOCTL_GET_INFO: {
            struct mmr_info info;
            memset(&info, 0, sizeof(info));
            info.core_id       = gdev.core_id;
            info.region_count  = gdev.region_count;
            info.frame_counter = gdev.frame_counter;

            if (copy_to_user((void __user *)arg, &info, sizeof(info)))
                ret = -EFAULT;
            break;
        }

        case MMR_IOCTL_GET_REGIONS: {
            struct mmr_regions out;
            memset(&out, 0, sizeof(out));
            out.count = gdev.region_count;
            memcpy(out.regions, gdev.regions, sizeof(gdev.regions));

            if (copy_to_user((void __user *)arg, &out, sizeof(out)))
                ret = -EFAULT;
            break;
        }

        case MMR_IOCTL_SELECT_REGION: {
            struct mmr_select sel;
            bool found = false;
            u32 i;

            if (copy_from_user(&sel, (void __user *)arg, sizeof(sel))) {
                ret = -EFAULT;
                break;
            }

            for (i = 0; i < gdev.region_count; i++) {
                if (gdev.regions[i].region_id == sel.region_id) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                ret = -EINVAL;
                break;
            }

            gdev.selected_region = sel.region_id;
            break;
        }

        case MMR_IOCTL_READ_REGION: {
            struct mmr_read req;
            const char *path;
            u32 size = 0;
            ssize_t r;
            void *kbuf;

            if (copy_from_user(&req, (void __user *)arg, sizeof(req))) {
                ret = -EFAULT;
                break;
            }

            // Must match selected region
            if (req.region_id != gdev.selected_region) {
                ret = -EINVAL;
                break;
            }

            path = path_for_region(req.region_id);
            if (!path) {
                ret = -ENOENT;
                break;
            }

            // Get region size from descriptor list
            for (u32 i = 0; i < gdev.region_count; i++) {
                if (gdev.regions[i].region_id == req.region_id) {
                    size = gdev.regions[i].size;
                    break;
                }
            }
            if (!size) {
                ret = -EINVAL;
                break;
            }

            // Bounds: offset + len must be inside region
            if (req.offset > size || req.length > size || (req.offset + req.length) > size) {
                ret = -EINVAL;
                break;
            }

            // Allocate kernel buffer and read backing file
            kbuf = kmalloc(req.length, GFP_KERNEL);
            if (!kbuf) {
                ret = -ENOMEM;
                break;
            }

            r = file_read_all(path, (loff_t)req.offset, kbuf, req.length);
            if (r < 0) {
                kfree(kbuf);
                ret = r;
                break;
            }

            if (r != req.length) {
                // short read: treat as error to keep behavior strict
                kfree(kbuf);
                ret = -EIO;
                break;
            }

            if (copy_to_user((void __user *)(uintptr_t)req.user_ptr, kbuf, req.length)) {
                kfree(kbuf);
                ret = -EFAULT;
                break;
            }

            kfree(kbuf);

            // Mimic frame progress: each read increments frame_counter
            gdev.frame_counter++;
            break;
        }

        default:
            ret = -ENOTTY;
            break;
    }

    mutex_unlock(&gdev.lock);
    return ret;
}

static const struct file_operations mmr_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = mmr_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl   = mmr_ioctl,
#endif
};

static struct miscdevice mmr_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "mmr_memtap",
    .fops  = &mmr_fops,
    // mode is not always honored depending on kernel config, but it's a good default.
    .mode  = 0600,
};

static int __init mmr_init(void)
{
    int r;

    mutex_init(&gdev.lock);

    // Default to NES core_id=1 to match your userspace mapping.
    gdev.core_id = 1;

    // Expose whichever regions have a backing path configured.
    gdev.region_count = 0;

    if (nes_path) {
        gdev.regions[gdev.region_count++] = (struct mmr_region_desc){
            .region_id = MMR_REGION_NES_CPU_RAM,
            .size      = 2048
        };
        gdev.selected_region = MMR_REGION_NES_CPU_RAM;
    }
    if (snes_path) {
        gdev.regions[gdev.region_count++] = (struct mmr_region_desc){
            .region_id = MMR_REGION_SNES_WRAM,
            .size      = 131072
        };
        if (!gdev.selected_region)
            gdev.selected_region = MMR_REGION_SNES_WRAM;
    }
    if (gen_path) {
        gdev.regions[gdev.region_count++] = (struct mmr_region_desc){
            .region_id = MMR_REGION_GEN_68K_RAM,
            .size      = 65536
        };
        if (!gdev.selected_region)
            gdev.selected_region = MMR_REGION_GEN_68K_RAM;
    }

    gdev.frame_counter = 0;

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
