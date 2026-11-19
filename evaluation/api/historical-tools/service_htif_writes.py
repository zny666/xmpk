#!/usr/bin/env python3
import argparse
import os
import struct
import time
from pathlib import Path


GUEST_BASE = 0x80000000
TOHOST_OFFSET = 0x1000
FROMHOST_OFFSET = 0x1040
SYS_WRITE = 64


def read_u64(fd: int, address: int) -> int:
    data = os.pread(fd, 8, address)
    if len(data) != 8:
        raise ProcessLookupError(f"short read at 0x{address:x}")
    return struct.unpack("<Q", data)[0]


def write_u64(fd: int, address: int, value: int) -> None:
    written = os.pwrite(fd, struct.pack("<Q", value), address)
    if written != 8:
        raise ProcessLookupError(f"short write at 0x{address:x}")


def guest_address(sram_base: int, address: int) -> int:
    if address < GUEST_BASE:
        raise RuntimeError(f"invalid guest address 0x{address:x}")
    return sram_base + address - GUEST_BASE


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("pid", type=int)
    parser.add_argument("root_pointer", type=lambda value: int(value, 0))
    parser.add_argument("output", type=Path)
    parser.add_argument("--merge-log", type=Path)
    parser.add_argument("--sram-offset", type=lambda value: int(value, 0),
                        default=268478824)
    parser.add_argument("--dcache-bank-offset", type=lambda value: int(value, 0),
                        default=536981280)
    parser.add_argument("--dcache-way-stride", type=lambda value: int(value, 0),
                        default=2048)
    parser.add_argument("--dcache-ways", type=int, default=8)
    parser.add_argument("--fromhost-cache-index", type=int, default=4)
    parser.add_argument("--poll-ms", type=float, default=2.0)
    args = parser.parse_args()

    memory_path = Path(f"/proc/{args.pid}/mem")
    sram_base = args.root_pointer + args.sram_offset
    tohost_host = sram_base + TOHOST_OFFSET
    fromhost_host = sram_base + FROMHOST_OFFSET
    args.output.parent.mkdir(parents=True, exist_ok=True)

    requests = 0
    with memory_path.open("r+b", buffering=0) as memory, args.output.open("wb") as output:
        fd = memory.fileno()
        try:
            while memory_path.exists():
                request = read_u64(fd, tohost_host)
                if request == 0 or request & 1:
                    time.sleep(args.poll_ms / 1000.0)
                    continue

                magic_host = guest_address(sram_base, request)
                syscall_number, file_descriptor, buffer_address, length = struct.unpack(
                    "<QQQQ", os.pread(fd, 32, magic_host)
                )
                if syscall_number != SYS_WRITE:
                    raise RuntimeError(
                        f"unsupported syscall {syscall_number} at 0x{request:x}"
                    )
                if file_descriptor not in (1, 2):
                    raise RuntimeError(f"unsupported file descriptor {file_descriptor}")

                payload_host = guest_address(sram_base, buffer_address)
                payload = os.pread(fd, length, payload_host)
                if len(payload) != length:
                    raise RuntimeError(
                        f"short payload read: expected {length}, got {len(payload)}"
                    )
                output.write(payload)
                output.flush()

                write_u64(fd, magic_host, length)
                write_u64(fd, tohost_host, 0)
                write_u64(fd, fromhost_host, 1)
                for way in range(args.dcache_ways):
                    write_u64(
                        fd,
                        args.root_pointer
                        + args.dcache_bank_offset
                        + way * args.dcache_way_stride
                        + args.fromhost_cache_index * 8,
                        1,
                    )
                requests += 1
        except OSError:
            pass

    if args.merge_log:
        with args.output.open("rb") as capture, args.merge_log.open("ab") as merged:
            merged.write(capture.read())
    print(f"pid={args.pid} requests={requests}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
