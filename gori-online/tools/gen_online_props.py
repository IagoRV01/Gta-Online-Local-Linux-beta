# Generates online_props.h: every GTA Online apartment, house, garage, office and MC clubhouse with
# the exact entrance markers (walk-in and garage), exit points and interior entry points that GTA
# Online itself uses.
#
# Source: the decompiled GTA Online script startup.ysc (acidlabsdev/gtav-legacy-scripts,
# scripts/startup.c), which fills the property table Global_1312440[132] at boot:
#   func_34(property) -> building (f_35), func_53/55/56/57(building) -> entrances f_38[0..2]
#   (f_26 kind 1 walk-in / 2 garage, f_16 door + f_19 rotation + f_22 door template, f_23 stand
#   point, angled area f_0..f_6), f_146[0].f_8/f_11 on-foot exit, f_1742.f_24/f_27 vehicle exit.
#   The blue marker f_12 = func_58(door, template): the template's door->marker offset rotated onto
#   this door (am_mp_property_ext.c draws it with DRAW_MARKER at that point).
#   func_49(property) = interior origin; interior points = template point moved to that origin
#   (func_48). Gori Online's tested interior points give one template point per interior type.
# Names come from func_66 (labels annotated with the English text by the decompiler).
# Prices: gtaboss.gg (high-end, stilt houses, penthouses, clubhouses); medium/low-end apartments and
# garages are spread inside the official tier ranges (exact per-property prices are server tunables).
#
# Cutscenes come from am_mp_property_ext.c: func_6694 (walking in), func_6763 (driving out of the
# garage), func_6773 (walking out of the garage); building cameras f_1922[0..1] from startup.c.
# Interior points (door exit area, entry spot) are template points read at run time with
# GET_BASE_ELEMENT_LOCATION_FROM_METADATA_BLOCK (it works in story mode) and moved like func_48.
#
# Prices: GTA Online's own defaults (startup func_67 -> Global_262145 tunable -> its default in
# tuneables_processing.c). Dynasty 8 listing data (O_DYN): labels from startup func_66/func_65,
# appinternet func_7227 / func_9110 (the two ints of the listing's SET_DATA_SLOT).
# tuneables_processing.c and appinternet.c are read from the folder of startup.c when present.
#
#   python tools/gen_online_props.py <startup.c> <am_mp_property_ext.c> [online_props.h]
import re, sys, math, os

src = open(sys.argv[1], encoding='utf-8', errors='replace').read().split('\n')
ext = open(sys.argv[2], encoding='utf-8', errors='replace').read().split('\n')
out_path = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(__file__), '..', 'online_props.h')
V = r'\{ (-?[\d.]+)f, (-?[\d.]+)f, (-?[\d.]+)f \}'

def body(name, lines=None):
    lines = lines or src
    i = next(k for k, l in enumerate(lines) if re.match(r'^\S.* ' + name + r'\(', l))
    j = next(k for k in range(i + 1, len(lines)) if lines[k] == '}')
    return lines[i:j + 1]

def cases(name, lines=None):
    """yields (labels, line) for each body line, labels = the case run it belongs to (top level only)"""
    labels, opened = [], False
    for l in body(name, lines):
        m = re.match(r'\t\tcase (-?\d+):', l)
        if m:
            if not opened: labels, opened = [], True
            labels.append(int(m.group(1)))
            continue
        if l.strip(): opened = False
        yield labels, l

vec = lambda m, a=1: [float(m.group(a)), float(m.group(a + 1)), float(m.group(a + 2))]
def rotz(v, deg):
    s, c = math.sin(math.radians(deg)), math.cos(math.radians(deg))
    return [v[0] * c - v[1] * s, v[0] * s + v[1] * c, v[2]]
def norm(a):
    while a > 180: a -= 360
    while a <= -180: a += 360
    return a

# ---- door templates (func_58)
T = {}
for labels, l in cases('func_58'):
    for k in labels:
        t = T.setdefault(k, {})
        m = re.search(r'(unk7?)(\.f_3)? = ' + V, l)
        if m: t[m.group(1) + (m.group(2) or '')] = vec(m, 3)

