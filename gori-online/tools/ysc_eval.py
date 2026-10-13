# A tiny interpreter for decompiled GTA V script functions (acidlabsdev/gtav-legacy-scripts style C):
# enough to run the "data feed" functions of appinternet (switch tables, if/else, tunable reads,
# helper calls) and record what they send to a scaleform movie.
#
#   ev = Ysc(open('appinternet.c').read(), tunables)        tunables: {"f_123": default, ...}
#   ev.call('func_8954', [rec, 21, 0])                       rec = Recorder() for the movie handle
#
# Unknown natives return 0; unknown globals read 0 (Global_262145.f_N reads the tunable default);
# a call that cannot be evaluated returns 0. Good enough for "not owned, defaults" data.
import re

PCOMP = {'PV_COMP_INVALID': -1, 'PV_COMP_HEAD': 0, 'PV_COMP_BERD': 1, 'PV_COMP_HAIR': 2, 'PV_COMP_UPPR': 3, 'PV_COMP_LOWR': 4,
         'PV_COMP_HAND': 5, 'PV_COMP_FEET': 6, 'PV_COMP_TEEF': 7, 'PV_COMP_ACCS': 8, 'PV_COMP_TASK': 9, 'PV_COMP_DECL': 10,
         'PV_COMP_JBIB': 11, 'PV_COMP_MAX': 12}


def joaat(s):
    h = 0
    for c in s.lower().encode():
        h = (h + c) & 0xffffffff; h = (h + (h << 10)) & 0xffffffff; h ^= h >> 6
    h = (h + (h << 3)) & 0xffffffff; h ^= h >> 11; h = (h + (h << 15)) & 0xffffffff
    return h - (1 << 32) if h >= 1 << 31 else h


class Ret(Exception):
    def __init__(self, v): self.v = v


class Brk(Exception):
    pass


class Recorder:
    """collects BEGIN_SCALEFORM_MOVIE_METHOD ... END calls: [(method, [params])]"""
    def __init__(self): self.calls = []; self.cur = None


