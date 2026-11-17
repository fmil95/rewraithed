#!/usr/bin/env python3
"""
hasm2c: static recompiler for the game's hand-written MIPS (HASM_1.s, HASM_2.s).

Turns selected functions of the R3000 assembly into C that runs the same
instruction stream on an emulated register file:

  * registers are a shared array, so the private register conventions between
    the hand-written routines (e.g. the primitive pointer kept in $t0) work
    unchanged; calls between translated routines are plain C calls;
  * branch and jump delay slots, load delay slots, R3000 DIV semantics and
    computed jumps through tables of code labels are reproduced;
  * GTE instructions go to the PsyCross software GTE (MTC2/MFC2/CTC2/CFC2,
    doCOP2), using the encodings from soul-re's include/gte_macros.s;
  * addresses are real 32-bit pointers (the port is a 32-bit build) and the
    scratchpad is mapped at 0x1F800000, so absolute scratchpad addresses work.

Usage:
  hasm2c.py --gte include/gte_macros.s --out hasm_recomp.c \
            --entry NAME:ARGS[:ret|void[:CNAME]] ... --func NAME ... --extern NAME:ARGS[:CNAME] ... \
            HASM_1.s HASM_2.s
"""
import argparse
import re
import sys

REGS = ['zero', 'at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3',
        't0', 't1', 't2', 't3', 't4', 't5', 't6', 't7',
        's0', 's1', 's2', 's3', 's4', 's5', 's6', 's7',
        't8', 't9', 'k0', 'k1', 'gp', 'sp', 'fp', 'ra']
REGNUM = {n: i for i, n in enumerate(REGS)}
REGNUM['s8'] = 30

LOADS = {'lw', 'lh', 'lhu', 'lb', 'lbu'}
STORES = {'sw', 'sh', 'sb'}
BRANCHES = {'b', 'beq', 'bne', 'beqz', 'bnez', 'bgez', 'bltz', 'bgtz', 'blez'}
JUMPS = {'j', 'jal', 'jr', 'jalr'}


class Insn:
    def __init__(self, op, args, line, labels):
        self.op = op
        self.args = args
        self.line = line
        self.labels = labels  # labels attached to this instruction


def strip_comment(s):
    s = re.sub(r'/\*.*?\*/', '', s)
    s = s.split('#')[0]
    return s.strip()


def split_args(s):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def parse_functions(paths):
    """Returns {name: [Insn]} for every glabel ... .size block."""
    funcs = {}
    for path in paths:
        cur = None
        pending = []
        body = []
        with open(path, encoding='latin-1') as f:
            lines = f.readlines()
        # splat's library files have several functions per file and no .size
        # directives: there every glabel starts a new function
        sized = any(l.lstrip().startswith('.size') for l in lines)
        for raw in lines:
                line = strip_comment(raw)
                if not line:
                    continue
                m = re.match(r'^glabel\s+(\w+)$', line)
                if m:
                    if cur is not None and not sized:
                        if pending:
                            body.append(Insn('nop', [], '(end)', pending))
                        funcs[cur] = body
                        cur = None
                    if cur is None:
                        cur = m.group(1)
                        body, pending = [], []
                    else:
                        pending.append(m.group(1))
                    continue
                if cur is None:
                    continue
                m = re.match(r'^\.size\s+(\w+)', line)
                if m:
                    if pending:
                        body.append(Insn('nop', [], '(end)', pending))
                    funcs[cur] = body
                    cur = None
                    continue
                m = re.match(r'^([.\w]+):\s*(.*)$', line)
                if m and not line.startswith('.') or (m and m.group(1).startswith('.L')):
                    pending.append(m.group(1))
                    line = m.group(2).strip()
                    if not line:
                        continue
                if line.startswith('.'):
                    continue
                parts = line.split(None, 1)
                op = parts[0]
                args = split_args(parts[1]) if len(parts) > 1 else []
                body.append(Insn(op, args, line, pending))
                pending = []
        if cur is not None:
            # splat's per-function files end without a .size directive
            if pending:
                body.append(Insn('nop', [], '(end)', pending))
            funcs[cur] = body
    return funcs


