#!/usr/bin/env python3
"""Validate the structural claims made in docs/executable-analysis.md against the real assets.

This is RESEARCH tooling: it checks hypotheses derived from the executable (sizes, offsets,
cross-references). It is deliberately not a reusable reader library; the independent readers for
the port are a later phase.

usage: validate_assets.py /path/to/game/dir      (read-only; extracts nothing to disk)
"""
import glob
import math
import os
import struct
import sys
from collections import Counter

KEY = b'SOFTWAREREFINERY'          # XOR key stored in the exe at VA 0x1F180


def u16(d, o):
    return struct.unpack_from('<H', d, o)[0]


def u32(d, o):
    return struct.unpack_from('<I', d, o)[0]


class Report:
    def __init__(self):
        self.rows = []

    def check(self, name, ok, detail=''):
        self.rows.append((name, ok, detail))
        print(('PASS ' if ok else 'FAIL ') + name + (('  ' + detail) if detail else ''))

    def summary(self):
        bad = [r for r in self.rows if not r[1]]
        print('\n%d checks, %d failed' % (len(self.rows), len(bad)))
        return 1 if bad else 0


def parse_res(path):
    d = open(path, 'rb').read()
    diroff = u32(d, len(d) - 4)
    hdr = u32(d, diroff)
    n = hdr & 0x7fffffff
    ents = []
    p = diroff + 4
    for _ in range(n):
        flags = u32(d, p)
        name = bytes(a ^ b for a, b in zip(d[p + 4:p + 20], KEY)) if hdr & 0x80000000 else d[p + 4:p + 20]
        off, size = struct.unpack_from('<II', d, p + 20)
        ents.append((flags, name.rstrip(b'\0').decode('latin1'), off, size))
        p += 28
    return d, diroff, hdr, n, ents, p


