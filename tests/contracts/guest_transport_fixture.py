#!/usr/bin/env python3
"""Controlled QEMU boundary fixture, never evidence of actual guest execution."""
import gzip
import hashlib
import os
from pathlib import Path
import re
import sys


if "--version" in sys.argv:
    print("QEMU emulator version 6.2.0")
    sys.exit(0)

archive = gzip.decompress(Path(sys.argv[sys.argv.index("-initrd") + 1]).read_bytes())
offset = 0
files = {}
while True:
    header = archive[offset:offset + 110]
    size, namesize = int(header[54:62], 16), int(header[94:102], 16)
    name = archive[offset + 110:offset + 110 + namesize - 1].decode("utf-8")
    offset = (offset + 110 + namesize + 3) & ~3
    files[name] = archive[offset:offset + size]
    offset = (offset + size + 3) & ~3
    if name == "TRAILER!!!":
        break
token = re.search(rb"QAPLET_[0-9a-f]{32}", files["init"])[0].decode("ascii")
body = "".join(token + " " + line + "\n" for line in (
    "FACT os Linux", "FACT architecture aarch64", "FACT kernel 6.8.0-147-generic-64k",
    "FACT page_size 65536", "FACT page_size_alias 65536", "FACT smaps_kib 64",
    "HASH /payload/probe " + hashlib.sha256(files["payload/probe"]).hexdigest(),
    "CASE probe CWD /", "CASE probe START 2026-11-03T10:55:26Z", "CASE probe EXIT 7",
    "CASE probe STDOUT Zml4dHVyZQo=", "CASE probe STDERR ", "CASE probe END 2026-11-03T10:55:26Z",
    "HASH_AFTER /payload/probe " + hashlib.sha256(files["payload/probe"]).hexdigest(), "DONE "))
mode = os.environ.get("QAPLET_TRANSPORT_FIXTURE", "console-loss")
console = "\n".join(line for line in body.splitlines() if " CASE probe START " not in line) + "\n"
if mode == "missing-channel":
    console = body
sys.stdout.write(console)
if "-chardev" in sys.argv and mode != "missing-channel":
    option = sys.argv[sys.argv.index("-chardev") + 1]
    target = Path(option.split("path=", 1)[1].split(",", 1)[0])
    if mode == "missing-start":
        body = console
    elif mode == "duplicate-start":
        body += token + " CASE probe START 2026-11-03T10:55:26Z\n"
    elif mode == "missing-done":
        body = body.replace(token + " DONE \n", "")
    elif mode == "false-success":
        body = body.replace("CASE probe EXIT 7", "CASE probe EXIT 0")
    raw = body.encode("ascii")
    footer = token + " JOURNAL " + str(len(raw)) + " " + hashlib.sha256(raw).hexdigest() + "\n"
    if mode == "bad-footer":
        footer = footer.replace(hashlib.sha256(raw).hexdigest(), "0" * 64)
    target.write_bytes(raw + footer.encode("ascii"))
sys.exit(9 if mode == "qemu-fail" else 0)