def marker_from_door(door, rot, t):
    d0, r0, m0, mr = T[t]['unk'], T[t]['unk.f_3'], T[t]['unk7'], T[t]['unk7.f_3']
    v = rotz(rotz([m0[k] - d0[k] for k in range(3)], -r0[2]), rot[2])
    h = mr[2] if rot == r0 else norm(mr[2] + norm(rot[2]) - norm(r0[2]))
    return [door[k] + v[k] for k in range(3)], h

# ---- buildings
B = {}
for f in ('func_53', 'func_55', 'func_56', 'func_57'):
    for labels, l in cases(f):
        for bid in labels:
            b = B.setdefault(bid, {'e': {}})
            m = re.search(r'\*uParam0 = ' + V, l)
            if m: b['loc'] = vec(m)
            m = re.search(r'uParam0->f_38\[(\d) /\*27\*/\](\.f_\d+)? = (.*);', l)
            if m:
                e = b['e'].setdefault(int(m.group(1)), {})
                vm = re.match(V, m.group(3))
                if vm: e[m.group(2) or '.f_0'] = vec(vm)
                elif re.match(r'-?[\d.]+f?$', m.group(3)): e[m.group(2) or '.f_0'] = float(m.group(3).rstrip('f'))
            m = re.search(r'uParam0->f_1922\[(\d) /\*9\*/\](\.f_3)? = ' + V, l)
            if m: b.setdefault('cam%s' % m.group(1), {})['rot' if m.group(2) else 'pos'] = vec(m, 3)
            m = re.search(r'uParam0->f_1922\[(\d) /\*9\*/\]\.f_6 = (-?[\d.]+)f', l)
            if m: b.setdefault('cam%s' % m.group(1), {})['fov'] = float(m.group(2))
            for key, pat in (('spawn', r'uParam0->f_146\[0 /\*12\*/\]\.f_8 = ' + V), ('veh', r'uParam0->f_1742\.f_24 = ' + V)):
                m = re.search(pat, l)
                if m: b[key] = vec(m)
            for key, pat in (('spawnH', r'uParam0->f_146\[0 /\*12\*/\]\.f_11 = (-?[\d.]+)f'), ('vehH', r'uParam0->f_1742\.f_27 = (-?[\d.]+)f'),
                             ('type', r'uParam0->f_34 = (\d+);'), ('units', r'uParam0->f_33 = (\d+);')):
                m = re.search(pat, l)
                if m: b[key] = float(m.group(1))

def entrance(e):
    """marker position + heading of one entrance, exactly as GTA Online computes f_12/f_15"""
    if '.f_12' in e: return e['.f_12'], e.get('.f_7', 0.0)
    t = int(e.get('.f_22', -1))
    if t in T:
        door = e.get('.f_16', T[t]['unk'])            # no door given: the template's own door
        rot = e.get('.f_19', T[t]['unk.f_3']) if '.f_16' in e else T[t]['unk.f_3']
        return marker_from_door(door, rot, t)
    return e['.f_23'], e.get('.f_7', 0.0)          # vehicle-only entrance: where the car stops

# ---- properties: building, kind, names
P = {}
for f in ('func_34', 'func_61', 'func_63'):
    for labels, l in cases(f):
        for p in labels:
            m = re.search(r'uParam0->f_35 = (\d+);', l)
            if m: P.setdefault(p, {})['b'] = int(m.group(1))
            m = re.search(r'uParam0->f_34 = (\d+);', l)
            if m: P.setdefault(p, {})['kind'] = int(m.group(1))
            m = re.search(r'func_51\(uParam0, (\d+)\)', l)
            if m: P.setdefault(p, {})['units'] = int(m.group(1))
for labels, l in cases('func_35'):
    m = re.search(r'return (\d+);', l)
    if m:
        for p in labels:
            if p in P and 'b' not in P[p]: P[p]['b'] = int(m.group(1))
for labels, l in cases('func_66'):
    m = re.search(r'str = "(\w+)" /\*(.*?)\*/', l)
    if m:
        for p in labels:
            if p in P: P[p]['name'] = m.group(2).replace('Apt.', 'Apt')

