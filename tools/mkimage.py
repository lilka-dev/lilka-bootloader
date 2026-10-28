#!/usr/bin/env python3
"""Compose a full Lilka flash image from the lilka_boot partition table.

Usage:
    mkimage.py --partitions partitions.csv --bootloader bootloader.bin \
        --partition-table partition-table.bin [--otadata ota_data.bin] \
        -o lilka_boot.img LABEL=FILE [LABEL=FILE ...]

Each LABEL=FILE is written at the offset of the partition with that label.
The image is 0xFF-padded up to the end of the last written partition, so it
can be flashed at 0x0 with `esptool.py write_flash 0x0 lilka_boot.img`.
"""
import argparse
import csv
import os
import sys

BOOTLOADER_OFFSET = 0x0
PARTITION_TABLE_OFFSET = 0x8000


def parse_size(value):
    value = value.strip()
    mult = 1
    if value[-1:].upper() == "K":
        mult, value = 1024, value[:-1]
    elif value[-1:].upper() == "M":
        mult, value = 1024 * 1024, value[:-1]
    return int(value, 0) * mult


def read_partitions(path):
    parts = {}
    with open(path) as f:
        rows = (line for line in f if line.strip() and not line.lstrip().startswith("#"))
        for row in csv.reader(rows):
            row = [c.strip() for c in row]
            name, ptype, subtype, offset, size = row[:5]
            parts[name] = {"type": ptype, "subtype": subtype, "offset": parse_size(offset), "size": parse_size(size)}
    return parts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--partitions", required=True)
    ap.add_argument("--bootloader", required=True)
    ap.add_argument("--partition-table", required=True)
    ap.add_argument("--otadata", help="initial otadata (blank = boot ota_0)")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("files", nargs="+", metavar="LABEL=FILE")
    args = ap.parse_args()

    parts = read_partitions(args.partitions)
    writes = [
        (BOOTLOADER_OFFSET, PARTITION_TABLE_OFFSET - BOOTLOADER_OFFSET, args.bootloader, "bootloader"),
        (PARTITION_TABLE_OFFSET, 0x1000, args.partition_table, "partition table"),
    ]
    if args.otadata:
        p = parts["otadata"]
        writes.append((p["offset"], p["size"], args.otadata, "otadata"))
    for item in args.files:
        label, _, path = item.partition("=")
        if label not in parts:
            sys.exit(f"unknown partition label: {label}")
        p = parts[label]
        writes.append((p["offset"], p["size"], path, label))

    writes.sort()
    image = bytearray()
    for offset, limit, path, what in writes:
        data = open(path, "rb").read()
        if len(data) > limit:
            sys.exit(f"{what}: {path} is {len(data)} bytes, partition holds {limit}")
        if len(image) > offset:
            sys.exit(f"{what}: overlaps previous data at 0x{offset:x}")
        image += b"\xff" * (offset - len(image))
        image += data
        print(f"0x{offset:07x} {what:16} {len(data):>9} / {limit:<9} {os.path.basename(path)}")

    with open(args.output, "wb") as f:
        f.write(image)
    print(f"{args.output}: {len(image)} bytes")


if __name__ == "__main__":
    main()
