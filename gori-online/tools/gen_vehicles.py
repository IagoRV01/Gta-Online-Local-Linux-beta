# Builds vehicles_data.h (the whole GTA Online vehicle catalogue) from DurtyFree's vehicles.json dump.
# Prices: the real Online price for the well-known ones, an estimate from class + performance for the rest.
# Usage: python tools/gen_vehicles.py <path to vehicles.json>
import json, sys, statistics, os

src = sys.argv[1]
out = os.path.join(os.path.dirname(__file__), '..', 'vehicles_data.h')
V = json.load(open(src, encoding='utf-8'))

REAL = {
    # Super
    'adder': 1000000, 'zentorno': 725000, 'entityxf': 795000, 't20': 2200000, 'osiris': 1950000, 'prototipo': 2700000,
    'vagner': 1535000, 'krieger': 2875000, 'emerus': 2750000, 'thrax': 2325000, 'zorrusso': 1925000, 'tezeract': 2825000,
    'deveste': 1795000, 'italigtb': 1189000, 'nero': 1440000, 'tempesta': 1329000, 'visione': 2250000, 'xa21': 2375000,
    'cyclone': 1890000, 'autarch': 1955000, 'sc1': 1603000, 'entity2': 2305000, 'furia': 2740000, 'ignus': 2765000,
    'torero2': 2890000, 'zeno': 2820000, 's80': 2575000, 'tyrant': 2515000, 'taipan': 1980000, 'tigon': 2310000,
    'penetrator': 880000, 'reaper': 1595000, 'fmj': 1750000, 'pfister811': 1135000, 'turismor': 500000, 'infernus': 440000,
    'cheetah': 650000, 'vacca': 240000, 'bullet': 155000, 'voltic': 150000, 'gp1': 1260000, 'le7b': 4750000,
    'nero2': 605000, 'italigtb2': 495000, 'banshee2': 565000, 'sultanrs': 795000, 'voltic2': 3830400, 'vigilante': 3750000,
    'entity3': 2440000, 'torero': 998000, 'champion': 2995000, 'tenf': 1675000, 'corsita': 1795000, 'lm87': 2915000,
    # Sports
    'kuruma': 126350, 'kuruma2': 698250, 'elegy': 904000, 'elegy2': 95000, 'jester': 240000, 'jester3': 790000,
    'jester4': 1970000, 'comet2': 100000, 'comet5': 1145000, 'comet6': 1878000, 'calico': 1995000, 'euros': 1800000,
    'futo2': 1590000, 'growler': 1627000, 'vectre': 1785000, 'zr350': 1615000, 'sultan3': 1718000, 'remus': 1370000,
    'cypher': 1550000, 'rt3000': 1715000, 'dominator7': 1220000, 'dominator8': 1480000, 'warrener2': 1260000,
    'tailgater2': 1495000, 'sugoi': 1224000, 'sultan2': 1718000, 'neon': 1500000, 'pariah': 1420000, 'raiden': 1375000,
    'revolter': 1610000, 'schlagen': 1300000, 'italigto': 1965000, 'vstr': 1285000, 'carbonizzare': 195000,
    'massacro': 275000, 'ninef': 120000, 'banshee': 105000, 'sultan': 12000, 'feltzer2': 145000, 'coquette': 138000,
    'massacro2': 385000, 'jester2': 350000, 'furoregt': 448000, 'specter': 599000, 'specter2': 1125000, 'buffalo4': 2140000,
    'omnisegt': 1795000, 'paragon': 905000, 'gb200': 940000, 'flashgt': 1675000, 'jugular': 1225000, 'locust': 1625000,
    'issi7': 897000, 'drafter': 718000, 'komoda': 1700000, 'imorgon': 2165000, 'coquette4': 1510000, 'penumbra2': 1380000,
    'banshee3': 2650000, 'coquette6': 2100000, 'paragon3': 2190000,
    # Sports classics, muscle, others
    'stingergt': 875000, 'ztype': 950000, 'monroe': 490000, 'jb700': 350000, 'deluxo': 5750000, 'stromberg': 3185000,
    'toreador': 3660000, 'viseris': 875000, 'torero': 998000, 'gt500': 785000, 'savestra': 990000, 'z190': 900000,
    'dominator': 35000, 'gauntlet': 32000, 'sabregt': 15000, 'dukes': 62000, 'dukes2': 665000, 'vigero': 21000,
    'greenwood': 1465000, 'ruiner2': 5745600, 'buffalo5': 2400000, 'gauntlet4': 745000, 'dominator3': 725000,
    'kamacho': 345000, 'sandking': 45000, 'trophytruck': 550000, 'brawler': 715000, 'caracara2': 875000,
    'baller3': 149000, 'granger': 35000, 'toros': 498000, 'rebla': 1175000, 'novak': 608000, 'granger2': 1489000,
    'baller7': 890000, 'jubilee': 1650000, 'patriot3': 1710000, 'bifta': 75000, 'vivanite': 1600000,
    # Motorcycles
    'akuma': 9000, 'bati': 15000, 'hakuchou': 82000, 'sanchez': 8000, 'oppressor': 3524500, 'oppressor2': 8000000,
    'shotaro': 2225000, 'deathbike': 1269000, 'hakuchou2': 976000, 'bf400': 95000, 'manchez2': 670000, 'reever': 1900000,
    # Weaponized / military
    'insurgent': 1795000, 'insurgent3': 897750, 'scramjet': 4628400, 'khanjali': 3850350, 'apc': 3092250,
    'halftrack': 2254350, 'technical': 1263500, 'nightshark': 1245000, 'dune3': 1130500, 'tampa3': 2108050,
    'chernobog': 3311700, 'terbyte': 1375000, 'rcbandito': 1590000, 'zhaba': 2400000, 'squaddie': 1130000,
    'vetir': 1470000, 'barrage': 2121350, 'boxville5': 2926000, 'riot2': 3000000, 'menacer': 1775000, 'speedo4': 1785000,
    'mule4': 1795000, 'pounder2': 1395000, 'trailersmall2': 1400000, 'rhino': 3000000, 'kuruma2': 698250,
    # Aircraft
    'buzzard': 1750000, 'buzzard2': 2000000, 'frogger': 1300000, 'luxor': 1625000, 'luxor2': 10000000, 'hydra': 3990000,
    'lazer': 6500000, 'akula': 3704050, 'hunter': 4123500, 'savage': 2593500, 'valkyrie': 3790500, 'annihilator2': 3870000,
    'sparrow': 1815000, 'volatol': 3724000, 'bombushka': 5918500, 'avenger': 6450000, 'starling': 3657500,
    'molotok': 4788000, 'pyro': 4455500, 'rogue': 1596000, 'nokota': 2653350, 'howard': 1296750, 'alphaz1': 2121350,
    'seabreeze': 1130000, 'tula': 5173700, 'mogul': 3125500, 'raiju': 6855000, 'streamer216': 1715000,
    'alkonost': 4350000, 'conada': 2200000, 'annihilator': 1825000, 'supervolito': 2113000, 'swift': 1500000,
    'volatus': 2295000, 'miljet': 1700000, 'besra': 1150000, 'nimbus': 1900000, 'titan': 2000000, 'cargobob': 1995000,
    'dodo': 500000, 'duster': 275000, 'cuban800': 240000, 'mammatus': 300000, 'velum': 450000, 'vestra': 950000,
    'shamal': 1150000, 'stunt': 150000, 'microlight': 665000, 'havok': 2300000, 'seasparrow': 1815000, 'swift2': 5150000,
    'supervolito2': 3330000, 'volatus': 2295000, 'thruster': 3657500,
    # Boats
    'speeder': 325000, 'seashark': 16899, 'toro': 1750000, 'longfin': 2125000, 'kosatka': 2200000, 'tug': 1250000,
    'avisa': 1995000, 'jetmax': 299000, 'marquis': 413990, 'tropic': 22000, 'suntrap': 25160, 'squalo': 196621,
    'dinghy': 166250, 'submersible': 1150000, 'submersible2': 1325000, 'patrolboat': 2150000, 'toro2': 1750000,
}