# ---- interiors (func_49 origins, func_50 templates)
ORG, TPL = {}, {}
for labels, l in cases('func_49'):
    for p in labels:
        m = re.search(r'unk = ' + V, l)
        if m: ORG.setdefault(p, {})['pos'] = vec(m)
        m = re.search(r'unk\.f_3 = ' + V, l)
        if m: ORG.setdefault(p, {})['rot'] = float(m.group(3))
for labels, l in cases('func_50'):
    m = re.search(r'return (-?\d+);', l)
    if m:
        for p in labels: TPL[p] = int(m.group(1))

# One known inside point per interior template (Gori Online's interiors, tested in game; mid/low are
# GTA Online's own f_38[0].f_8 from func_36). (property it belongs to, x, y, z, heading)
KNOWN = {1: (38, -35.31, -580.42, 88.71, 70.0), 61: (61, -778.51, 331.32, 210.40, 0.0), 8: (8, 347.3975, -1001.4458, -99.20, 4.881),
         17: (17, 264.5677, -1000.6248, -99.01, 38.4697), 73: (76, -676.13, 588.61, 145.17, 40.0), 77: (77, 120.50, 549.95, 184.10, 180.0)}
# Properties whose interior Gori Online already defines (id in INTERIORS) - kept as is.
GORI_INT = {61: 'hl_eclipse', 62: 'hl_delperro', 63: 'hl_richards', 64: 'hl_tinsel', 65: 'hl_integrity', 38: 'apt_iw30', 34: 'apt_dp7',
            73: 'house1', 74: 'house2', 82: 'house3', 76: 'house4', 75: 'house5', 80: 'house6', 77: 'house7', 81: 'house8',
            84: 'exec1', 85: 'exec2', 83: 'exec3', 87: 'office_lom', 88: 'office_mbw', 89: 'office_arc', 90: 'office_mbt'}
# interiors GTA Online only loads on demand (am_mp_property_int: REQUEST_IPL while you are inside)
IPL_FOR = {79: 'apa_stilt_ch2_09c_int'}
def interior_point(p):
    t = TPL.get(p, 1 if p in range(34, 44) else None)   # 34..43: the original high-end apartments
    if t not in KNOWN or p not in ORG or KNOWN[t][0] not in ORG: return None
    kp, x, y, z, h = KNOWN[t]
    o0, o1 = ORG[kp], ORG[p]
    v = rotz(rotz([x - o0['pos'][0], y - o0['pos'][1], z - o0['pos'][2]], -o0['rot']), o1['rot'])
    return [o1['pos'][k] + v[k] for k in range(3)], norm(h - o0['rot'] + o1['rot']) % 360