class Gen:
    def __init__(self, gte, funcs, entries, externs):
        self.gte = gte
        self.funcs = funcs
        self.entries = entries
        self.externs = externs
        self.symbols = set()
        self.code_labels = {}  # global code label (glabel) -> id
        self.out = []

    # --- operands ---------------------------------------------------------
    def reg(self, s):
        s = s.strip()
        if not s.startswith('$'):
            raise ValueError('register expected: ' + s)
        name = s[1:]
        if name.isdigit():
            return int(name)
        return REGNUM[name]

    def R(self, s):
        n = self.reg(s)
        return '0u' if n == 0 else 'R[%d]' % n

    def addr_of(self, sym):
        if sym in self.code_labels:
            return '0x%08Xu' % self.code_labels[sym]
        self.symbols.add(sym)
        return '((u32)(uintptr_t)%s)' % sym

    def imm(self, s):
        """C expression for an immediate (may involve symbols)."""
        s = s.strip()

        def hi(m):
            return '((%s + 0x8000u) >> 16)' % self.addr_of(m.group(1))

        def lo(m):
            return '((u32)(s32)(s16)(%s & 0xFFFF))' % self.addr_of(m.group(1))

        def hi_u(m):
            return '(%s >> 16)' % self.addr_of(m.group(1))

        def lo_u(m):
            return '(%s & 0xFFFF)' % self.addr_of(m.group(1))

        e = re.sub(r'%hi\((\w+)\)', hi, s)
        e = re.sub(r'%lo\((\w+)\)', lo, e)
        e = re.sub(r'\b(\w+)_HI\b', hi_u, e)
        e = re.sub(r'\b(\w+)_LO\b', lo_u, e)
        if re.fullmatch(r'[\s0-9xXa-fA-F()+\-*<>&|~^]+', e):
            return '0x%Xu' % (eval(e) & 0xFFFFFFFF)
        return '(u32)(%s)' % e

    def simm(self, s):
        """Sign-extended 16-bit immediate."""
        e = self.imm(s)
        if re.fullmatch(r'0x[0-9A-F]+u', e):
            v = int(e[:-1], 16) & 0xFFFF
            if v & 0x8000:
                v -= 0x10000
            return '(u32)%d' % v if v >= 0 else '(u32)(%d)' % v
        return '(u32)(s32)(s16)(%s)' % e

    def mem(self, s):
        m = re.fullmatch(r'(.*)\((\$\w+)\)', s.strip())
        off = m.group(1).strip() or '0'
        g = re.fullmatch(r'%gp_rel\(\s*(\w+)\s*(?:([+-])\s*(0x[0-9A-Fa-f]+|\d+))?\s*\)', off)
        if g:
            # small data addressed through $gp: use the symbol's address
            disp = 0
            if g.group(3):
                disp = int(g.group(3), 0) * (-1 if g.group(2) == '-' else 1)
            return '(%s + (u32)(%d))' % (self.addr_of(g.group(1)), disp)
        return '(%s + %s)' % (self.R(m.group(2)), self.simm(off))

    # --- register use (for load delay slots) ---------------------------------
    def reads(self, ins):
        op, a = ins.op, ins.args
        if op in ('nop', 'break', 'j', 'jal', 'mflo', 'mfhi', 'lui'):
            return set()
        if op in LOADS or op == 'lwc2':
            return {self.reg(re.search(r'\((\$\w+)\)', a[1]).group(1))}
        if op in STORES or op in ('lwl', 'lwr', 'swl', 'swr'):
            return {self.reg(a[0]), self.reg(re.search(r'\((\$\w+)\)', a[1]).group(1))}
        if op == 'swc2':
            return {self.reg(re.search(r'\((\$\w+)\)', a[1]).group(1))}
        if op in ('mtc2', 'ctc2'):
            return {self.reg(a[0])}
        if op in ('mfc2', 'cfc2'):
            return set()
        if op in ('jr', 'jalr'):
            return {self.reg(a[0])}
        regs = set()
        start = 1 if op not in BRANCHES and op not in ('mult', 'multu', 'div', 'divu') else 0
        for x in a[start:]:
            if x.startswith('$'):
                regs.add(self.reg(x))
        return regs

    def writes(self, ins):
        op, a = ins.op, ins.args
        if op in STORES or op in BRANCHES or op in ('nop', 'break', 'j', 'jr', 'mtc2', 'ctc2',
                                                      'swc2', 'lwc2', 'mult', 'multu', 'div', 'divu') \
                or op.upper() in self.gte:
            return set()
        if op == 'jal':
            return {2, 3}
        if a and a[0].startswith('$'):
            return {self.reg(a[0])}
        return set()

    # --- instruction translation -------------------------------------------
    def stmt(self, ins):
        """C statement for a non-control-flow instruction."""
        op, a = ins.op, ins.args
        R, imm, simm = self.R, self.imm, self.simm

        def w(dst, expr):
            n = self.reg(dst)
            return '' if n == 0 else 'R[%d] = %s;' % (n, expr)

        if op == 'nop' or op == 'break':
            return ''
        if op.upper() in self.gte:
            word = self.gte[op.upper()](a)
            return 'doCOP2(0x%07X);' % (word & 0x1FFFFFF)
        if op in ('addu', 'add'):
            return w(a[0], '%s + %s' % (R(a[1]), R(a[2])))
        if op in ('subu', 'sub'):
            return w(a[0], '%s - %s' % (R(a[1]), R(a[2])))
        if op in ('addiu', 'addi'):
            g = re.fullmatch(r'%gp_rel\(\s*(\w+)\s*(?:([+-])\s*(0x[0-9A-Fa-f]+|\d+))?\s*\)', a[2].strip())
            if g:
                # address of small data addressed through $gp
                disp = int(g.group(3), 0) * (-1 if g.group(2) == '-' else 1) if g.group(3) else 0
                return w(a[0], '%s + (u32)(%d)' % (self.addr_of(g.group(1)), disp))
            return w(a[0], '%s + %s' % (R(a[1]), simm(a[2])))
        if op in ('neg', 'negu'):
            return w(a[0], '0u - %s' % R(a[1]))
        if op == 'move':
            return w(a[0], R(a[1]))
        if op == 'and':
            return w(a[0], '%s & %s' % (R(a[1]), R(a[2])))
        if op == 'or':
            return w(a[0], '%s | %s' % (R(a[1]), R(a[2])))
        if op == 'xor':
            return w(a[0], '%s ^ %s' % (R(a[1]), R(a[2])))
        if op == 'nor':
            return w(a[0], '~(%s | %s)' % (R(a[1]), R(a[2])))
        if op == 'andi':
            return w(a[0], '%s & (%s & 0xFFFF)' % (R(a[1]), imm(a[2])))
        if op == 'ori':
            return w(a[0], '%s | (%s & 0xFFFF)' % (R(a[1]), imm(a[2])))
        if op == 'xori':
            return w(a[0], '%s ^ (%s & 0xFFFF)' % (R(a[1]), imm(a[2])))
        if op == 'lui':
            return w(a[0], '(%s & 0xFFFF) << 16' % imm(a[1]))
        if op == 'slt':
            return w(a[0], '(s32)%s < (s32)%s' % (R(a[1]), R(a[2])))
        if op == 'sltu':
            return w(a[0], '%s < %s' % (R(a[1]), R(a[2])))
        if op == 'slti':
            return w(a[0], '(s32)%s < (s32)%s' % (R(a[1]), simm(a[2])))
        if op == 'sltiu':
            return w(a[0], '%s < %s' % (R(a[1]), simm(a[2])))
        if op == 'sll':
            return w(a[0], '%s << %s' % (R(a[1]), int(imm(a[2])[:-1], 16)))
        if op == 'srl':
            return w(a[0], '%s >> %s' % (R(a[1]), int(imm(a[2])[:-1], 16)))
        if op == 'sra':
            return w(a[0], '(u32)((s32)%s >> %s)' % (R(a[1]), int(imm(a[2])[:-1], 16)))
        if op == 'sllv':
            return w(a[0], '%s << (%s & 31)' % (R(a[1]), R(a[2])))
        if op == 'srlv':
            return w(a[0], '%s >> (%s & 31)' % (R(a[1]), R(a[2])))
        if op == 'srav':
            return w(a[0], '(u32)((s32)%s >> (%s & 31))' % (R(a[1]), R(a[2])))
        if op == 'mult':
            return '{ s64 p = (s64)(s32)%s * (s32)%s; LO = (u32)p; HI = (u32)(p >> 32); }' % (R(a[0]), R(a[1]))
        if op == 'multu':
            return '{ u64 p = (u64)%s * %s; LO = (u32)p; HI = (u32)(p >> 32); }' % (R(a[0]), R(a[1]))
        if op == 'div':
            x, y = (a[1], a[2]) if len(a) == 3 else (a[0], a[1])
            return 'hasm_div(%s, %s);' % (R(x), R(y))
        if op == 'divu':
            x, y = (a[1], a[2]) if len(a) == 3 else (a[0], a[1])
            return 'hasm_divu(%s, %s);' % (R(x), R(y))
        if op == 'mflo':
            return w(a[0], 'LO')
        if op == 'mfhi':
            return w(a[0], 'HI')
        if op == 'lw':
            return w(a[0], 'LD32(%s)' % self.mem(a[1]))
        if op == 'lh':
            return w(a[0], '(u32)(s32)LDS16(%s)' % self.mem(a[1]))
        if op == 'lhu':
            return w(a[0], '(u32)LD16(%s)' % self.mem(a[1]))
        if op == 'lb':
            return w(a[0], '(u32)(s32)LDS8(%s)' % self.mem(a[1]))
        if op == 'lbu':
            return w(a[0], '(u32)LD8(%s)' % self.mem(a[1]))
        if op in ('lwl', 'lwr', 'swl', 'swr'):
            # unaligned word access, little-endian R3000
            return 'hasm_%s(%d, %s);' % (op, self.reg(a[0]), self.mem(a[1]))
        if op == 'sw':
            return 'MEM32(%s) = %s;' % (self.mem(a[1]), R(a[0]))
        if op == 'sh':
            return 'MEM16(%s) = (u16)%s;' % (self.mem(a[1]), R(a[0]))
        if op == 'sb':
            return 'MEM8(%s) = (u8)%s;' % (self.mem(a[1]), R(a[0]))
        if op == 'mtc2':
            return 'MTC2(%s, %d);' % (R(a[0]), self.reg(a[1]))
        if op == 'ctc2':
            return 'CTC2(%s, %d);' % (R(a[0]), self.reg(a[1]))
        if op == 'mfc2':
            return w(a[0], 'MFC2(%d)' % self.reg(a[1]))
        if op == 'cfc2':
            return w(a[0], 'CFC2(%d)' % self.reg(a[1]))
        if op == 'lwc2':
            return 'MTC2(LD32(%s), %d);' % (self.mem(a[1]), self.reg(a[0]))
        if op == 'swc2':
            return 'MEM32(%s) = MFC2(%d);' % (self.mem(a[1]), self.reg(a[0]))
        raise ValueError('unsupported instruction: ' + ins.line)

    def cond(self, ins):
        op, a, R = ins.op, ins.args, self.R
        if op == 'b':
            return '1', a[0]
        if op == 'beq':
            return '%s == %s' % (R(a[0]), R(a[1])), a[2]
        if op == 'bne':
            return '%s != %s' % (R(a[0]), R(a[1])), a[2]
        if op == 'beqz':
            return '%s == 0' % R(a[0]), a[1]
        if op == 'bnez':
            return '%s != 0' % R(a[0]), a[1]
        if op == 'bgez':
            return '(s32)%s >= 0' % R(a[0]), a[1]
        if op == 'bltz':
            return '(s32)%s < 0' % R(a[0]), a[1]
        if op == 'bgtz':
            return '(s32)%s > 0' % R(a[0]), a[1]
        if op == 'blez':
            return '(s32)%s <= 0' % R(a[0]), a[1]
        raise ValueError(op)

    def cname(self, label):
        return 'L_' + label.replace('.', '')

    def call(self, target):
        if target in self.funcs_emitted:
            return 'hasm_%s();' % target
        if target in self.externs:
            n, cname = self.externs[target]
            args = ', '.join(['R[%d]' % (4 + i) for i in range(min(n, 4))] +
                             ['MEM32(R[29] + %d)' % (16 + 4 * i) for i in range(4, n)])
            return 'R[2] = (u32)(uintptr_t)%s(%s);' % (cname, args)
        raise ValueError('call to unknown function ' + target)

    def jump_reg(self, reg, labels):
        cases = ' '.join('case 0x%08Xu: goto %s;' % (self.code_labels[l], self.cname(l)) for l in labels)
        return 'switch (%s) { %s default: hasm_bad_jump(%s); return; }' % (self.R(reg), cases, self.R(reg))

    # --- functions -------------------------------------------------------------
    def emit_group(self, names):
        """Routines that branch into each other's code become one C function
        with an entry switch; each routine gets a small stub."""
        out = self.out
        gname = 'hasm_grp_' + names[0]
        out.append('static void %s(int entry)\n{\n    int bc = 0;\n    (void)bc;\n' % gname)
        out.append('    switch (entry) {')
        for k, n in enumerate(names):
            out.append('    case %d: goto E_%s;' % (k, n))
        out.append('    }')
        body = []
        local_globals = []
        for n in names:
            local_globals += [l for ins in self.funcs[n] for l in ins.labels if not l.startswith('.')]
        for n in names:
            out.append('E_%s:;' % n)
            self.emit_body(self.funcs[n], local_globals)
            out.append('    return;')
        out.append('}\n')
        for k, n in enumerate(names):
            out.append('static void hasm_%s(void) { %s(%d); }\n' % (n, gname, k))

    def emit_function(self, name, body):
        out = self.out
        local_globals = [l for ins in body for l in ins.labels if not l.startswith('.')]
        out.append('static void hasm_%s(void)\n{\n    int bc = 0;\n    (void)bc;\n' % name)
        self.emit_body(body, local_globals)
        out.append('}\n')

    def emit_body(self, body, local_globals):
        out = self.out
        i = 0
        n = len(body)
        while i < n:
            ins = body[i]
            for l in ins.labels:
                out.append('%s:;' % self.cname(l))
            op = ins.op
            if op in BRANCHES or op in JUMPS:
                delay = body[i + 1] if i + 1 < n else Insn('nop', [], 'nop', [])
                dstmt = self.stmt(delay)
                dlabels = delay.labels
                if op in BRANCHES:
                    c, target = self.cond(ins)
                    if op == 'bgez' and ins.args[0] == '$zero':
                        c = '1'
                    goto = 'goto %s;' % self.cname(target)
                    if dlabels:
                        tmp = self.cname(dlabels[0]) + '_x'
                        out.append('    bc = %s; goto %s;' % (c, tmp))
                        for l in dlabels:
                            out.append('%s:; bc = 0;' % self.cname(l))
                        out.append('%s:; %s if (bc) %s' % (tmp, dstmt, goto))
                    else:
                        out.append('    bc = %s; %s if (bc) %s' % (c, dstmt, goto))
                elif op == 'j':
                    out.append('    %s goto %s;' % (dstmt, self.cname(ins.args[0])))
                elif op == 'jal':
                    out.append('    %s %s' % (dstmt, self.call(ins.args[0])))
                elif op == 'jr':
                    if ins.args[0] == '$ra':
                        out.append('    %s return;' % dstmt)
                    else:
                        out.append('    { u32 jt = %s; %s R[%d] = jt; %s }' % (
                            self.R(ins.args[0]), dstmt, self.reg(ins.args[0]),
                            self.jump_reg(ins.args[0], local_globals)))
                else:
                    raise ValueError('unsupported jump ' + ins.line)
                if dlabels and op not in BRANCHES:
                    # code elsewhere jumps straight into this delay slot: it
                    # runs the slot instruction and falls through
                    for l in dlabels:
                        out.append('%s:;' % self.cname(l))
                    if dstmt:
                        out.append('    ' + dstmt)
                i += 2
                continue

            # load delay slot: the loaded value is not visible to the next
            # instruction on the R3000
            if (op in LOADS or op in ('mfc2', 'cfc2')) and i + 1 < n:
                nxt = body[i + 1]
                dst = self.reg(ins.args[0])
                if dst != 0 and not nxt.labels and nxt.op not in BRANCHES and nxt.op not in JUMPS \
                        and dst in self.reads(nxt):
                    s = self.stmt(ins).replace('R[%d] = ' % dst, 'u32 ld = ', 1)
                    out.append('    { %s %s R[%d] = ld; }' % (s, self.stmt(nxt), dst))
                    i += 2
                    continue
            s = self.stmt(ins)
            if s:
                out.append('    ' + s)
            i += 1

    def groups(self, names):
        """Union routines whose branches or jumps target another's labels."""
        owner = {}
        for n in names:
            for ins in self.funcs[n]:
                for l in ins.labels:
                    owner[l] = n
        parent = {n: n for n in names}

        def find(x):
            while parent[x] != x:
                parent[x] = parent[parent[x]]
                x = parent[x]
            return x

        for n in names:
            for ins in self.funcs[n]:
                target = None
                if ins.op in BRANCHES:
                    target = self.cond(ins)[1]
                elif ins.op == 'j':
                    target = ins.args[0]
                if target is not None:
                    if target not in owner:
                        raise ValueError('branch to unknown label %s in %s' % (target, n))
                    parent[find(n)] = find(owner[target])
        out = {}
        for n in names:
            out.setdefault(find(n), []).append(n)
        return list(out.values())

    def generate(self, names):
        self.funcs_emitted = set(names)
        next_id = 0xC0DE0000
        for name in names:
            for ins in self.funcs[name]:
                for l in ins.labels:
                    if not l.startswith('.'):
                        self.code_labels[l] = next_id
                        next_id += 4
        for group in self.groups(names):
            if len(group) == 1:
                self.emit_function(group[0], self.funcs[group[0]])
            else:
                self.emit_group(group)
        body = self.out
        head = [HEADER]
        for s in sorted(self.symbols):
            if s not in self.externs and s not in self.funcs_emitted:
                head.append('extern char %s[];' % s)
        for e, (n, cname) in sorted(self.externs.items()):
            head.append('extern int %s();' % cname)
        for name in names:
            head.append('static void hasm_%s(void);' % name)
        head.append('')
        tail = []
        for name, (nargs, ret, cname) in self.entries.items():
            params = ', '.join('u32 a%d' % i for i in range(nargs)) or 'void'
            sets = ''.join('    R[%d] = a%d;\n' % (4 + i, i) for i in range(min(nargs, 4)))
            stack = ''.join('    MEM32(R[29] + %d) = a%d;\n' % (16 + 4 * (i - 4), i) for i in range(4, nargs))
            tail.append('%s %s(%s)\n{\n    u32 sp = R[29];\n    if (hasm_depth++ == 0)\n        R[29] = (u32)(uintptr_t)(hasm_stack + sizeof(hasm_stack) - 64);\n    else\n        R[29] -= 64;\n%s%s    hasm_%s();\n    R[29] = sp;\n    hasm_depth--;\n%s}\n' % (
                'u32' if ret else 'void', cname, params, sets, stack, name,
                '    return R[2];\n' if ret else ''))
        return '\n'.join(head) + '\n' + '\n'.join(body) + '\n' + '\n'.join(tail)