BASE = {0: 18000, 1: 30000, 2: 70000, 3: 90000, 4: 60000, 5: 450000, 6: 300000, 7: 1700000, 8: 60000, 9: 120000,
        10: 150000, 11: 80000, 12: 45000, 13: 1000, 14: 250000, 15: 1500000, 16: 1600000, 17: 60000, 18: 150000,
        19: 1800000, 20: 150000, 22: 3000000}
SKIP_TYPES = {'TRAIN', 'TRAILER'}

def perf(v):
    return max(1.0, (v.get('MaxSpeed', 0) or 0) * (0.5 + max(0.0, v.get('Acceleration', 0) or 0)))

by_class = {}
for v in V:
    by_class.setdefault(v['ClassId'], []).append(perf(v))
med = {k: statistics.median(x) for k, x in by_class.items()}

rows = []
for v in V:
    name = v['Name'].lower()
    cls = v['ClassId']
    if v.get('Type') in SKIP_TYPES or cls == 21:
        continue
    weapon = 1 if v.get('Weapons') else 0
    if name in REAL:
        price = REAL[name]
    else:
        f = (perf(v) / med.get(cls, 1)) ** 1.5
        f = min(2.2, max(0.5, f))
        price = BASE.get(cls, 100000) * f
        if weapon:
            price *= 1.6 if cls in (15, 16) else 2.5
        price = round(price / 500) * 500 if price < 100000 else round(price / 5000) * 5000
    rows.append((name, cls, int(price), weapon))

rows.sort()
with open(out, 'w', encoding='utf-8', newline='\n') as f:
    f.write('// Generated by tools/gen_vehicles.py from the DurtyFree vehicles.json dump. Do not edit.\n#pragma once\n')
    f.write('struct VehRow { const char *model; uint8_t cls; int price; uint8_t weapon; };\n')
    f.write('static const VehRow VEH_ROWS[] = {\n')
    f.write(',\n'.join('{"%s",%d,%d,%d}' % r for r in rows))
    f.write('\n};\n')
print(len(rows), 'vehicles')