PRICE = {  # gtaboss.gg base prices (Unit 14 Popular St: gamerguides.com)
    'Unit 14 Popular St': 77500,
    'Del Perro Heights, Apt 7': 200000, 'Del Perro Heights, Apt 20': 205000, '3 Alta St, Apt 10': 217000, '3 Alta St, Apt 57': 223000,
    '4 Integrity Way, Apt 30': 235000, 'Richards Majestic, Apt 4': 241000, '4 Integrity Way, Apt 35': 247000, 'Richards Majestic, Apt 51': 253000,
    'Tinsel Towers, Apt 45': 270000, 'Tinsel Towers, Apt 29': 286000, 'Weazel Plaza, Apt 26': 304000, 'Weazel Plaza, Apt 70': 319000,
    'Weazel Plaza, Apt 101': 335000, 'Eclipse Towers, Apt 9': 373000, 'Eclipse Towers, Apt 5': 382000, 'Eclipse Towers, Apt 40': 391000,
    'Eclipse Towers, Apt 31': 400000, '2113 Mad Wayne Thunder Drive': 449000, 'Del Perro Heights, Apt 4': 468000, '4 Integrity Way, Apt 28': 476000,
    '3677 Whispymound Drive': 478000, 'Richards Majestic, Apt 2': 484000, 'Tinsel Towers, Apt 42': 492000, 'Eclipse Towers, Apt 3': 500000,
    '2866 Hillcrest Avenue': 525000, '2874 Hillcrest Avenue': 571000, '2117 Milton Road': 608000, '2868 Hillcrest Avenue': 672000,
    '2862 Hillcrest Avenue': 705000, '2045 North Conker Avenue': 727000, '2044 North Conker Avenue': 762000, '3655 Wild Oats Drive': 800000,
    'Eclipse Towers, Penthouse Suite 2': 905000, 'Eclipse Towers, Penthouse Suite 1': 985000, 'Eclipse Towers, Penthouse Suite 3': 1100000,
    'Great Chaparral Clubhouse': 200000, 'Sandy Shores Clubhouse': 210000, 'Grapeseed Clubhouse': 225000, 'Paleto Bay Southwest Clubhouse': 242000,
    'Paleto Bay Northeast Clubhouse': 250000, 'Del Perro Beach Clubhouse': 365000, 'Vespucci Beach Clubhouse': 395000, 'Rancho Clubhouse': 420000,
    'La Mesa Clubhouse': 449000, 'Pillbox Hill Clubhouse': 455000, 'Downtown Vinewood Clubhouse': 472000, 'Hawick Clubhouse': 495000,
    'Lombank West Office': 3100000, 'Maze Bank West Office': 1000000, 'Arcadius Business Center Office': 2250000, 'Maze Bank Tower Office': 4000000,
}
RANGE = {('apt', 2): (80000, 121000), ('apt', 6): (125000, 175000), ('gar', 2): (25000, 35000), ('gar', 6): (62500, 80000), ('gar', 10): (105000, 150000)}

# Paleto Bay has two clubhouses: GTA Online's map names them by position
for p, d in P.items():
    if d.get('name') == 'Paleto Bay Clubhouse': d['name'] = 'Paleto Bay Southwest Clubhouse' if d['b'] == 76 else 'Paleto Bay Northeast Clubhouse'
    if d.get('kind') == 3: d['name'] = d['name'] + ' Office'     # Gori Online's names for the executive offices

# ---- GTA Online's defaults: prices, Dynasty 8 labels and listing ints
here_src = os.path.dirname(os.path.abspath(sys.argv[1]))
def sibling(n):
    q = os.path.join(here_src, n)
    return open(q, encoding='utf-8', errors='replace').read().split('\n') if os.path.exists(q) else None
PCOMP = {'PV_COMP_HEAD': 0, 'PV_COMP_BERD': 1, 'PV_COMP_HAIR': 2, 'PV_COMP_UPPR': 3, 'PV_COMP_LOWR': 4, 'PV_COMP_HAND': 5, 'PV_COMP_FEET': 6,
         'PV_COMP_TEEF': 7, 'PV_COMP_ACCS': 8, 'PV_COMP_TASK': 9, 'PV_COMP_DECL': 10, 'PV_COMP_JBIB': 11, 'PV_COMP_MAX': 12}
def casemap(lines, rx):
    out, pend = {}, []
    for l in lines:
        l = re.sub(r'case (PV_COMP_\w+):', lambda m: 'case %d:' % PCOMP[m.group(1)], l)
        m = re.match(r'\s*case (\d+):', l)
        if m: pend.append(int(m.group(1))); continue
        m = re.search(rx, l)
        if m and pend:
            for k in pend: out[k] = m.group(1)
            pend = []
    return out
D8_LABEL = casemap(body('func_66'), r'str = "([A-Z0-9_]+)"')
D8_DESC = casemap(body('func_65'), r'"([A-Z0-9_]+DES)"')
PRICE_ID = {}
tp = sibling('tuneables_processing.c')
if tp:
    tun = dict(re.findall(r'Global_262145\.(f_\d+(?:\[\d+\])?) = NETWORK::_NETWORK_GET_TUNABLES_REGISTRATION_INT\((?:joaat\("[^"]+"\)[^,]*|-?\d+), (\d+)\);', '\n'.join(tp)))
    for k, g in casemap(body('func_67'), r'return Global_262145\.(f_\d+(?:\[\d+\])?);').items():
        if g in tun: PRICE_ID[k] = int(tun[g])
