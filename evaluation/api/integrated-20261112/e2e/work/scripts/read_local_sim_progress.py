#!/usr/bin/env python3
"""Read-only /proc snapshots. Run inside the ARM VM; never pause simulators."""
import argparse
import glob
import json
import os
from pathlib import Path
import struct
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument("--offset-helper", type=Path, required=True)
p.add_argument("--output", type=Path)
a = p.parse_args()
offsets = {name: int(value) for name, value in
           (line.split() for line in subprocess.check_output(
               [str(a.offset_helper)], text=True).splitlines())}


def elf_info(path):
    data = Path(path).read_bytes()
    if data[:6] != b"\x7fELF\x02\x01":
        raise ValueError("requires ELF64 little endian")
    h = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
    phoff, shoff = h[5], h[6]
    phsize, phnum, shsize, shnum = h[9:13]
    pattern = None
    for i in range(phnum):
        typ, flags, pos, va, pa, size, _, _ = struct.unpack_from(
            "<IIQQQQQQ", data, phoff+i*phsize)
        if typ == 1 and flags & 1 and size >= 16:
            pattern = (pa, data[pos:pos+16])
            break
    sections = [struct.unpack_from("<IIQQQQIIQQ", data, shoff+i*shsize)
                for i in range(shnum)]
    functions, objects = [], {}
    for s in sections:
        if s[1] != 2:
            continue
        strings = sections[s[6]]
        names = data[strings[4]:strings[4]+strings[5]]
        for pos in range(s[4], s[4]+s[5], s[9]):
            no, info, _, _, va, size = struct.unpack_from("<IBBHQQ", data, pos)
            end = names.find(b"\0", no)
            name = names[no:end].decode(errors="replace")
            if info & 15 == 2 and va:
                functions.append((va, size, name))
            if name.startswith("debug_"):
                objects[name] = va
    return pattern, sorted(functions), objects


result = {"timestamp": time.time(), "offsets": offsets, "simulators": []}
for cmdpath in glob.glob("/proc/[0-9]*/cmdline"):
    try:
        argv = Path(cmdpath).read_bytes().split(b"\0")
        if not argv or not argv[0].endswith(b"/Variane_testharness"):
            continue
        pid = int(cmdpath.split("/")[2])
        elf = next(x.decode() for x in argv[1:] if x.endswith(b".elf"))
        pattern, functions, objects = elf_info(elf)
        regions = []
        for line in Path(f"/proc/{pid}/maps").read_text().splitlines():
            cols = line.split()
            lo, hi = [int(x, 16) for x in cols[0].split("-")]
            if cols[1] == "rw-p" and len(cols) == 5 and hi-lo > 0x20000000:
                regions.append((hi-lo, lo))
        mapping = max(regions)[1]
        fd = os.open(f"/proc/{pid}/mem", os.O_RDONLY)
        try:
            # Validate nonzero CSR activity and actual loaded ELF bytes.
            # The HTIF-visible SRAM has a 256 MiB displacement from the first
            # generated SRAM member in this build; do not apply it to CSR state.
            candidates = (mapping+0x100, mapping+0x100+0x10000000)
            read = lambda address: struct.unpack("<Q", os.pread(fd, 8, address))[0]
            root, sram = next((base, base+offsets["sram"]+extra)
                             for base in candidates
                             for extra in (0, 0x10000000)
                             if read(base+offsets["cycle"]) > 0
                             and 0 < read(base+offsets["instret"]) <=
                                 2*read(base+offsets["cycle"])
                             and pattern and
                             os.pread(fd, 16, base+offsets["sram"]+extra
                                      +pattern[0]-0x80000000)==pattern[1])
            values = {name: read(root+offsets[name]) for name in ("cycle", "instret", "pc")}
            debug = {name: read(sram+va-0x80000000)
                     for name, va in objects.items() if va >= 0x80000000}
            pc = values["pc"]
            function = next((name for va, size, name in reversed(functions)
                             if va <= pc and (not size or pc < va+size)), "unknown")
            tohost = read(sram+0x1000)
            result["simulators"].append(dict(
                pid=pid, tag=Path(elf).stem, **values,
                pc_hex=hex(pc), pc_function=function, debug=debug,
                tohost=tohost, root=hex(root), sram=hex(sram),
                elf_load_verified=True))
        finally:
            os.close(fd)
    except (OSError, StopIteration, ValueError) as exc:
        result["simulators"].append({"pid": cmdpath.split("/")[2], "error": str(exc)})
text = json.dumps(result, indent=2)
print(text)
if a.output:
    a.output.write_text(text+"\n")
