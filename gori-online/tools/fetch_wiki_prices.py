# Builds tools/web_prices.json (model -> GTA Online website price) for gen_web_vehicles.py.
# GTA Online's prices come from Rockstar's catalog server (NET_GAMESERVER_GET_PRICE in appinternet), so
# they are not in the scripts: they are read from the GTA wiki's "Stock in GTA Online" pages of each
# website (gta.fandom.com MediaWiki API), matched by English display name (DurtyFree's vehicles.json),
# plus MANUAL for the ones the pages don't list in a parsable way.
#
#   python tools/fetch_wiki_prices.py <vehicles.json> [cache dir]
import json, re, os, sys, time, urllib.request, urllib.parse

here = os.path.dirname(os.path.abspath(__file__))
V = json.load(open(sys.argv[1], encoding='utf-8'))
cache = sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, '.wiki_cache')
os.makedirs(cache, exist_ok=True)
PAGES = ['Legendary Motorsport/Stock in GTA Online', 'Southern San Andreas Super Autos/Stock in GTA Online',
         'Warstock Cache & Carry/Stock in GTA Online', 'Elitás Travel/Stock in GTA Online', 'DockTease/Stock in GTA Online',
         'Pedal and Metal Cycles', "Benny's Original Motor Works", 'Arenawar']
MANUAL = {
    'feltzer3': 975000, 'neon': 1500000, 'jugular': 1225000, 'ignus': 2765000, 'torero2': 2890000, 'stingertt': 2380000,
    'dodo': 500000, 'monster': 742000, 'pbus': 550000, 'marshall': 500000, 'dukes2': 665000, 'avenger': 3450000,
    'terbyte': 1375000, 'kosatka': 2200000, 'submersible2': 1325000, 'sanchez': 8000, 'sanchez2': 8000, 'faggio2': 5000,
    'rebel': 3000, 'sentinel2': 60000, 'rumpo': 13000, 'dukes': 62000, 'blista2': 42000, 'stalion': 71000,
    'stalion2': 277000, 'dominator2': 315000, 'gauntlet2': 230000, 'buffalo3': 535000, 'sentinel3': 650000,
    'rcbandito': 1590000, 'outlaw': 1268000, 'greenwood': 1465000, 'moonbeam': 32500, 'faction': 36000,
    'buccaneer': 29000, 'voodoo2': 5500, 'virgo3': 165000, 'tornado': 30000, 'sabregt': 15000, 'sultan': 12000,
    'italigtb': 1189000, 'nero': 1440000, 'diablous': 169000, 'fcr': 135000, 'specter': 599000, 'manana': 8000,
    'peyote': 38000, 'daemon2': 145000, 'rhinehart': 1598000,
    # newest vehicles (their own wiki pages)
    'velenogt': 3000000, 'lrcgt': 2650000, 'laufer': 645000, 'merula': 1394000, 'estride': 1425000,
    # in the game files and in GTA Online's website script, not on the wiki yet: estimates
    'horus': 2950000, 'cartuccia': 1850000, 'polignus': 4800000,
}

def wikitext(title):
    fn = os.path.join(cache, re.sub(r'[^A-Za-z0-9]', '_', title) + '.json')
    if not os.path.exists(fn):
        q = urllib.parse.urlencode({'action': 'parse', 'page': title, 'prop': 'wikitext', 'format': 'json', 'redirects': 1})
        req = urllib.request.Request('https://gta.fandom.com/api.php?' + q, headers={'User-Agent': 'GoriOnline-tools/1.0 (personal mod tooling)'})
        open(fn, 'wb').write(urllib.request.urlopen(req, timeout=30).read())
        time.sleep(1)
    d = json.load(open(fn, encoding='utf-8'))
    return d['parse']['wikitext']['*'] if 'parse' in d else ''

def norm(s): return re.sub(r'[^a-z0-9]', '', s.lower().replace('é', 'e').replace('á', 'a').replace('ü', 'u'))
def money(s):
    v = [int(x.replace(',', '')) for x in re.findall(r'\$([\d,]{3,})', s)]
    return max(v) if v else None   # "$1,211,250 - $1,615,000": trade price - price
LINK = r'\[\[([^\]|]+)(?:\|([^\]]+))?\]\]'
makers = {v['ManufacturerDisplayName']['English'] for v in V if v.get('ManufacturerDisplayName')}
names = {}
for t in map(wikitext, PAGES):
    for blk in re.findall(r'\{\{WebVehicleModule(.*?)\}\}\n', t, re.S):
        n = re.search(r'\|name\s*=\s*(.*)', blk); p = re.search(r'\|price\s*=\s*(.*)', blk)
        if n and p and money(p.group(1)):
            m = re.search(LINK, n.group(1))
            names.setdefault(norm((m.group(2) or m.group(1)) if m else n.group(1)), money(p.group(1)))
    for tbl in re.findall(r'\{\|(.*?)\n\|\}', t, re.S):
        for row in tbl.split('\n|-')[1:]:
            cells = [c for c in re.split(r'\n\||\|\|', row) if c.strip()]
            found = []
            for c in cells:
                if '$' in c: break
                if '[[File:' in c: continue
                found += [(m.group(2) or m.group(1)).strip() for m in re.finditer(LINK, c)]
            pc = next((c for c in cells if '$' in c), None)
            found = [f for f in found if f not in makers]
            if found and pc and money(pc): names.setdefault(norm(found[0]), money(pc))
out = {}
for v in V:
    dn = v.get('DisplayName') and v['DisplayName'].get('English')
    if dn and norm(dn) in names: out[v['Name'].lower()] = names[norm(dn)]
out.update(MANUAL)
json.dump(dict(sorted(out.items())), open(os.path.join(here, 'web_prices.json'), 'w', encoding='utf-8', newline='\n'), indent=0)
print('%d wiki names, %d model prices -> tools/web_prices.json' % (len(names), len(out)))