HEADER = r'''/* Generated by tools/hasm2c.py from soul-re's hand-written MIPS. Do not edit. */
#include <stdint.h>
#include <stdio.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

extern void MTC2(unsigned int value, int reg);
extern void CTC2(unsigned int value, int reg);
extern unsigned int MFC2(int reg);
extern unsigned int CFC2(int reg);
extern int doCOP2(int op);

#define MEM32(a)  (*(u32 *)(uintptr_t)(a))
#define MEM16(a)  (*(u16 *)(uintptr_t)(a))
#define MEMS16(a) (*(s16 *)(uintptr_t)(a))
#define MEM8(a)   (*(u8 *)(uintptr_t)(a))
#define MEMS8(a)  (*(s8 *)(uintptr_t)(a))

/* Loads. The game reads through NULL in places (e.g. a face without
 * normals); on PSX that is RAM and harmless. Addresses below 64 KB are
 * never valid on the PC side, so they read as zero. */
#define LOWADDR(a) ((u32)(a) < 0x10000u)
#define LD32(a)   (LOWADDR(a) ? 0u : MEM32(a))
#define LD16(a)   (LOWADDR(a) ? (u16)0 : MEM16(a))
#define LDS16(a)  (LOWADDR(a) ? (s16)0 : MEMS16(a))
#define LD8(a)    (LOWADDR(a) ? (u8)0 : MEM8(a))
#define LDS8(a)   (LOWADDR(a) ? (s8)0 : MEMS8(a))

static u32 R[32];
static u32 HI, LO;
static int hasm_depth;
static unsigned char hasm_stack[256 * 1024];

/* R3000 DIV: no trap on division by zero. */
static void hasm_div(u32 x, u32 y)
{
    s32 n = (s32)x, d = (s32)y;
    if (d == 0) { LO = (n >= 0) ? 0xFFFFFFFFu : 1u; HI = (u32)n; }
    else if (n == (s32)0x80000000 && d == -1) { LO = 0x80000000u; HI = 0; }
    else { LO = (u32)(n / d); HI = (u32)(n % d); }
}

static void hasm_divu(u32 x, u32 y)
{
    if (y == 0) { LO = 0xFFFFFFFFu; HI = x; }
    else { LO = x / y; HI = x % y; }
}

/* Unaligned word access (little-endian): lwl/lwr merge part of the
 * aligned word into the register, swl/swr store part of the register. */
static void hasm_lwl(int rt, u32 addr)
{
    u32 sh = (addr & 3) * 8, w = LD32(addr & ~3u);
    if (rt) R[rt] = (R[rt] & (0x00FFFFFFu >> sh)) | (w << (24 - sh));
}

static void hasm_lwr(int rt, u32 addr)
{
    u32 sh = (addr & 3) * 8, w = LD32(addr & ~3u);
    if (rt) R[rt] = (R[rt] & ~(0xFFFFFFFFu >> sh)) | (w >> sh);
}

static void hasm_swl(int rt, u32 addr)
{
    u32 sh = (addr & 3) * 8, *p = (u32 *)(uintptr_t)(addr & ~3u);
    *p = (*p & ~(0xFFFFFFFFu >> (24 - sh))) | (R[rt] >> (24 - sh));
}

static void hasm_swr(int rt, u32 addr)
{
    u32 sh = (addr & 3) * 8, *p = (u32 *)(uintptr_t)(addr & ~3u);
    *p = (*p & ~(0xFFFFFFFFu << sh)) | (R[rt] << sh);
}

static void hasm_bad_jump(u32 target)
{
    fprintf(stderr, "[rewraithed] hasm: computed jump to unknown target 0x%08X\n", target);
}
'''