D8_SIZE, D8_AREA = {}, {}
ai = sibling('appinternet.c')
if ai:
    D8_SIZE = {k: int(v) for k, v in casemap(body('func_7227', ai), r'return (\d+);').items()}
    D8_AREA = {k: int(v) for k, v in casemap(body('func_9110', ai), r'return (\d+);').items()}
for q, d in P.items():
    if q in PRICE_ID and 'name' in d: PRICE[d['name']] = PRICE_ID[q]

rows, ints, bld = [], [], {}
tiers = {}
for p in sorted(P):
    d = P[p]
    if 'name' not in d or d.get('kind') not in (0, 1, 3, 4) or d.get('b') not in B: continue
    b = B[d['b']]
    kind = d['kind']
    cars = int(b.get('units', d.get('units', 0)) or 0)
    gt = {0: 'T_APT', 1: 'T_GARAGE', 3: 'T_OFFICE', 4: 'T_CLUB'}[kind]
    if kind == 0 and p in range(73, 83): gt = 'T_HOUSE'
    if kind == 0 and cars == 0: cars = 10
    if kind == 0 and TPL.get(p) in (73, 77): cars = 10
    if kind == 3: cars = 0          # offices: Gori Online's own office garages
    if kind == 4: cars = 10
    tiers.setdefault(('gar' if kind == 1 else 'apt', cars), []).append(p)
    d.update(gt=gt, cars=cars)
for (tier, cars), plist in tiers.items():
    if (tier, cars) not in RANGE: continue
    lo, hi = RANGE[(tier, cars)]
    for k, p in enumerate(plist):
        if P[p]['name'] not in PRICE: PRICE[P[p]['name']] = int(round((lo + (hi - lo) * k / max(1, len(plist) - 1)) / 500.0) * 500)

for p in sorted(P):
    d = P[p]
    if 'gt' not in d: continue
    bld[d['b']] = True
    interior = GORI_INT.get(p, '')
    if not interior and d['kind'] == 4: interior = 'clubhouse1' if TPL.get(p) == 91 else 'clubhouse2'
    if not interior and d['kind'] == 0:
        ip = interior_point(p)
        if ip:
            interior = 'o%d' % p
            ints.append('    {"%s", %.4ff, %.4ff, %.4ff, %.1ff, "%s", ""},' % (interior, ip[0][0], ip[0][1], ip[0][2], ip[1], IPL_FOR.get(p, '')))
    rows.append((p, d, interior))

# ---- cutscenes (am_mp_property_ext.c)
def scenes(fname):
    out = {}
    for labels, l in cases(fname, ext):
        for bid in labels:
            if bid == 0: continue
            c = out.setdefault(bid, {})
            def put(k, v):
                if k not in c: c[k] = v          # first value wins (entrance 0 of nested switches)
            for k, pat in (('a', r'uParam1->\[0 /\*15\*/\] = '), ('ar', r'uParam1->\[0 /\*15\*/\]\.f_3 = '), ('b', r'uParam1->\[0 /\*15\*/\]\.f_6 = '),
                           ('br', r'uParam1->\[0 /\*15\*/\]\.f_6\.f_3 = '), ('r0', r'uParam1->f_25\[0 /\*3\*/\] = '), ('r1', r'uParam1->f_25\[1 /\*3\*/\] = '),
                           ('r2', r'uParam1->f_25\[2 /\*3\*/\] = '), ('s', r'uParam1->f_35\[0 /\*4\*/\] = ')):
                m = re.search(pat + V, l)
                if m: put(k, vec(m))
                elif k == 's' and re.search(r'uParam1->f_35\[0 /\*4\*/\] = \{ func_\d+\(iParam0\) \}', l): put('s', 'building')
            for k, pat in (('fov', r'\.f_12 = '), ('shake', r'\.f_13 = '), ('dur', r'\.f_14 = ')):
                m = re.search(r'uParam1->\[0 /\*15\*/\]' + pat + r'(-?[\d.]+)f', l)
                if m: put(k, float(m.group(1)))
            m = re.search(r'uParam1->f_35\[0 /\*4\*/\]\.f_3 = (-?[\d.]+)f', l)
            if m: put('sh', float(m.group(1)))
            m = re.search(r'uParam1->f_42 = (-?[\d.]+)f', l)
            if m: put('extra', float(m.group(1)))
    return out