def main(root):
    r = Report()
    # ---- RES archives -------------------------------------------------------------------
    arch = {}
    for f in ('SLIPSTRM.RES', 'SLIPCD.RES', 'SLIPMED.RES', 'SLIPMIN.RES', 'SLIPMAX.RES'):
        path = os.path.join(root, f)
        if not os.path.exists(path):
            continue
        d, diroff, hdr, n, ents, p = parse_res(path)
        r.check('RES %s: directory parse ends at EOF-4' % f, p == len(d) - 4,
                'entries=%d hdr=0x%08x' % (n, hdr))
        pos = 0
        contiguous = True
        for fl, nm, off, size in ents:
            contiguous &= (off == pos)
            pos = off + size
        r.check('RES %s: entries are contiguous and fill [0,diroff)' % f, contiguous and pos == diroff)
        r.check('RES %s: entry flags all == 2' % f, all(e[0] == 2 for e in ents))
        arch[f] = (d, ents)
    main_key = 'SLIPCD.RES' if 'SLIPCD.RES' in arch else next(iter(arch), None)
    if main_key is None:
        print('no RES files found'); return 2
    d, ents = arch[main_key]
    files = {}
    for fl, nm, off, size in ents:
        files[nm.replace(' ', '')] = d[off:off + size]

    def of(ext):
        return {k: v for k, v in files.items() if k.upper().endswith('.' + ext)}

    # ---- MAT ----------------------------------------------------------------------------
    mats = of('MAT')
    r.check('MAT: size == 4 + 46*count and version == 1 (%d files)' % len(mats),
            all(len(v) == 4 + 46 * u16(v, 0) and u16(v, 2) == 1 for v in mats.values()))
    # ---- PAL ----------------------------------------------------------------------------
    pals = of('PAL')
    r.check('PAL: size == 4 + 3*count, first=0, count=248, 6-bit components (%d files)' % len(pals),
            all(len(v) == 4 + 3 * u16(v, 2) and u16(v, 0) == 0 and u16(v, 2) == 248 and max(v[4:]) <= 63
                for v in pals.values()))
    # ---- SPR ----------------------------------------------------------------------------
    sprs = of('SPR')
    cnt = Counter()
    for v in sprs.values():
        w, h = u16(v, 0), u16(v, 2)
        extra = len(v) - 16 - w * h
        cnt[extra] += 1
    ok = all(e in (0, 748, 559) for e in cnt)
    r.check('SPR: size == 16 + w*h (+748 or +559 trailing palette) (%d files)' % len(sprs), ok, str(dict(cnt)))
    trail_ok = True
    for v in sprs.values():
        w, h = u16(v, 0), u16(v, 2)
        extra = len(v) - 16 - w * h
        if extra:
            first, count = u16(v, 16 + w * h), u16(v, 16 + w * h + 2)
            trail_ok &= (extra == 4 + 3 * count)
    r.check('SPR: trailing block is {u16 first,u16 count,count*RGB}', trail_ok)
    # ---- SHP ----------------------------------------------------------------------------
    shps = of('SHP')
    ok = True
    for k, v in shps.items():
        if u16(v, 0) != 12:
            ok = False; break
        pts, polys = u32(v, 0x10), u32(v, 0x14)
        n = u16(v, polys)
        p = polys + 2
        for _ in range(n):
            info = u16(v, p)
            N = info & 0x3fff
            p += 12 + 2 * N + (6 * N if info & 0x4000 else 0) + (4 * N if info & 0x8000 else 0)
        if p != pts:
            ok = False; break
    r.check('SHP: version 12 and polygon block ends exactly at points block (%d files)' % len(shps), ok)
    ok = True
    for k, v in shps.items():
        mats_off, pts, polys = u32(v, 0x18), u32(v, 0x10), u32(v, 0x14)
        sort_off = u32(v, 0xc)
        pts_end = pts + 2 + 6 * u16(v, pts)
        ok &= (u32(v, 8) == len(v))                                   # +8 = total size
        ok &= (polys == 0x52)                                          # header is 0x52 bytes
        ok &= (sort_off == 0 or sort_off == pts_end)                   # sort tree follows points
        ok &= (mats_off + 2 + 18 * u16(v, mats_off) == len(v))         # material table ends at EOF
        ok &= (u16(v, 0x38) == 0 or sort_off == 0)                    # +0x38 (NOSORT) set => no sort data
    r.check('SHP: header +8 = file size, polys@0x52, sort data follows points, material table ends at EOF, +0x38!=0 implies no sort data', ok)
    # ---- ART ----------------------------------------------------------------------------
    arts = of('ART')
    ok = True
    for k, v in arts.items():
        ok &= (u32(v, 0) == 18)
        root = u32(v, 0x20)
        seen, stack = [], [root]
        while stack:
            o = stack.pop()
            if o == 0 or o in seen:
                continue
            seen.append(o)
            stack.extend([u32(v, o + 4), u32(v, o + 8)])
        seen.sort()
        ends = [o + 0x1d0 + 0x10 * u32(v, o + 0x1cc) for o in seen]
        ok &= (seen[0] == 0x58 and ends[:-1] == seen[1:] and ends[-1] == len(v))
    r.check('ART: version 18, node tree (child@+4, sibling@+8) tiles the file; node size = 0x1D0 + 16*refcount (%d files)' % len(arts), ok)
    # ---- TRK / TRC / TRD ----------------------------------------------------------------
    trks, trcs, trds = of('TRK'), of('TRC'), of('TRD')
    ok = True
    for k, v in trks.items():
        cells = u16(v, 0xc); n = u16(v, cells)
        ok &= (u16(v, 0) == len(v) and u16(v, 2) == 0x2b and cells + 2 + 24 * n == u16(v, 0xe))
    pool_ok = all(u16(v, u16(v, 0xe)) * 8 + u16(v, 0xe) + 2 == len(v) for v in trks.values())
    r.check('TRK: size word, version 0x2B, cell table ends at header+0xE (%d files)' % len(trks), ok)
    r.check('TRK: corner pool = u16 count + count*8 bytes, ends at EOF (%d files)' % len(trks), pool_ok)
    ok = True
    for k, v in trcs.items():
        lst, mt = u16(v, 6), u16(v, 8)
        n = u16(v, lst); p = lst + 2
        for _ in range(n):
            size = u16(v, p)
            lists = sorted(o for o in (u16(v, p + 4), u16(v, p + 6)) if o)   # polygon lists A(+4), B(+6)
            pos = None
            for o in lists:
                if pos is not None and pos != o:
                    ok = False
                c = u16(v, o); q = o + 2
                for _ in range(c):
                    info = u16(v, q); N = info & 0x7fff
                    q += (N * 6 + 12) if info & 0x8000 else (N * 2 + 12)
                pos = q
            if lists:
                ok &= (pos == p + size)       # the polygon lists tile the tail of the record
            p += size
        ok &= (p == mt) and (mt + 2 + 18 * u16(v, mt) == len(v)) and u16(v, 0) == len(v)
    r.check('TRC: record chain ends at material table; polygon lists (B before A when both) tile the tail of each record; table ends at EOF (%d files)' % len(trcs), ok)
    ok = True
    for k, v in trds.items():
        n = u16(v, u16(v, 2)); p = u16(v, 2) + 2
        for _ in range(n):
            sz = u16(v, p); arr = u16(v, p + 8)
            if arr:
                ok &= (p <= arr and arr + 2 + 0x46 * u16(v, arr) <= p + sz)
            p += sz
        ok &= (p == u16(v, 8)) and u16(v, 0) == len(v)
    r.check('TRD: group chain ends at header+8; shape arrays lie inside their group (%d files)' % len(trds), ok)
    # ---- CAM ----------------------------------------------------------------------------
    cams = of('CAM')
    r.check('CAM: 720 bytes = 60 x (int32 x,y,z) (%d files)' % len(cams), all(len(v) == 720 for v in cams.values()))
    # ---- MATHS.BIN ----------------------------------------------------------------------
    mb = files.get('MATHS.BIN')
    if mb:
        o0, o2, o4 = struct.unpack_from('<3H', mb, 0)
        B = struct.unpack_from('<8193H', mb, o0)
        C = struct.unpack_from('<8193H', mb, o2)
        A = struct.unpack_from('<%dH' % ((len(mb) - o4) // 2), mb, o4)
        e1 = max(abs(B[i] - round(math.sin(i * math.pi / 2 / 8192) * 16384)) for i in range(8193))
        e2 = max(abs(C[i] - round(math.asin(i / 8192) / (math.pi / 2) * 16384)) for i in range(8193))
        M = len(A) - 1
        e3 = max(abs(A[i] - round(math.atan(i / M) / (2 * math.pi) * 65536)) for i in range(len(A)))
        r.check('MATHS.BIN: table0 = sin quarter wave 2.14 (max err %d)' % e1, e1 <= 1)
        r.check('MATHS.BIN: table1 = asin, 0x4000 = 90 deg (max err %d)' % e2, e2 <= 1)
        r.check('MATHS.BIN: table2 = atan(i/4096), 0x10000 = 360 deg (max err %d, %d entries)' % (e3, len(A)), e3 <= 1 and len(A) == 4097)
    return r.summary()


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else '.'))
