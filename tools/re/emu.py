"""Minimal 32-bit x86 integer interpreter for the Slipstream 5000 executable (research tool, NOT port code).

It executes the original code from the unmodified SLIPSTRM.EXE image (relocations applied by an.Img) so that individual
engine routines (physics, collision helpers, lighting math ...) can be run on synthetic inputs and used as a reference for
the C++ reimplementation. Only integer instructions that occur in the engine are implemented; the x87 FPU is not.

Usage: see tools/re/emu_tests.py. Hooks (Python callables) replace chosen call targets (allocator, resource manager ...).
"""
import re, struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import an

M32 = 0xFFFFFFFF
REG32 = ['eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi']
REG16 = ['ax', 'cx', 'dx', 'bx', 'sp', 'bp', 'si', 'di']
REG8 = ['al', 'cl', 'dl', 'bl', 'ah', 'ch', 'dh', 'bh']
R32 = {n: i for i, n in enumerate(REG32)}
R16 = {n: i for i, n in enumerate(REG16)}
R8 = {n: i for i, n in enumerate(REG8)}


class EmuError(Exception):
    pass


class Memory:
    PAGE = 0x1000

    def __init__(self, img):
        self.pages = {}
        for base, data in img.segs:
            for off in range(0, len(data), self.PAGE):
                chunk = bytearray(data[off:off + self.PAGE])
                chunk.extend(b'\0' * (self.PAGE - len(chunk)))
                self.pages[(base + off) // self.PAGE] = chunk
        self.dirty_log = None

    def page(self, a):
        p = self.pages.get(a // self.PAGE)
        if p is None:
            p = bytearray(self.PAGE)  # auto-map zero page (heap/stack/unmapped)
            self.pages[a // self.PAGE] = p
        return p

    def r8(self, a):
        return self.page(a)[a % self.PAGE]

    def w8(self, a, v):
        self.page(a)[a % self.PAGE] = v & 0xFF

    def r16(self, a):
        return self.r8(a) | (self.r8(a + 1) << 8)

    def w16(self, a, v):
        self.w8(a, v); self.w8(a + 1, v >> 8)

    def r32(self, a):
        return self.r16(a) | (self.r16(a + 2) << 16)

    def w32(self, a, v):
        self.w16(a, v); self.w16(a + 2, v >> 16)

    def read(self, a, n):
        return bytes(self.r8(a + i) for i in range(n))

    def write(self, a, data):
        for i, b in enumerate(data):
            self.w8(a + i, b)

    def rs16(self, a):
        v = self.r16(a)
        return v - 0x10000 if v & 0x8000 else v

    def rs32(self, a):
        v = self.r32(a)
        return v - (1 << 32) if v & 0x80000000 else v


def sx(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v


class CPU:
    def __init__(self, img=None):
        self.img = img or an.Img()
        self.mem = Memory(self.img)
        self.r = [0] * 8
        self.eip = 0
        self.cf = self.zf = self.sf = self.of = self.pf = self.df = 0
        self.hooks = {}
        self.dec = {}
        self.steps = 0
        self.trace = False
        self.halt_addr = 0xFFFF0000
        self.r[R32['esp']] = 0x00F00000
        self.mem.pages  # stack auto-mapped on demand

    # ---- registers
    def get_reg(self, name):
        if name in R32: return self.r[R32[name]]
        if name in R16: return self.r[R16[name]] & 0xFFFF
        if name in R8:
            i = R8[name]
            return (self.r[i] & 0xFF) if i < 4 else ((self.r[i - 4] >> 8) & 0xFF)
        raise EmuError('reg ' + name)

    def set_reg(self, name, v):
        if name in R32:
            self.r[R32[name]] = v & M32
        elif name in R16:
            i = R16[name]; self.r[i] = (self.r[i] & 0xFFFF0000) | (v & 0xFFFF)
        elif name in R8:
            i = R8[name]
            if i < 4: self.r[i] = (self.r[i] & 0xFFFFFF00) | (v & 0xFF)
            else:
                j = i - 4; self.r[j] = (self.r[j] & 0xFFFF00FF) | ((v & 0xFF) << 8)
        else:
            raise EmuError('reg ' + name)

    @staticmethod
    def reg_size(name):
        if name in R32: return 32
        if name in R16: return 16
        if name in R8: return 8
        return 0

    # ---- operands
    MEMRE = re.compile(r'^(?:(byte|word|dword|qword|xmmword|tbyte) ptr )?(?:([a-z]s):)?\[(.*)\]$')

    def ea(self, expr):
        total = 0
        for term in re.split(r'\s*\+\s*', expr.replace(' - ', ' + -')):
            term = term.strip()
            if not term: continue
            m = re.match(r'^(-?[a-z]{2,3})\*(\d)$', term)
            if m:
                total += self.get_reg(m.group(1)) * int(m.group(2))
            elif re.match(r'^-?0x[0-9a-f]+$', term) or re.match(r'^-?\d+$', term):
                total += int(term, 0)
            else:
                total += self.get_reg(term)
        return total & M32

    def parse(self, op):
        op = op.strip()
        if op in R32 or op in R16 or op in R8:
            return ('reg', op, self.reg_size(op))
        m = self.MEMRE.match(op)
        if m:
            size = {'byte': 8, 'word': 16, 'dword': 32, 'qword': 64}.get(m.group(1), 0)
            return ('mem', m.group(3), size)
        if re.match(r'^-?0x[0-9a-f]+$', op) or re.match(r'^-?\d+$', op):
            return ('imm', int(op, 0), 0)
        raise EmuError('operand ' + op)

    def rd(self, o, size=None):
        k, v, s = o
        if k == 'reg': return self.get_reg(v)
        if k == 'imm': return v & ((1 << (size or 32)) - 1)
        a = self.ea(v); s = s or size or 32
        return self.mem.r8(a) if s == 8 else self.mem.r16(a) if s == 16 else self.mem.r32(a)

    def wr(self, o, val, size=None):
        k, v, s = o
        if k == 'reg': self.set_reg(v, val); return
        a = self.ea(v); s = s or size or 32
        if s == 8: self.mem.w8(a, val)
        elif s == 16: self.mem.w16(a, val)
        else: self.mem.w32(a, val)

    def osize(self, *ops):
        for o in ops:
            if o[2]: return o[2]
        return 32

    # ---- stack
    def push(self, v, size=32):
        self.r[4] = (self.r[4] - (size // 8)) & M32
        (self.mem.w32 if size == 32 else self.mem.w16)(self.r[4], v)

    def pop(self, size=32):
        v = (self.mem.r32 if size == 32 else self.mem.r16)(self.r[4])
        self.r[4] = (self.r[4] + size // 8) & M32
        return v

    # ---- flags
    def setszp(self, res, size):
        m = (1 << size) - 1
        res &= m
        self.zf = int(res == 0)
        self.sf = (res >> (size - 1)) & 1
        self.pf = int(bin(res & 0xFF).count('1') % 2 == 0)

    def cond(self, cc):
        c = {
            'o': self.of, 'no': not self.of, 'b': self.cf, 'ae': not self.cf, 'e': self.zf, 'ne': not self.zf,
            'be': self.cf or self.zf, 'a': not (self.cf or self.zf), 's': self.sf, 'ns': not self.sf,
            'p': self.pf, 'np': not self.pf, 'l': self.sf != self.of, 'ge': self.sf == self.of,
            'le': self.zf or (self.sf != self.of), 'g': (not self.zf) and (self.sf == self.of),
            'nae': self.cf, 'nb': not self.cf, 'z': self.zf, 'nz': not self.zf, 'na': self.cf or self.zf, 'nbe': not (self.cf or self.zf),
            'nge': self.sf != self.of, 'nl': self.sf == self.of, 'ng': self.zf or (self.sf != self.of), 'nle': (not self.zf) and (self.sf == self.of),
            'c': self.cf, 'nc': not self.cf, 'pe': self.pf, 'po': not self.pf,
        }
        return bool(c[cc])

    def flags_word(self):
        return (self.cf) | (self.pf << 2) | (self.zf << 6) | (self.sf << 7) | (self.df << 10) | (self.of << 11) | 0x202

    def set_flags_word(self, v):
        self.cf = v & 1; self.pf = (v >> 2) & 1; self.zf = (v >> 6) & 1; self.sf = (v >> 7) & 1; self.df = (v >> 10) & 1; self.of = (v >> 11) & 1

    # ---- decode cache
    def decode(self, a):
        d = self.dec.get(a)
        if d is None:
            x = self.img.dis1(a)
            if not x:
                raise EmuError('cannot decode at %x' % a)
            _, size, mnem, ops, _bytes = x
            d = (size, mnem, ops, _bytes)
            self.dec[a] = d
        return d

    # ---- execution
    def call(self, addr, regs=None, max_steps=2_000_000):
        """Run the function at addr until it returns to the sentinel address."""
        if regs:
            for k, v in regs.items(): self.set_reg(k, v)
        self.push(self.halt_addr)
        self.eip = addr
        n = 0
        while self.eip != self.halt_addr:
            if self.eip in self.hooks:
                self.hooks[self.eip](self)
                self.eip = self.pop()  # hook acts like a function: return
                continue
            self.step()
            n += 1
            if n > max_steps: raise EmuError('step limit at %x' % self.eip)
        return n

    def step(self):
        a = self.eip
        size, mn, ops, _b = self.decode(a)
        nxt = (a + size) & M32
        if self.trace: print('%08x  %s %s  eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x ebp=%08x' % (a, mn, ops, *[self.r[i] for i in (0, 3, 1, 2, 6, 7, 5)]))
        self.steps += 1
        pre = ''
        if ' ' in mn:  # capstone prints prefixed string ops as one mnemonic, e.g. 'rep movsd'
            pre, mn = mn.split(' ', 1)
        elif mn in ('rep', 'repe', 'repne', 'repz', 'repnz'):
            # capstone prints "rep movsw word ptr es:[edi], word ptr [esi]" with mnemonic 'rep' in some builds
            parts = ops.split(' ', 1)
            pre = mn; mn = parts[0]; ops = parts[1] if len(parts) > 1 else ''
        self.eip = nxt
        fn = getattr(self, 'op_' + mn, None)
        if fn is None:
            raise EmuError('unimplemented %s %s at %x' % (mn, ops, a))
        oplist = [x for x in self.split_ops(ops)] if ops else []
        if pre:
            self.string_op(mn, oplist, pre, nxt)
        else:
            fn(oplist)

    @staticmethod
    def split_ops(s):
        out, depth, cur = [], 0, ''
        for ch in s:
            if ch == '[': depth += 1
            if ch == ']': depth -= 1
            if ch == ',' and depth == 0:
                out.append(cur.strip()); cur = ''
            else:
                cur += ch
        if cur.strip(): out.append(cur.strip())
        return out

    # ---- helpers for ALU
    def alu(self, kind, a, b, size, cin=0):
        m = (1 << size) - 1
        if kind in ('add', 'adc'):
            c = cin if kind == 'adc' else 0
            r = a + b + c
            self.cf = int(r > m)
            self.of = int(((a ^ r) & (b ^ r)) >> (size - 1) & 1)
        elif kind in ('sub', 'sbb', 'cmp'):
            c = cin if kind == 'sbb' else 0
            r = a - b - c
            self.cf = int(a < b + c)
            self.of = int(((a ^ b) & (a ^ r)) >> (size - 1) & 1)
        else:
            raise EmuError(kind)
        self.setszp(r, size)
        return r & m

    def bin2(self, ops, kind):
        d, s = self.parse(ops[0]), self.parse(ops[1])
        size = self.osize(d, s)
        a = self.rd(d, size)
        b = self.rd(s, size)
        if s[0] == 'imm': b = sx(b, 8 if (-128 <= s[1] < 128 and size != 8 and False) else size) & ((1 << size) - 1) if s[1] < 0 else b
        r = self.alu(kind, a, b, size, self.cf)
        if kind != 'cmp': self.wr(d, r, size)

    def op_add(self, o): self.bin2(o, 'add')
    def op_adc(self, o): self.bin2(o, 'adc')
    def op_sub(self, o): self.bin2(o, 'sub')
    def op_sbb(self, o): self.bin2(o, 'sbb')
    def op_cmp(self, o): self.bin2(o, 'cmp')

    def logic(self, o, kind):
        d, s = self.parse(o[0]), self.parse(o[1])
        size = self.osize(d, s)
        a = self.rd(d, size); b = self.rd(s, size)
        if s[0] == 'imm' and s[1] < 0: b = s[1] & ((1 << size) - 1)
        r = {'and': a & b, 'or': a | b, 'xor': a ^ b, 'test': a & b}[kind]
        self.cf = 0; self.of = 0
        self.setszp(r, size)
        if kind != 'test': self.wr(d, r, size)

    def op_and(self, o): self.logic(o, 'and')
    def op_or(self, o): self.logic(o, 'or')
    def op_xor(self, o): self.logic(o, 'xor')
    def op_test(self, o): self.logic(o, 'test')

    def op_mov(self, o):
        d, s = self.parse(o[0]), self.parse(o[1])
        size = self.osize(d, s)
        v = self.rd(s, size)
        if s[0] == 'imm' and s[1] < 0: v = s[1] & ((1 << size) - 1)
        self.wr(d, v, size)

    def op_movzx(self, o):
        d, s = self.parse(o[0]), self.parse(o[1])
        self.wr(d, self.rd(s, s[2] or 8), d[2])

    def op_movsx(self, o):
        d, s = self.parse(o[0]), self.parse(o[1])
        ss = s[2] or 8
        self.wr(d, sx(self.rd(s, ss), ss) & ((1 << d[2]) - 1), d[2])

    def op_lea(self, o):
        d = self.parse(o[0]); m = self.MEMRE.match(o[1].strip())
        self.wr(d, self.ea(m.group(3)), d[2])

    def op_xchg(self, o):
        a, b = self.parse(o[0]), self.parse(o[1])
        size = self.osize(a, b); x, y = self.rd(a, size), self.rd(b, size)
        self.wr(a, y, size); self.wr(b, x, size)

    def op_inc(self, o):
        d = self.parse(o[0]); size = self.osize(d); cf = self.cf
        r = self.alu('add', self.rd(d, size), 1, size); self.cf = cf; self.wr(d, r, size)

    def op_dec(self, o):
        d = self.parse(o[0]); size = self.osize(d); cf = self.cf
        r = self.alu('sub', self.rd(d, size), 1, size); self.cf = cf; self.wr(d, r, size)

    def op_neg(self, o):
        d = self.parse(o[0]); size = self.osize(d)
        r = self.alu('sub', 0, self.rd(d, size), size); self.wr(d, r, size)

    def op_not(self, o):
        d = self.parse(o[0]); size = self.osize(d)
        self.wr(d, ~self.rd(d, size) & ((1 << size) - 1), size)

    # shifts
    def shift(self, o, kind):
        d = self.parse(o[0]); size = self.osize(d)
        cnt = self.rd(self.parse(o[1]), 8) if len(o) > 1 else 1
        cnt &= 0x1F
        v = self.rd(d, size); m = (1 << size) - 1
        if cnt == 0: return
        if kind in ('shl', 'sal'):
            r = (v << cnt) & m; self.cf = (v >> (size - cnt)) & 1 if cnt <= size else 0
            self.of = ((r >> (size - 1)) & 1) ^ self.cf
        elif kind == 'shr':
            r = v >> cnt; self.cf = (v >> (cnt - 1)) & 1; self.of = (v >> (size - 1)) & 1
        elif kind == 'sar':
            sv = sx(v, size); r = (sv >> cnt) & m; self.cf = (sv >> (cnt - 1)) & 1; self.of = 0
        elif kind == 'rcl':
            r = v
            for _ in range(cnt):
                c = (r >> (size - 1)) & 1; r = ((r << 1) & m) | self.cf; self.cf = c
            self.wr(d, r, size); return
        elif kind == 'rcr':
            r = v
            for _ in range(cnt):
                c = r & 1; r = (r >> 1) | (self.cf << (size - 1)); self.cf = c
            self.wr(d, r, size); return
        elif kind == 'rol':
            cnt %= size; r = ((v << cnt) | (v >> (size - cnt))) & m if cnt else v; self.cf = r & 1; self.wr(d, r, size); return
        elif kind == 'ror':
            cnt %= size; r = ((v >> cnt) | (v << (size - cnt))) & m if cnt else v; self.cf = (r >> (size - 1)) & 1; self.wr(d, r, size); return
        self.setszp(r, size); self.wr(d, r, size)

    def op_shl(self, o): self.shift(o, 'shl')
    def op_sal(self, o): self.shift(o, 'shl')
    def op_shr(self, o): self.shift(o, 'shr')
    def op_sar(self, o): self.shift(o, 'sar')
    def op_rcl(self, o): self.shift(o, 'rcl')
    def op_rcr(self, o): self.shift(o, 'rcr')
    def op_rol(self, o): self.shift(o, 'rol')
    def op_ror(self, o): self.shift(o, 'ror')

    def dshift(self, o, left):
        d, s = self.parse(o[0]), self.parse(o[1]); size = self.osize(d, s)
        cnt = self.rd(self.parse(o[2]), 8) & 0x1F
        if cnt == 0: return
        m = (1 << size) - 1
        a, b = self.rd(d, size), self.rd(s, size)
        if left:
            full = (a << size) | b
            r = ((full << cnt) >> size) & m
            self.cf = (a >> (size - cnt)) & 1
        else:
            full = (b << size) | a
            r = (full >> cnt) & m
            self.cf = (a >> (cnt - 1)) & 1
        self.setszp(r, size); self.of = 0
        self.wr(d, r, size)

    def op_shld(self, o): self.dshift(o, True)
    def op_shrd(self, o): self.dshift(o, False)

    # multiply / divide
    def op_mul(self, o):
        s = self.parse(o[0]); size = self.osize(s)
        v = self.rd(s, size)
        if size == 8:
            r = self.get_reg('al') * v; self.set_reg('ax', r); hi = r >> 8
        elif size == 16:
            r = self.get_reg('ax') * v; self.set_reg('ax', r); self.set_reg('dx', r >> 16); hi = r >> 16
        else:
            r = self.r[0] * v; self.r[0] = r & M32; self.r[2] = (r >> 32) & M32; hi = r >> 32
        self.cf = self.of = int(hi != 0)

    def op_imul(self, o):
        if len(o) == 1:
            s = self.parse(o[0]); size = self.osize(s); v = sx(self.rd(s, size), size)
            if size == 8:
                r = sx(self.get_reg('al'), 8) * v; self.set_reg('ax', r); ok = -128 <= r < 128
            elif size == 16:
                r = sx(self.get_reg('ax'), 16) * v; self.set_reg('ax', r); self.set_reg('dx', r >> 16); ok = -32768 <= r < 32768
            else:
                r = sx(self.r[0], 32) * v; self.r[0] = r & M32; self.r[2] = (r >> 32) & M32; ok = -(1 << 31) <= r < (1 << 31)
            self.cf = self.of = int(not ok)
            return
        if len(o) == 2:
            d, s = self.parse(o[0]), self.parse(o[1]); a = d; b = s
        else:
            d, a, b = self.parse(o[0]), self.parse(o[1]), self.parse(o[2])
        size = self.osize(d)
        x = sx(self.rd(a, size), size)
        y = b[1] if b[0] == 'imm' else sx(self.rd(b, size), size)
        r = x * y
        self.wr(d, r & ((1 << size) - 1), size)
        self.cf = self.of = int(sx(r, size) != r)

    def op_div(self, o):
        s = self.parse(o[0]); size = self.osize(s); v = self.rd(s, size)
        if v == 0: raise EmuError('divide by zero at %x' % (self.eip))
        if size == 8:
            n = self.get_reg('ax'); q, r = divmod(n, v); self.set_reg('al', q); self.set_reg('ah', r)
        elif size == 16:
            n = (self.get_reg('dx') << 16) | self.get_reg('ax'); q, r = divmod(n, v); self.set_reg('ax', q); self.set_reg('dx', r)
        else:
            n = (self.r[2] << 32) | self.r[0]; q, r = divmod(n, v)
            if q > M32: raise EmuError('div overflow')
            self.r[0] = q & M32; self.r[2] = r & M32

    def op_idiv(self, o):
        s = self.parse(o[0]); size = self.osize(s); v = sx(self.rd(s, size), size)
        if v == 0: raise EmuError('idiv by zero at %x' % (self.eip))
        def tdiv(n, d):
            q = abs(n) // abs(d)
            if (n < 0) != (d < 0): q = -q
            return q, n - q * d
        if size == 16:
            n = sx((self.get_reg('dx') << 16) | self.get_reg('ax'), 32); q, r = tdiv(n, v)
            self.set_reg('ax', q); self.set_reg('dx', r)
        elif size == 32:
            n = sx((self.r[2] << 32) | self.r[0], 64); q, r = tdiv(n, v)
            if not (-(1 << 31) <= q < (1 << 31)): raise EmuError('idiv overflow at %x' % self.eip)
            self.r[0] = q & M32; self.r[2] = r & M32
        else:
            n = sx(self.get_reg('ax'), 16); q, r = tdiv(n, v); self.set_reg('al', q); self.set_reg('ah', r)

    def op_cwde(self, o): self.r[0] = sx(self.r[0] & 0xFFFF, 16) & M32
    def op_cbw(self, o): self.set_reg('ax', sx(self.get_reg('al'), 8))
    def op_cdq(self, o): self.r[2] = M32 if self.r[0] & 0x80000000 else 0
    def op_cwd(self, o): self.set_reg('dx', 0xFFFF if self.get_reg('ax') & 0x8000 else 0)

    def op_bsr(self, o):
        d, s = self.parse(o[0]), self.parse(o[1]); size = self.osize(d); v = self.rd(s, size)
        if v == 0: self.zf = 1
        else: self.zf = 0; self.wr(d, v.bit_length() - 1, size)

    # control flow
    def target(self, t):
        t = t.strip()
        if re.match(r'^0x[0-9a-f]+$', t): return int(t, 16)
        p = self.parse(t); return self.rd(p, 32)

    def op_call(self, o): self.push(self.eip); self.eip = self.target(o[0])
    def op_jmp(self, o): self.eip = self.target(o[0])
    def op_ret(self, o):
        self.eip = self.pop()
        if o: self.r[4] = (self.r[4] + int(o[0], 0)) & M32

    def op_push(self, o):
        s = self.parse(o[0]); size = 16 if (s[2] == 16) else 32
        v = self.rd(s, size)
        if s[0] == 'imm': v &= M32
        self.push(v, size)

    def op_pop(self, o):
        d = self.parse(o[0]); size = 16 if d[2] == 16 else 32
        self.wr(d, self.pop(size), size)

    def op_pushal(self, o):
        sp = self.r[4]
        for i in (0, 1, 2, 3):
            self.push(self.r[i])
        self.push(sp)
        for i in (5, 6, 7):
            self.push(self.r[i])

    def op_popal(self, o):
        for i in (7, 6, 5): self.r[i] = self.pop()
        self.pop()
        for i in (3, 2, 1, 0): self.r[i] = self.pop()

    def op_pushfd(self, o): self.push(self.flags_word())
    def op_popfd(self, o): self.set_flags_word(self.pop())
    def op_pushf(self, o): self.push(self.flags_word() & 0xFFFF, 16)
    def op_popf(self, o): self.set_flags_word(self.pop(16))
    def op_leave(self, o): self.r[4] = self.r[5]; self.r[5] = self.pop()
    def op_nop(self, o): pass
    def op_clc(self, o): self.cf = 0
    def op_stc(self, o): self.cf = 1
    def op_cmc(self, o): self.cf ^= 1
    def op_cld(self, o): self.df = 0
    def op_std(self, o): self.df = 1
    def op_sahf(self, o): self.set_flags_word((self.flags_word() & ~0xFF) | self.get_reg('ah'))
    def op_lahf(self, o): self.set_reg('ah', self.flags_word() & 0xFF)

    def op_jecxz(self, o):
        if self.r[1] == 0: self.eip = self.target(o[0])

    def op_jcxz(self, o):
        if self.r[1] & 0xFFFF == 0: self.eip = self.target(o[0])

    def op_loop(self, o):
        self.r[1] = (self.r[1] - 1) & M32
        if self.r[1]: self.eip = self.target(o[0])

    def op_loope(self, o):
        self.r[1] = (self.r[1] - 1) & M32
        if self.r[1] and self.zf: self.eip = self.target(o[0])

    def op_loopne(self, o):
        self.r[1] = (self.r[1] - 1) & M32
        if self.r[1] and not self.zf: self.eip = self.target(o[0])

    def __getattr__(self, name):
        m = re.match(r'^op_j(.+)$', name)
        if m and m.group(1) not in ('mp', 'cxz', 'ecxz'):
            cc = m.group(1)
            def f(o, cc=cc):
                if self.cond(cc): self.eip = self.target(o[0])
            return f
        m = re.match(r'^op_set(.+)$', name)
        if m:
            cc = m.group(1)
            def g(o, cc=cc): self.wr(self.parse(o[0]), int(self.cond(cc)), 8)
            return g
        m = re.match(r'^op_cmov(.+)$', name)
        if m:
            cc = m.group(1)
            def h(o, cc=cc):
                if self.cond(cc): self.op_mov(o)
            return h
        raise AttributeError(name)

    # string ops
    def string_op(self, mn, ops, pre, nxt):
        # element size from mnemonic suffix
        sz = {'movsb': 8, 'movsw': 16, 'movsd': 32, 'stosb': 8, 'stosw': 16, 'stosd': 32, 'scasb': 8, 'scasw': 16, 'scasd': 32,
              'lodsb': 8, 'lodsw': 16, 'lodsd': 32, 'cmpsb': 8, 'cmpsw': 16, 'cmpsd': 32}[mn]
        step = (sz // 8) * (-1 if self.df else 1)
        esi, edi = 6, 7
        while self.r[1] != 0:
            if mn.startswith('movs'):
                v = self.mem.r8(self.r[esi]) if sz == 8 else self.mem.r16(self.r[esi]) if sz == 16 else self.mem.r32(self.r[esi])
                (self.mem.w8 if sz == 8 else self.mem.w16 if sz == 16 else self.mem.w32)(self.r[edi], v)
                self.r[esi] = (self.r[esi] + step) & M32; self.r[edi] = (self.r[edi] + step) & M32
            elif mn.startswith('stos'):
                v = self.r[0] & ((1 << sz) - 1)
                (self.mem.w8 if sz == 8 else self.mem.w16 if sz == 16 else self.mem.w32)(self.r[edi], v)
                self.r[edi] = (self.r[edi] + step) & M32
            elif mn.startswith('lods'):
                v = self.mem.r8(self.r[esi]) if sz == 8 else self.mem.r16(self.r[esi]) if sz == 16 else self.mem.r32(self.r[esi])
                self.r[0] = (self.r[0] & ~((1 << sz) - 1)) | v
                self.r[esi] = (self.r[esi] + step) & M32
            elif mn.startswith('scas'):
                v = self.mem.r8(self.r[edi]) if sz == 8 else self.mem.r16(self.r[edi]) if sz == 16 else self.mem.r32(self.r[edi])
                self.alu('cmp', self.r[0] & ((1 << sz) - 1), v, sz)
                self.r[edi] = (self.r[edi] + step) & M32
            elif mn.startswith('cmps'):
                a = self.mem.r8(self.r[esi]) if sz == 8 else self.mem.r16(self.r[esi]) if sz == 16 else self.mem.r32(self.r[esi])
                b = self.mem.r8(self.r[edi]) if sz == 8 else self.mem.r16(self.r[edi]) if sz == 16 else self.mem.r32(self.r[edi])
                self.alu('cmp', a, b, sz)
                self.r[esi] = (self.r[esi] + step) & M32; self.r[edi] = (self.r[edi] + step) & M32
            self.r[1] = (self.r[1] - 1) & M32
            if mn.startswith(('scas', 'cmps')):
                if pre in ('repe', 'repz') and not self.zf: break
                if pre in ('repne', 'repnz') and self.zf: break
        # (a plain `rep` with count 0 does nothing)

    def op_movsb(self, o): self.string_op('movsb', o, 'rep', 0) if False else self._single('movsb')
    def op_movsw(self, o): self._single('movsw')
    def op_movsd(self, o): self._single('movsd')
    def op_stosb(self, o): self._single('stosb')
    def op_stosw(self, o): self._single('stosw')
    def op_stosd(self, o): self._single('stosd')
    def op_lodsb(self, o): self._single('lodsb')
    def op_lodsw(self, o): self._single('lodsw')
    def op_lodsd(self, o): self._single('lodsd')
    def op_scasb(self, o): self._single('scasb')
    def op_scasw(self, o): self._single('scasw')
    def op_scasd(self, o): self._single('scasd')

    def _single(self, mn):
        saved = self.r[1]
        self.r[1] = 1
        self.string_op(mn, [], 'none', 0)
        self.r[1] = saved


if __name__ == '__main__':
    print('emu.py is a library; see emu_tests.py')
