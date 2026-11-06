#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Package a fixed-base freestanding x64 fixture with explicit Windows imports."""
import struct
from pathlib import Path

BASE = 0x140000000
IMPORTS = {
    "KERNEL32.dll": ["GetCommandLineW", "GetEnvironmentStringsW", "FreeEnvironmentStringsW",
        "GetEnvironmentVariableW", "GetCurrentDirectoryW", "GetStdHandle", "ReadFile", "WriteFile",
        "GetHandleInformation", "GetFileInformationByHandle", "LocalFree", "ExitProcess", "Sleep", "CloseHandle"],
    "SHELL32.dll": ["CommandLineToArgvW"],
}


def layout():
    blob = bytearray(4096)
    cursor, slots = 0x300, {}
    for index, (dll, names) in enumerate(IMPORTS.items()):
        original, iat, dllname = 0x80 + index * 0x100, 0x100 + index * 0x100, 0x700 + index * 0x80
        blob[dllname:dllname + len(dll) + 1] = dll.encode() + b"\0"
        struct.pack_into("<IIIII", blob, index * 20, 0x3000 + original, 0, 0, 0x3000 + dllname, 0x3000 + iat)
        for n, name in enumerate(names):
            value = b"\0\0" + name.encode() + b"\0"
            blob[cursor:cursor + len(value)] = value
            struct.pack_into("<Q", blob, original + 8 * n, 0x3000 + cursor)
            struct.pack_into("<Q", blob, iat + 8 * n, 0x3000 + cursor)
            slots[name] = BASE + 0x3000 + iat + 8 * n
            cursor += (len(value) + 1) & ~1
    return blob, slots


def link_flags():
    return ["--defsym=" + name + "=" + hex(address) for name, address in layout()[1].items()]


def package(binary, output, entry_offset):
    size = Path(binary).stat().st_size
    if not 0 < size <= 0x2000 or not 0 <= entry_offset < size:
        raise ValueError("Freestanding PE text exceeds reserved bounds or entry is invalid")
    code = Path(binary).read_bytes()
    imports, _ = layout()
    padded = (len(code) + 511) & ~511
    header = bytearray(512)
    header[:2] = b"MZ"
    struct.pack_into("<I", header, 0x3C, 0x80)
    header[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", header, 0x84, 0x8664, 2, 0, 0, 0, 240, 0x23)
    opt = 0x98
    struct.pack_into("<HBBIIIIIQII", header, opt, 0x20B, 0, 0, padded, 4096, 0, 0x1000 + entry_offset, 0x1000, BASE, 4096, 512)
    struct.pack_into("<HHHHHH", header, opt + 40, 6, 0, 0, 0, 6, 0)
    struct.pack_into("<IIIHHQQQQII", header, opt + 56, 0x4000, 512, 0, 3, 0x100, 0x100000, 0x1000, 0x100000, 0x1000, 0, 16)
    struct.pack_into("<II", header, opt + 120, 0x3000, 60)
    for index, (name, size, rva, raw, offset, flags) in enumerate([
        (b".text", len(code), 0x1000, padded, 512, 0x60000020),
        (b".idata", 4096, 0x3000, 4096, 512 + padded, 0xC0000040),
    ]):
        struct.pack_into("<8sIIIIIIHHI", header, opt + 240 + index * 40, name, size, rva, raw, offset, 0, 0, 0, 0, flags)
    Path(output).write_bytes(header + code + b"\0" * (padded - len(code)) + imports)
