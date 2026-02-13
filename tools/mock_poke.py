#!/usr/bin/env python3
import argparse
import os

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--file", required=True, help="Path to mock RAM bin file")
    ap.add_argument("--offset", type=int, required=True)
    ap.add_argument("--value", type=int, required=True)
    args = ap.parse_args()

    if not os.path.exists(args.file):
        raise SystemExit(f"File not found: {args.file}")
    if args.offset < 0:
        raise SystemExit("offset must be >= 0")
    if args.value < 0 or args.value > 255:
        raise SystemExit("value must be 0..255")

    with open(args.file, "r+b") as f:
        f.seek(args.offset)
        f.write(bytes([args.value]))
    print(f"wrote {args.value} at offset {args.offset} in {args.file}")

if __name__ == "__main__":
    main()
