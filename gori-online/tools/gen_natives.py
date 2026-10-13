# Regenerates nativehashes.h with every N_<NAME> used in the .cpp/.h files of the mod.
# Usage: python tools/gen_natives.py   (natives.json = alloc8or/gta5-nativedb-data)
import json, re, os, glob
here = os.path.join(os.path.dirname(__file__), '..')
d = json.load(open(os.path.join(here, 'natives.json'), encoding='utf-8'))
idx = {}
for ns, fs in d.items():
    for h, f in fs.items():
        idx[f['name']] = (h, f)
used = set()
for p in glob.glob(os.path.join(here, '*.cpp')) + glob.glob(os.path.join(here, '*.inc')):
    used |= set(re.findall(r'\bN_([A-Z0-9_]+)\b', open(p, encoding='utf-8').read()))
missing = sorted(u for u in used if u not in idx)
if missing:
    raise SystemExit('unknown natives: ' + ', '.join(missing))
with open(os.path.join(here, 'nativehashes.h'), 'w', encoding='utf-8', newline='\n') as out:
    out.write('// Generated from alloc8or/gta5-nativedb-data (natives.json) by tools/gen_natives.py. Native name -> hash.\n#pragma once\n#include <cstdint>\n')
    for u in sorted(used):
        h, f = idx[u]
        params = ', '.join(p['type'] + ' ' + p['name'] for p in f.get('params', []))
        out.write('constexpr uint64_t N_%s = %s; // %s %s(%s)\n' % (u, h, f.get('return_type', 'void'), u, params))
print(len(used), 'natives')
