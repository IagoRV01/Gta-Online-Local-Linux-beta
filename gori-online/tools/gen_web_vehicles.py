# Generates web_vehicles.h: which vehicle each page of the in-game car websites sells, as GTA Online
# does it (decompiled appinternet.ysc from acidlabsdev/gtav-legacy-scripts):
#   func_8808(site): Online catalog, slots 3 .. n+2 (Legendary 222, Southern SA 291, Warstock 189,
#                    Elitas 38, Docktease 28, P&M 11, Benny's 32, Arena 22)
#   func_8744(site, num): whether slot num is sold in Online (the others return -1 in func_8804 too)
#   func_8804(site, page): page (= slot) -> vehicle index; per site a switch on num = page - 3, or on
#                          num - k (Benny's, Arena), with tunable-gated alternatives (first one wins)
#   func_8796(index): vehicle index -> model: joaat("name"), a raw hash (vehicles newer than the
#                     decompiler's name list), a Global set in standard_global_init, or a func
# Raw hashes are named from every joaat("...") string in the scripts next to appinternet.c and the model
# names in vehicles_data.h; the rest stay "0x%08X" (ModelHash() understands both).
#
#   python tools/gen_web_vehicles.py <appinternet.c> [web_vehicles.h]
import re, sys, os, glob

src_path = sys.argv[1]
src = open(src_path, encoding='utf-8', errors='replace').read().split('\n')
here = os.path.dirname(os.path.abspath(__file__))
out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, '..', 'web_vehicles.h')

def joaat(s):
    h = 0
    for c in s.lower().encode():
        h = (h + c) & 0xffffffff; h = (h + (h << 10)) & 0xffffffff; h ^= h >> 6
    h = (h + (h << 3)) & 0xffffffff; h ^= h >> 11; h = (h + (h << 15)) & 0xffffffff
    return h

def body(name):
    i = next(k for k, l in enumerate(src) if re.match(r'^\S.* ' + name + r'\(', l))
    j = next(k for k in range(i + 1, len(src)) if src[k] == '}')
    return src[i:j + 1]

# names for raw hashes
names = {}
for f in glob.glob(os.path.join(os.path.dirname(os.path.abspath(src_path)), '*.c')):
    for n in re.findall(r'joaat\("([A-Za-z0-9_]+)"\)', open(f, encoding='utf-8', errors='replace').read()):
        names[joaat(n)] = n.lower()
vd = os.path.join(here, '..', 'vehicles_data.h')
if os.path.exists(vd):
    for n in re.findall(r'\{"([a-z0-9_]+)",', open(vd, encoding='utf-8').read()):
        names[joaat(n)] = n
# vehicles newer than both lists (names checked in game: GET_DISPLAY_NAME_FROM_VEHICLE_MODEL + joaat)
for n in ('horus', 'velenogt', 'lrcgt', 'cartuccia', 'polignus', 'laufer', 'merula', 'estride'):
    names[joaat(n)] = n
glob_vals = {}
sgi = os.path.join(os.path.dirname(os.path.abspath(src_path)), 'standard_global_init.c')
if os.path.exists(sgi):
    for g, v in re.findall(r'(Global_\d+) = (-?\d+|joaat\("[a-z0-9_]+"\));', open(sgi, encoding='utf-8', errors='replace').read()):
        m = re.match(r'joaat\("([a-z0-9_]+)"\)', v)
        glob_vals[g] = joaat(m.group(1)) if m else int(v) & 0xffffffff

# freemode's pseudo-models for the special purchases (VE_BIG_TRUCK_t0_v0 etc.): the vehicle when there is
# one, "@name" for the ones that need their own purchase code (the special vehicles phase)
SPECIAL = {'Global_77357': '@yacht', 'Global_77358': '@moc', 'Global_77359': '@ballistic',
           'Global_77360': 'avenger', 'Global_77361': 'terbyte'}
for g, n in SPECIAL.items():
    if g in glob_vals: names[glob_vals[g]] = n

def model_name(h):
    return names.get(h, '0x%08X' % h)

# func_8744: whether slot num of a site is sold in GTA Online (Global_80337 = Online, true)
def sold(site, num):
    if site == 10: return 0 <= num <= 8 or 14 <= num < 222
    if site == 16: return 0 <= num <= 30 or 35 <= num < 291
    if site == 11: return 0 <= num <= 5 or 12 <= num < 38
    if site == 12:
        if num in (0, 2, 4, 5, 7): return True
        if num in (14, 15, 17, 18) or 21 <= num < 189:
            return num not in (57, 58, 59, 85, 86, 87, 105, 106, 107, 142, 143, 144)
        return False
    if site == 13:
        if 0 <= num <= 6 or num in (8, 11, 14): return True
        return 16 <= num < 28 and num not in (23, 24, 25)
    if site == 15: return 0 <= num <= 7
    if site == 26: return num != 10 and 0 <= num <= 44
    if site == 29: return True
    return False

