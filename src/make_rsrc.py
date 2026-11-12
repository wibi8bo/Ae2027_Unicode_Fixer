#!/usr/bin/env python3
"""Generate the PiPL resource (Contents/Resources/*.rsrc) AE needs to see a plugin.

File layout:
    [16-byte header] [pad to 0x100] [resource data] [resource map]

Each PiPL property is stored as:
    '8BIM' + key(4) + 4 zero bytes + len(u32 BE) + data, padded to 4 bytes.
"""
import struct
import sys

# Standard one-resource map for a single 'PiPL' entry (id 16000, unnamed).
MAP_TAIL = bytes.fromhex(
    "0000000009000000001c003200005069504c"
    "0000000a3e80ffff0000000000000000"
)


def pascal(s):
    b = s.encode("latin-1")
    return bytes([len(b)]) + b


def prop(key, data):
    out = b"8BIM" + key.encode() + b"\0\0\0\0" + struct.pack(">I", len(data)) + data
    while len(out) % 4:
        out += b"\0"
    return out


PROPS = [
    ("kind", b"eFKT"),
    ("name", pascal("UTF8 Fixer")),
    ("catg", pascal("AL's Toolkit")),
    ("mi32", pascal("EntryPointFunc")),
    ("mi64", pascal("EntryPointFunc")),
    ("ma64", pascal("EntryPointFunc")),
    ("eVER", bytes.fromhex("00080028")),
    ("ePVR", bytes.fromhex("00020000")),
    ("eSVR", bytes.fromhex("000d0019")),
    ("eINF", bytes.fromhex("0000")),
    ("eGLO", bytes.fromhex("02080604")),
    ("eGL2", bytes.fromhex("08081489")),
    ("eMNA", pascal("UTF8 Fixer")),
    ("aeFL", bytes.fromhex("00000000")),
]


def build(path):
    body = b"\0\0\0\0" + struct.pack(">I", len(PROPS))
    body += b"".join(prop(k, v) for k, v in PROPS)

    data_len = 4 + len(body)
    header = struct.pack(">IIII", 0x100, 0x100 + data_len, data_len, 0x32)
    blob = header + b"\0" * (0x100 - len(header))
    blob += struct.pack(">I", len(body)) + body + header + MAP_TAIL

    with open(path, "wb") as f:
        f.write(blob)
    print(f"{path}  {len(blob)} bytes")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(f"usage: {sys.argv[0]} <output.rsrc>")
    build(sys.argv[1])