SC = {1: scenes('func_6694'), 2: scenes('func_6763'), 3: scenes('func_6773')}   # 1 walk in, 2 drive out, 3 walk out of the garage

# ---- interior data per property: metadata map, origins, literal points (mid/low-end apartments)
MAP = {1: 0, 61: 1, 73: 2, 77: 3, 83: 4, 86: 5, 88: 6, 91: 7, 97: 8, 109: 9}
LIT = {}
cur_lit = None
for labels, l in cases('func_36'):
    for k, pat in (('sp', r'uParam0->f_38\[0 /\*27\*/\]\.f_8 = '), ('a', r'uParam0->f_146\[0 /\*12\*/\] = '), ('b', r'uParam0->f_146\[0 /\*12\*/\]\.f_3 = ')):
        m = re.search(pat + V, l)
        if m:
            for p in labels: LIT.setdefault(p, {})[k] = vec(m)
    for k, pat in (('sh', r'uParam0->f_38\[0 /\*27\*/\]\.f_11 = '), ('w', r'uParam0->f_146\[0 /\*12\*/\]\.f_6 = ')):
        m = re.search(pat + r'(-?[\d.]+)f;', l)
        if m:
            for p in labels: LIT.setdefault(p, {})[k] = float(m.group(1))
def exit_width(p):
    if p in range(73, 86): return 1.0
    if p in range(91, 97): return 2.0
    if p in range(97, 103): return 2.25
    return 3.5

def f(x): return '%.4ff' % x
lines = ['// Generated by tools/gen_online_props.py from GTA Online\'s startup.ysc. Do not edit by hand.',
         '// Every GTA Online apartment, house, garage, office and MC clubhouse, with the entrance markers,',
         '// exit points and interior entry points GTA Online uses (see the generator for the derivation).',
         '#pragma once', '',
         'struct OEntrance { uint8_t kind; float mx, my, mz, mh; float ax, ay, az, bx, by, bz, w; float dx, dy, dz; float sx, sy, sz; };   // kind 1 walk-in, 2 garage; area = vehicle trigger; d* door, s* where you stand/stop',
         'struct OCamShot { float x, y, z, rx, ry, rz, fov; };',
         'struct OBuilding { int id; uint8_t type, ne; OEntrance e[3]; float fx, fy, fz, fh; float vx, vy, vz, vh; OCamShot cam[2]; };   // f* on-foot exit, v* vehicle exit, cam: walk-in / garage shots',
         'static const OBuilding O_BUILDINGS[] = {']