# index -> model
models = {}
pend = []
for l in body('func_8796'):
    m = re.match(r'\s*case (\d+):', l)
    if m: pend.append(int(m.group(1))); continue
    if 'return' not in l or not pend: continue
    h = None
    m = re.search(r'return joaat\("([a-z0-9_]+)"\);', l)
    if m: h = joaat(m.group(1)); names.setdefault(h, m.group(1))
    m = re.search(r'return (-?\d+);', l)
    if m and int(m.group(1)) not in (0, -1): h = int(m.group(1)) & 0xffffffff
    m = re.search(r'return (Global_\d+);', l)
    if m and m.group(1) in glob_vals: h = glob_vals[m.group(1)]
    m = re.search(r'return (func_\d+)\(\);', l)
    if m:
        fm = re.search(r'return joaat\("([a-z0-9_]+)"\);', '\n'.join(body(m.group(1))))
        if fm: h = joaat(fm.group(1))
    if h is not None:
        for k in pend: models[k] = model_name(h)
    pend = []

# (site, page) -> index
pages = {}
site, off, pend = None, 0, []
for l in body('func_8804'):
    m = re.match(r'\t\tcase (\d+):', l)
    if m: site, off = int(m.group(1)), 0; continue
    m = re.search(r'switch \(num - (\d+)\)', l)
    if m: off = int(m.group(1)); continue
    if re.search(r'switch \(num\)', l): off = 0; continue
    m = re.match(r'\t\t\t\t+case (-?\d+):', l)
    if m and site is not None: pend.append(int(m.group(1)) + off); continue
    m = re.search(r'return (-?\d+);', l)
    if m and site is not None and pend:
        for k in pend: pages.setdefault((site, k + 3), int(m.group(1)))
        pend = []

COUNT = {10: 222, 16: 291, 12: 189, 11: 38, 13: 28, 15: 11, 26: 32, 29: 22}
NAMES = {10: 'Legendary Motorsport', 16: 'Southern San Andreas Super Autos', 12: 'Warstock Cache & Carry', 11: 'Elitas Travel',
         13: 'Docktease', 15: 'Pedal and Metal Cycles', 26: "Benny's Original Motorworks", 29: 'Arena War'}
# GTA Online prices (tools/fetch_wiki_prices.py); 0 = use Gori Online's catalogue price
import json
pp = os.path.join(here, 'web_prices.json')
PRICES = json.load(open(pp, encoding='utf-8')) if os.path.exists(pp) else {}
def price_of(site, model):
    if model in PRICES: return PRICES[model]
    root = re.sub(r'\d+$', '', model)   # Arena War styles (cerberus2/3) cost what the first style does
    for (s2, p2), idx2 in sorted(pages.items()):
        m2 = models.get(idx2, '')
        if s2 == site and m2 != model and re.sub(r'\d+$', '', m2) == root and m2 in PRICES: return PRICES[m2]
    return 0
rows, unnamed, special = [], set(), set()
for (s, p), idx in sorted(pages.items()):
    if s in COUNT and idx >= 0 and idx in models and sold(s, p - 3):
        rows.append((s, p, idx, models[idx], price_of(s, models[idx])))
        if models[idx].startswith('0x'): unnamed.add(models[idx])
        if models[idx].startswith('@'): special.add(models[idx])
lines = ['// Generated by tools/gen_web_vehicles.py from GTA Online\'s appinternet.ysc. Do not edit.',
         '// site = GET_CURRENT_WEBSITE_ID, page = GET_CURRENT_WEBPAGE_ID of the vehicle\'s page = SET_PRICES slot;',
         '// model = model name, "0x%08X" for a hash with no known name (ModelHash() takes both), or "@name" for a',
         '// special purchase that is not a plain vehicle (yacht, MOC, ballistic equipment).',
         '#pragma once', 'struct WebVehicle { int site, page, index; const char *model; int price; };   // price 0: catalogue price', 'static const WebVehicle WEB_VEHICLES[] = {']
for r in rows: lines.append('    {%d, %d, %d, "%s", %d},' % r)
lines += ['};', 'static const int NWEB_VEHICLES = (int)(sizeof WEB_VEHICLES / sizeof *WEB_VEHICLES);', '']
open(out, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines))
by = {}
for r in rows: by[r[0]] = by.get(r[0], 0) + 1
print('%d models, %d site pages -> %s' % (len(models), len(rows), out))
for s in sorted(by): print('  site %d %-34s %d vehicles (Online slots %d)' % (s, NAMES.get(s, '?'), by[s], COUNT[s]))
noprice = sorted({r[3] for r in rows if not r[4]})
if noprice: print('  no Online price (catalogue price): ' + ' '.join(noprice))
if special: print('  special purchases (own code): ' + ' '.join(sorted(special)))
if unnamed: print('  no name for: ' + ' '.join(sorted(unnamed)))
