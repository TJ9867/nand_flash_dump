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
        "-i",
        "--info",
        action="store_true",
        help="Print out info from the NAND flash and exit",
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
        default=None,
        help="Size of a single page in bytes (including oob).",
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

    parser.add_argument(
        "-z",
        "--zlowmo",
        action="store_true",
        help="Use the super-slow page set method",
    )

    return parser.parse_args()


def reset_page_number(ser):
    ser.write(b"2")


def set_page_number(ser, page_no):
    p1 = page_no & 0xFF
    p2 = (page_no >> 8) & 0xFF
    p3 = (page_no >> 16) & 0x1
    ser_cmd = b"3" + bytes([p1, p2, p3])
    ser.write(ser_cmd)


def read_page(ser, pagesize):
    ser.write(b"1")
    return ser.read(pagesize * 2)


def get_flash_sizes(ser):
    ser.write(b"5")
    time.sleep(0.1)
    page_size, oob_size, total_size = ser.read_all().split(b",")
    return int(page_size) + int(oob_size), int(total_size)


def get_flash_info(ser):
    ser.write(b"0")
    time.sleep(0.1)
    return ser.read_all().decode()


def main():
    args = parse_args()

    if args.filename is None:
        output_dir = pathlib.Path("dumps")
        if not output_dir.exists():
            output_dir.mkdir()
        ts = datetime.datetime.now().strftime("%m_%d_%y_%H:%M:%S")
        args.filename = output_dir / pathlib.Path(f"dump_{ts}.dat")

    with serial.Serial(args.devname, baudrate=args.baudrate) as s:
        try:
            s.read_all()
            time.sleep(0.5)

            if args.info:
                print(get_flash_info(s))
                return

            if args.page_size is None:
                print(f"Getting page size automatically...")
                args.page_size, flash_size_b = get_flash_sizes(s)
                print(f"Got page size (total bytes): {args.page_size}")
                print(f"Got flash size (total bytes): {flash_size_b}")
                if args.num_pages != flash_size_b // args.page_size:
                    print(
                        f"Warning: calculated pages != calculated number"
                        f"of pages {args.page_size} vs "
                        f"{flash_size_b // args.page_size}."
                    )

            print(
                f"Dumping {args.num_pages} pages of size "
                f"{args.page_size} to {args.filename}. "
                f"Starting from page {args.start_page}"
            )

            set_page_number(s, args.start_page)

            with open(args.filename, "wb") as wf:
                for i in tqdm.tqdm(range(args.num_pages)):
                    if args.zlowmo:
                        set_page_number(s, i)
                    x = read_page(s, args.page_size).decode()
                    page = bytes.fromhex(x)
                    wf.write(page)

        except KeyboardInterrupt:
            s.read_all()


if __name__ == "__main__":
    main()