bindex = {}
for bid in sorted(bld):
    b = B[bid]
    es = []
    for ei in sorted(b['e']):
        e = b['e'][ei]
        if '.f_26' not in e: continue
        (mx, my, mz), mh = entrance(e)
        a = e.get('.f_0', [mx, my, mz]); c = e.get('.f_3', [mx, my, mz]); w = e.get('.f_6', 3.0)
        dr = e.get('.f_16', [mx, my, mz]); st = e.get('.f_23', [mx, my, mz])
        es.append('{%d, %s, %s, %s, %.2ff, %s, %s, %s, %s, %s, %s, %.2ff, %s, %s, %s, %s, %s, %s}' % (int(e['.f_26']), f(mx), f(my), f(mz), norm(mh) % 360, f(a[0]), f(a[1]), f(a[2]),
                  f(c[0]), f(c[1]), f(c[2]), w, f(dr[0]), f(dr[1]), f(dr[2]), f(st[0]), f(st[1]), f(st[2])))
    walk = next((b['e'][i] for i in sorted(b['e']) if b['e'][i].get('.f_26') == 1), None)
    gar = next((b['e'][i] for i in sorted(b['e']) if b['e'][i].get('.f_26') == 2), None)
    if 'spawn' in b: fp, fh = b['spawn'], b.get('spawnH', 0.0)
    else: fp, fh = entrance(walk or gar)
    if 'veh' in b: vp, vh = b['veh'], b.get('vehH', 0.0)
    elif gar: vp, vh = gar['.f_23'], gar.get('.f_7', 0.0)
    else: vp, vh = fp, fh
    while len(es) < 3: es.append('{0}')
    bindex[bid] = len(bindex)
    cams = []
    for ci in ('0', '1'):
        c = b.get('cam' + ci)
        cams.append('{%s, %s, %s, %.2ff, %.2ff, %.2ff, %.2ff}' % (f(c['pos'][0]), f(c['pos'][1]), f(c['pos'][2]), c['rot'][0], c['rot'][1], c['rot'][2], c.get('fov', 50.0)) if c and 'pos' in c and 'rot' in c else '{0}')
    lines.append('    {%d, %d, %d, {%s}, %s, %s, %s, %.2ff, %s, %s, %s, %.2ff, {%s}},' % (bid, int(b.get('type', 0)), sum(1 for x in es if x != '{0}'), ', '.join(es),
                 f(fp[0]), f(fp[1]), f(fp[2]), norm(fh) % 360, f(vp[0]), f(vp[1]), f(vp[2]), norm(vh) % 360, ', '.join(cams)))
lines += ['};', 'static const int NO_BUILDINGS = (int)(sizeof O_BUILDINGS / sizeof *O_BUILDINGS);', '']
lines += ['// interiors computed for apartments and houses Gori Online had no interior for', 'static const InteriorDef O_INTERIORS[] = {'] + ints + ['};', '']
lines += ['// name, type, price, building location (map blip), interior, GTA Online building index, garage size',
          'static const PropDef O_PROPS[] = {']
for p, d, interior in rows:
    b = B[d['b']]
    lines.append('    {"%s", %s, %d, %s, %s, %s, 0.f, "%s", %d, %d},   // property %d' % (d['name'], d['gt'], PRICE.get(d['name'], 0), f(b['loc'][0]), f(b['loc'][1]), f(b['loc'][2]),
                 interior, bindex[d['b']], d['cars'], p))
lines += ['};', 'static const int NO_PROPS = (int)(sizeof O_PROPS / sizeof *O_PROPS);', '']
# Dynasty 8 listing data, one row per O_PROPS row (same order)
lines += ['// Dynasty 8 (appinternet func_9108) per O_PROPS row: property id, name / description labels,',
          "// the listing's two ints (func_7227 size class, func_9110 area); exact = the price is GTA Online's default.",
          'struct ODyn { int pid; const char *label, *desc; int size, area; bool exact; };',
          'static const ODyn O_DYN[] = {']
for p, d, interior in rows:
    lines.append('    {%d, "%s", "%s", %d, %d, %s},' % (p, D8_LABEL.get(p, ''), D8_DESC.get(p, ''), D8_SIZE.get(p, 0), D8_AREA.get(p, 0), 'true' if p in PRICE_ID else 'false'))
lines += ['};', '']
# interior data, one row per O_PROPS row (same order)
lines += ['// GTA Online interior frame of each O_PROPS row: map = metadata block (-1: literal points / none),',
          '// o* this property\'s interior origin, t* the template\'s origin (func_49), w exit-area width,',
          '// lit*: literal points of mid/low-end apartments (exit area a-b, entry spot sp + heading).',
          'struct OInterior { int pid, map; float ox, oy, oz, orot, tx, ty, tz, trot, w; bool lit; float ax, ay, az, bx, by, bz, spx, spy, spz, sph; };',
          'static const OInterior O_PINT[] = {']
