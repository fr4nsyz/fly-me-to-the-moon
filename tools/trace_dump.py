#!/usr/bin/env python3
import struct
import sys

def main():
    if len(sys.argv) < 2:
        print("usage: trace_dump.py <trace.sdct>", file=sys.stderr)
        sys.exit(1)
    path = sys.argv[1]
    with open(path, 'rb') as f:
        hdr = f.read(32)
        if len(hdr) < 32:
            print("short header")
            return
        magic = hdr[0:8]
        version = struct.unpack('<I', hdr[8:12])[0]
        flags = struct.unpack('<I', hdr[12:16])[0]
        meta_len = struct.unpack('<I', hdr[16:20])[0]
        rec_size = struct.unpack('<I', hdr[20:24])[0]
        print(f"magic={magic} version={version} flags={flags} meta_len={meta_len} rec_size={rec_size}")
        if meta_len:
            meta = f.read(meta_len)
            print("meta:", meta.decode('utf-8'))
        i = 0
        while True:
            rec = f.read(40)
            if len(rec) == 0:
                break
            if len(rec) < 40:
                print(f"[{i}] SHORT ({len(rec)} bytes)")
                break
            t = struct.unpack('<I', rec[0:4])[0]
            op = struct.unpack('<I', rec[4:8])[0]
            occ = struct.unpack('<Q', rec[8:16])[0]
            o0 = struct.unpack('<Q', rec[16:24])[0]
            o1 = struct.unpack('<Q', rec[24:32])[0]
            res = struct.unpack('<Q', rec[32:40])[0]
            if t == 0:
                print(f"[{i}] EVENT op={op} occ={occ} o0=0x{o0:016x} o1=0x{o1:016x} res=0x{res:016x}")
            elif t == 1:
                print(f"[{i}] EXIT  op={op} occ={occ} code={o0} det={o1} res=0x{res:016x}")
                break
            else:
                print(f"[{i}] BAD type={t}")
                break
            i += 1

if __name__ == '__main__':
    main()