def gte_table(path):
    text = open(path, encoding='latin-1').read()
    table = {}
    for m in re.finditer(r'\.macro\s+(\w+)\s*([^\n]*)\n\s*\.word\s+([^\n]+)\n\s*\.endm', text):
        name, params, expr = m.group(1), m.group(2), m.group(3)
        params = [p.strip() for p in params.split(',') if p.strip()]

        def make(params=params, expr=expr):
            def f(args):
                e = expr
                for p, v in zip(params, args):
                    e = e.replace('\\' + p, '(%s)' % v)
                return eval(e) & 0xFFFFFFFF
            return f
        table[name] = make()
    return table


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--gte', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--entry', action='append', default=[], help='NAME:NARGS[:ret] C-callable entry')
    ap.add_argument('--func', action='append', default=[], help='internal function to translate')
    ap.add_argument('--extern', action='append', default=[], help='NAME:NARGS C function called from asm')
    ap.add_argument('sources', nargs='+')
    args = ap.parse_args()

    funcs = parse_functions(args.sources)
    entries = {}
    for e in args.entry:
        # NAME:ARGS[:ret|void[:CNAME]] - CNAME is the exported C name
        parts = e.split(':')
        ret = len(parts) > 2 and parts[2] == 'ret'
        cname = parts[3] if len(parts) > 3 else parts[0]
        entries[parts[0]] = (int(parts[1]), ret, cname)
    # NAME:ARGS[:CNAME] - CNAME is the C function to call (default NAME)
    externs = {}
    for e in args.extern:
        parts = e.split(':')
        externs[parts[0]] = (int(parts[1]), parts[2] if len(parts) > 2 else parts[0])
    names = list(dict.fromkeys(list(entries) + args.func))
    for n in names:
        if n not in funcs:
            sys.exit('hasm2c: function %s not found' % n)

    gen = Gen(gte_table(args.gte), funcs, entries, externs)
    code = gen.generate(names)
    with open(args.out, 'w', newline='\n') as f:
        f.write(code)


if __name__ == '__main__':
    main()