for p, d, interior in rows:
    t = TPL.get(p, 1 if p in range(34, 44) else None)
    mp = MAP.get(t, -1)
    o = ORG.get(p); to = ORG.get(t) if t is not None else None
    L = LIT.get(p, {})
    lit = all(k in L for k in ('sp', 'a', 'b'))
    if not (o and to) and not lit: mp = -1
    oo = o or {'pos': [0, 0, 0], 'rot': 0}; tt = to or {'pos': [0, 0, 0], 'rot': 0}
    a = L.get('a', [0, 0, 0]); bb = L.get('b', [0, 0, 0]); sp = L.get('sp', [0, 0, 0])
    lines.append('    {%d, %d, %s, %s, %s, %.4ff, %s, %s, %s, %.4ff, %.2ff, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %.2ff},' % (
        p, mp if not lit else -1, f(oo['pos'][0]), f(oo['pos'][1]), f(oo['pos'][2]), oo['rot'], f(tt['pos'][0]), f(tt['pos'][1]), f(tt['pos'][2]), tt['rot'],
        L.get('w', exit_width(p)), 'true' if lit else 'false', f(a[0]), f(a[1]), f(a[2]), f(bb[0]), f(bb[1]), f(bb[2]), f(sp[0]), f(sp[1]), f(sp[2]), L.get('sh', 0.0)))
lines += ['};', '']
# cutscenes
lines += ['// Cutscenes: kind 1 walking in, 2 driving out of the garage, 3 walking out of the garage. Camera',
          '// interpolates a -> b over dur seconds (fov, hand shake); the ped/car starts at s (heading sh,',
          '// s on the building location when sb) and follows r0 -> r1 -> r2 (kind 2: drives to r0).',
          'struct OScene { int bid; uint8_t kind; float ax, ay, az, arx, ary, arz, bx, by, bz, brx, bry, brz, fov, shake, dur; bool sb; float sx, sy, sz, sh; float r[3][3]; float extra; };',
          'static const OScene O_SCENES[] = {']
nsc = 0
for kind, table in SC.items():
    for bid in sorted(table):
        c = table[bid]
        if bid not in bld or not all(k in c for k in ('a', 'ar', 'b', 'br')): continue
        if 'r0' not in c:   # camera only (stilt houses, offices, clubhouses): walk from the marker to the door
            we = next((e for i, e in sorted(B[bid]['e'].items()) if e.get('.f_26') == (2 if kind != 1 else 1)), None)
            if not we: continue
            (mx, my, mz), mh = entrance(we)
            door = we.get('.f_16', we.get('.f_23', [mx, my, mz]))
            c = dict(c, s=[mx, my, mz], sh=mh, r0=door)
        sb = c.get('s') == 'building'
        sv = B[bid]['loc'] if sb else (c.get('s') or c['r0'])
        r = [c.get('r0'), c.get('r1') or c.get('r0'), c.get('r2') or c.get('r1') or c.get('r0')]
        lines.append('    {%d, %d, %s, %s, %s, %.2ff, %.2ff, %.2ff, %s, %s, %s, %.2ff, %.2ff, %.2ff, %.2ff, %.2ff, %.2ff, %s, %s, %s, %s, %.2ff, {%s}, %.2ff},' % (
            bid, kind, f(c['a'][0]), f(c['a'][1]), f(c['a'][2]), c['ar'][0], c['ar'][1], c['ar'][2], f(c['b'][0]), f(c['b'][1]), f(c['b'][2]), c['br'][0], c['br'][1], c['br'][2],
            c.get('fov', 50.0), c.get('shake', 0.25), c.get('dur', 5.0), 'true' if sb else 'false', f(sv[0]), f(sv[1]), f(sv[2]), c.get('sh', 0.0),
            ', '.join('{%s, %s, %s}' % (f(x[0]), f(x[1]), f(x[2])) for x in r), c.get('extra', 0.5)))
        nsc += 1
lines += ['};', 'static const int NO_SCENES = (int)(sizeof O_SCENES / sizeof *O_SCENES);', '']
print('%d cutscenes' % nsc)
open(out_path, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines))
print('%d buildings, %d properties, %d computed interiors -> %s' % (len(bindex), len(rows), len(ints), out_path))
missing = [d['name'] for p, d, i in rows if not PRICE.get(d['name'])]
if missing: print('NO PRICE:', missing)
noint = [d['name'] for p, d, i in rows if not i and d['kind'] != 1]
if noint: print('NO INTERIOR:', noint)
