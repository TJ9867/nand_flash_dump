#!/usr/bin/env python3
import serial
import time
import tqdm
import argparse
import datetime
import pathlib


def parse_args():
    parser = argparse.ArgumentParser(
        description="Tool to use with RP2040 NAND flash dumper. "
        "By default it will dump the flash to 'dump.dat'."
    )

    parser.add_argument(
        "-s",
        "--start-page",
        type=int,
        default=0,
        help="The page offset to start reading from.",
    )

    parser.add_argument(
        "-n",
        "--num-pages",
        type=int,
        default=(64 * 2048),
        help="Size of flash in pages.",
    )

    parser.add_argument(
        "-p",
        "--page-size",
        type=int,
        default=(4096 + 256),
        help="Size of a single page in bytes (including oob).",
    )

    parser.add_argument(
        "-x",
        "--oob_size",
        type=int,
        default=(256),
        help="Size of oob area per-page in bytes.",
    )

    parser.add_argument(
        "-f",
        "--filename",
        type=str,
        default=None,
        help="Filename to output the flash contents to.",
    )

    parser.add_argument(
        "-d",
        "--devname",
        type=str,
        default="/dev/ttyACM0",
        help="Path to the serial device used for dumping.",
    )

    parser.add_argument(
        "-b",
        "--baudrate",
        type=int,
        default=115200,
        help="Baudrate to use with serial device.",
    )

    return parser.parse_args()


def reset_page_number(ser):
    ser.write(b"2")


def set_page_number(ser, page_no):
    p1 = page_no & 0xFF
    p2 = (page_no >> 8) & 0xFF
    p3 = (page_no >> 16) & 0x1
    ser_cmd = b"3" + bytes([p1, p2, p3])
    print(f"Writing bytes: {ser_cmd}")
    ser.write(ser_cmd)


def read_page(ser, pagesize):
    ser.write(b"1")
    return ser.read(pagesize * 2)


def main():
    args = parse_args()

    if args.filename is None:
        output_dir = pathlib.Path("dumps")
        if not output_dir.exists():
            output_dir.mkdir()
        ts = datetime.datetime.now().strftime("%m_%d_%y_%H:%M:%S")
        args.filename = output_dir / pathlib.Path(f"dump_{ts}.dat")

    print(
        f"Dumping {args.num_pages} pages of size {
          args.page_size} to {args.filename}. "
        f"Starting from page {args.start_page}"
    )

    with serial.Serial(args.devname, baudrate=args.baudrate) as s, open(
        args.filename, "wb"
    ) as wf:
        try:
            s.read_all()
            time.sleep(0.5)
            set_page_number(s, args.start_page)

            for i in tqdm.tqdm(range(args.num_pages)):
                x = read_page(s, args.page_size).decode()
                page = bytes.fromhex(x)
                wf.write(page)
        except KeyboardInterrupt:
            s.read_all()


if __name__ == "__main__":
    main()