class Ysc:
    def __init__(self, src, tunables, overrides=None):
        self.lines = src.split('\n')
        self.funcs = {}
        for i, l in enumerate(self.lines):
            m = re.match(r'^\S.*? (func_\d+)\((.*?)\)\s*//', l)
            if m:
                self.funcs[m.group(1)] = i
        self.tun = tunables
        self.over = overrides or {}
        self.depth = 0
        self.rec = None

    # ---- function bodies
    def body(self, name):
        i = self.funcs[name]
        j = next(k for k in range(i + 1, len(self.lines)) if self.lines[k] == '}')
        head = self.lines[i]
        params = [p.strip().split(' ')[-1].lstrip('*&') for p in re.match(r'^.*?\((.*?)\)\s*//', head).group(1).split(',') if p.strip()]
        return params, [l.strip() for l in self.lines[i + 2:j]]

    def call(self, name, args):
        if name in self.over:
            return self.over[name](*args)
        if name not in self.funcs or self.depth > 40:
            return 0
        params, body = self.body(name)
        env = {p: (args[k] if k < len(args) else 0) for k, p in enumerate(params)}
        self.depth += 1
        try:
            self.run(body, 0, len(body), env)
        except Ret as r:
            return r.v
        except Exception:
            return 0
        finally:
            self.depth -= 1
        return 0

    # ---- statements
    def block_end(self, b, i):
        """b[i] is '{': index of the matching '}'"""
        d = 0
        for k in range(i, len(b)):
            if b[k] == '{': d += 1
            elif b[k] == '}':
                d -= 1
                if d == 0: return k
        return len(b) - 1

    def stmt_end(self, b, i):
        """the statement starting at i (a block, an if chain with its elses, a switch, a loop or one line): index after it"""
        if i >= len(b): return i
        if b[i] == '{': return self.block_end(b, i) + 1
        if re.match(r'(else )?if \(', b[i]):
            e = self.stmt_end(b, self.next_ne(b, i + 1))
            k = self.next_ne(b, e)
            if k < len(b) and (b[k] == 'else' or b[k].startswith('else if')):
                if b[k] == 'else': return self.stmt_end(b, self.next_ne(b, k + 1))
                return self.stmt_end(b, k)
            return e
        if re.match(r'(while|for) \(', b[i]): return self.stmt_end(b, self.next_ne(b, i + 1))
        if b[i].startswith('switch'): return self.block_end(b, self.next_ne(b, i + 1)) + 1
        return i + 1

    def next_ne(self, b, i):
        while i < len(b) and (not b[i] or b[i].startswith('//')): i += 1
        return i

    def run(self, b, i, end, env):
        while i < end:
            l = b[i]
            if not l or l.startswith('//') or re.match(r'^(var|int|float|BOOL|bool|char\*|Hash|Vector3|ePedComponentType|struct<\d+>|Player|Entity|Ped|Vehicle|const char\*)\s+\w+(\[\d+\])?;', l):
                i += 1; continue
            if l == '{':
                e = self.block_end(b, i); self.run(b, i + 1, e, env); i = e + 1; continue
            if l.startswith('case ') or l.startswith('default:'):
                i += 1; continue
            if l == 'break;':
                raise Brk()
            if l.startswith('goto '):
                raise Brk()
            if l.startswith('return'):
                m = re.match(r'return\s*(.*);$', l)
                raise Ret(self.ev(m.group(1), env) if m.group(1) else 0)
            m = re.match(r'switch \((.*)\)$', l)
            if m:
                v = self.ev(m.group(1), env)
                s = self.next_ne(b, i + 1); e = self.block_end(b, s)
                start = None; dflt = None; depth = 0
                for k in range(s + 1, e):
                    if b[k] == '{': depth += 1
                    elif b[k] == '}': depth -= 1
                    if depth: continue   # the cases of a nested switch are not ours
                    cm = re.match(r'case (.+):$', b[k])
                    if cm and start is None:
                        try:
                            if self.ev(cm.group(1), env) == v: start = k
                        except Exception: pass
                    if b[k] == 'default:' and dflt is None: dflt = k
                start = start if start is not None else dflt
                if start is not None:
                    try: self.run(b, start, e, env)
                    except Brk: pass
                i = e + 1; continue
            m = re.match(r'if \((.*)\)$', l)
            if m:
                k = i; done = False
                while True:
                    cm = re.match(r'(?:else )?if \((.*)\)$', b[k])
                    s0 = self.next_ne(b, k + 1)
                    e0 = self.stmt_end(b, s0)
                    if cm is None:   # plain else
                        if not done: self.run(b, s0, e0, env)
                        i = e0; break
                    if not done and self.truth(self.ev(cm.group(1), env)):
                        self.run(b, s0, e0, env); done = True
                    nk = self.next_ne(b, e0)
                    if nk < len(b) and (b[nk] == 'else' or b[nk].startswith('else if')):
                        k = nk; continue
                    i = e0; break
                continue
            if l == 'else':
                i = self.stmt_end(b, self.next_ne(b, i + 1)); continue
            if re.match(r'(while|for) \(', l):   # loops are not needed for the feeds we run: skip
                i = self.stmt_end(b, i); continue
            self.exec_simple(l, env)
            i += 1

    def truth(self, v):
        return bool(v)

    def exec_simple(self, l, env):
        l = l.rstrip(';')
        m = re.match(r'TEXT_LABEL_ASSIGN_STRING\(&?\(?([\w.\[\]]+)\)?, (.*), \d+\)$', l)
        if m:
            env[m.group(1)] = self.ev(m.group(2), env); return
        m = re.match(r'TEXT_LABEL_APPEND_INT\(&?\(?([\w.\[\]]+)\)?, (.*), \d+\)$', l)
        if m:
            env[m.group(1)] = str(env.get(m.group(1), '')) + str(int(self.ev(m.group(2), env))); return
        m = re.match(r'TEXT_LABEL_APPEND_STRING\(&?\(?([\w.\[\]]+)\)?, (.*), \d+\)$', l)
        if m:
            env[m.group(1)] = str(env.get(m.group(1), '')) + str(self.ev(m.group(2), env)); return
        m = re.match(r'^([\w.\[\]]+) = (.*)$', l)
        if m and not m.group(1).startswith('Global_') and '::' not in m.group(1):
            env[m.group(1)] = self.ev(m.group(2), env); return
        m = re.match(r'^\*?([\w]+) = (.*)$', l)
        if m and not l.startswith('Global_'):
            env[m.group(1)] = self.ev(m.group(2), env); return
        # scaleform recording
        m = re.match(r'GRAPHICS::BEGIN_SCALEFORM_MOVIE_METHOD\((.*?), "(\w+)"\)$', l)
        if m and self.rec is not None:
            self.rec.cur = (m.group(2), []); return
        m = re.match(r'GRAPHICS::END_SCALEFORM_MOVIE_METHOD\(\)$', l)
        if m and self.rec is not None and self.rec.cur:
            self.rec.calls.append(self.rec.cur); self.rec.cur = None; return
        m = re.match(r'GRAPHICS::SCALEFORM_MOVIE_METHOD_ADD_PARAM_(INT|FLOAT|BOOL|PLAYER_NAME_STRING|LITERAL_STRING|TEXTURE_NAME_STRING)\((.*)\)$', l)
        if m and self.rec is not None and self.rec.cur:
            v = self.ev(m.group(2), env)
            if m.group(1) == 'INT': v = int(v) if not isinstance(v, (tuple, str)) else v
            if m.group(1) == 'BOOL': v = bool(v)
            # a vector passed where a float goes is its x; the recorder keeps the whole vector (the caller decides)
            self.rec.cur[1].append((m.group(1), v)); return
        m = re.match(r'(func_\d+)\((.*)\)$', l)
        if m:
            self.call_expr(m.group(1), m.group(2), env); return
        # anything else (natives, global writes): ignored

    # ---- expressions
    def split_args(self, s):
        out, d, cur, q = [], 0, '', False
        for c in s:
            if c == '"': q = not q
            if not q and c in '([{': d += 1
            if not q and c in ')]}': d -= 1
            if c == ',' and d == 0 and not q: out.append(cur); cur = ''
            else: cur += c
        if cur.strip(): out.append(cur)
        return [a.strip() for a in out]

    def call_expr(self, name, argstr, env):
        args = [self.ev(a, env) for a in self.split_args(argstr)]
        if name == 'func_20' and self.rec is not None and self.rec.cur:   # label text component
            self.rec.cur[1].append(('LABEL', args[0] if args else '')); return 0
        if name == 'func_8883' and self.rec is not None and self.rec.cur:
            self.rec.cur[1].append(('PLAYER_NAME_STRING', args[0] if args else '')); return 0
        return self.call(name, args)

    def ev(self, e, env):
        e = e.strip()
        if e.startswith('{') and e.endswith('}'): e = e[1:-1].strip()
        e = re.sub(r'/\*.*?\*/', '', e).strip()
        if e.startswith('&') or e.startswith('*'): e = e[1:]   # address-of / dereference: the value itself
        if e.startswith('(') and e.endswith(')') and self.balanced(e[1:-1]): return self.ev(e[1:-1], env)
        if re.match(r'^"[^"]*"$', e): return e[1:-1]
        if re.match(r'^-?\d+$', e): return int(e)
        if re.match(r'^-?[\d.]+f$', e): return float(e[:-1])
        if e in ('true',): return 1
        if e in ('false',): return 0
        if e in PCOMP: return PCOMP[e]
        m = re.match(r'^joaat\("(.*)"\)$', e)
        if m: return joaat(m.group(1))
        # vector literal "a, b, c"
        parts = self.split_args(e)
        if len(parts) > 1: return tuple(self.ev(p, env) for p in parts)
        # binary operators, lowest precedence first
        for ops in (['||'], ['&&'], ['==', '!='], ['<=', '>=', '<', '>'], ['+', '-'], ['*', '/', '%']):
            pos = self.find_op(e, ops)
            if pos:
                k, op = pos
                a, b = self.ev(e[:k], env), self.ev(e[k + len(op):], env)
                if op == '||': return 1 if (a or b) else 0
                if op == '&&': return 1 if (a and b) else 0
                if op == '==': return 1 if a == b else 0
                if op == '!=': return 1 if a != b else 0
                try:
                    return {'<=': lambda: a <= b, '>=': lambda: a >= b, '<': lambda: a < b, '>': lambda: a > b, '+': lambda: a + b,
                            '-': lambda: a - b, '*': lambda: a * b, '/': lambda: a / b if b else 0, '%': lambda: a % b if b else 0}[op]()
                except Exception:
                    return 0
        if e.startswith('!'): return 0 if self.ev(e[1:], env) else 1
        if e.startswith('-') and not e[1:2].isdigit(): return -self.ev(e[1:], env)
        m = re.match(r'^BUILTIN::(CEIL|TO_FLOAT|FLOOR|ROUND)\((.*)\)$', e)
        if m:
            v = self.ev(m.group(2), env)
            import math
            return {'CEIL': lambda: math.ceil(v), 'TO_FLOAT': lambda: float(v), 'FLOOR': lambda: math.floor(v), 'ROUND': lambda: round(v)}[m.group(1)]()
        m = re.match(r'^(func_\d+)\((.*)\)$', e)
        if m and self.balanced(m.group(2)): return self.call_expr(m.group(1), m.group(2), env)
        m = re.match(r'^\*?Global_262145\.(f_\d+(?:\[\d+\])?)$', e)   # tunables (also read through a pointer)
        if m: return self.tun.get(m.group(1), 0)
        if e.startswith('Global_') or '::' in e: return 0
        m = re.match(r'^(\w+)\.f_(\d+)$', e)   # vector component
        if m:
            v = env.get(m.group(1), 0)
            if isinstance(v, tuple): return v[int(m.group(2))] if int(m.group(2)) < len(v) else 0
            return env.get(e, 0)
        if e in env: return env[e]
        m = re.match(r'^\*?(\w+)$', e)
        if m: return env.get(m.group(1), 0)
        return 0

    def balanced(self, s):
        d = 0; q = False
        for c in s:
            if c == '"': q = not q
            if q: continue
            if c in '([{': d += 1
            if c in ')]}':
                d -= 1
                if d < 0: return False
        return d == 0

    def find_op(self, e, ops):
        d = 0; q = False; best = None
        i = 0
        while i < len(e):
            c = e[i]
            if c == '"': q = not q
            if not q:
                if c in '([{': d += 1
                elif c in ')]}': d -= 1
                elif d == 0:
                    for op in ops:
                        if e.startswith(op, i):
                            if op in ('<', '>') and e[i:i + 2] in ('<=', '>=', '<<', '>>'): continue
                            if op in ('-', '+') and (i == 0 or e[i - 1] in '(*/%+-<>=!&|,' or e[i - 1:i].isspace() and (i < 2 or e[i - 2] in '(*/%+-<>=!&|,')):
                                continue
                            if op == '!' : continue
                            if op == '=' : continue
                            best = (i, op)   # rightmost of the lowest precedence: left associativity
                            break
            i += 1
        return best
