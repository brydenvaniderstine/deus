#!/usr/bin/env python3
"""Regenerate compile_commands.json from the Makefile's freestanding CFLAGS.
Run after adding/removing a kernel/**/*.c file."""
import json
import os
import glob

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(root)

cc = "clang"
target_flags = ["--target=x86_64-elf", "-m64", "-march=x86-64"]
freestanding = ["-ffreestanding", "-fno-stack-protector", "-fno-stack-check",
                "-fno-pic", "-fno-pie", "-mno-red-zone", "-mcmodel=kernel",
                "-mgeneral-regs-only"]
warn = ["-Wall", "-Wextra"]
includes = ["-Ikernel", "-Ilimine"]
cflags = target_flags + ["-std=gnu11"] + freestanding + warn + includes + ["-O2", "-g", "-c"]

entries = []
for f in sorted(glob.glob("kernel/**/*.c", recursive=True)):
    extra = ["-DWX_TEST=1"] if f == "kernel/boot/test_wx.c" else []
    args = [cc] + cflags + extra + ["-o", os.path.splitext(f)[0] + ".o", f]
    entries.append({"directory": root, "file": f, "arguments": args})

with open("compile_commands.json", "w") as fh:
    json.dump(entries, fh, indent=2)
print(f"wrote {len(entries)} entries to compile_commands.json")
