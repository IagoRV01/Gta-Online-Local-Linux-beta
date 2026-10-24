"""Convert GTA Online races (Rockstar's own UGC JSON, as Rockstar's cloud serves them) to Gori Online race files.

  python tools/rockstar-races/convert.py <json dir> <out dir> [vanilla classes .lua or .json]
(types.txt next to the json dir, when there, gives each race its Social Club subtype.)

The jobs are downloaded by the player from Rockstar's cloud (prod.cloud.rockstargames.com/ugc/gta5mission/<id>/0_0_en.json)
and stay on their PC: neither they nor the converted files are published. The output is Gori Online's race XML (the
Community Races layout it already reads) with what vanilla races add:
  - <Laps> (0: point to point), <Type>, <DefaultVehicle>, <Classes>;
  - <CheckpointData>: per checkpoint its heading, size and kind (round, air, warp, pit) and its secondary (split route)
    checkpoint when it has one;
  - props with their colour (<Texture>), whether they are dynamic, and the stunt pads' strength (<Speed>: boost pads
    1..5, slow-down pads -1..-3).
The field meanings follow GTA Online's own Creator data as documented by the custom_races project (Unlicense).
"""
import json, os, re, sys
from xml.sax.saxutils import escape

def joaat(s):
    h = 0
    for c in s.lower().encode():
        h = (h + c) & 0xFFFFFFFF; h = (h + (h << 10)) & 0xFFFFFFFF; h ^= h >> 6
    h = (h + (h << 3)) & 0xFFFFFFFF; h ^= h >> 11; h = (h + (h << 15)) & 0xFFFFFFFF
    return h

BOOST = {joaat(n): 1 for n in ("stt_prop_track_speedup", "stt_prop_track_speedup_t1", "stt_prop_track_speedup_t2", "ar_prop_ar_speed_ring",
                               "stt_prop_stunt_tube_speed", "stt_prop_stunt_tube_speedb", "stt_prop_track_bend_bar_l_b", "stt_prop_track_bend_bar_m_b") if n}
SLOW = {joaat(n) for n in ("stt_prop_track_slowdown", "stt_prop_track_slowdown_t1", "stt_prop_track_slowdown_t2")}
# Social Club's own race subtypes (types.txt: "<content id>:<letters>", from its job list): the most specific one wins
TYPE_LETTERS = [("T", "Transform Race"), ("V", "Special Vehicle Race"), ("S", "Stunt Race"), ("R", "Street Race"), ("P", "Pursuit Race"),
                ("D", "Drift Race"), ("G", "Drag Race"), ("A", "Air Race"), ("W", "Sea Race"), ("B", "Bike Race"), ("L", "Land Race")]
TYPES = {}

def u32(v): return v & 0xFFFFFFFF
def bit(v, b): return v is not None and (int(v) >> b) & 1 == 1
def at(lst, i, d=None):
    return lst[i] if isinstance(lst, list) and i < len(lst) and lst[i] is not None else d
def vec(d): return (float(d.get("x", 0)), float(d.get("y", 0)), float(d.get("z", 0))) if isinstance(d, dict) else (0.0, 0.0, 0.0)
def V(tag, p, ind):
    return f"{ind}<{tag}>\n{ind}\t<X>{p[0]:.3f}</X>\n{ind}\t<Y>{p[1]:.3f}</Y>\n{ind}\t<Z>{p[2]:.3f}</Z>\n{ind}</{tag}>\n"

def load_classes(path):
    """class id -> {aveh: [...], adlc: [...]} from custom_races' client/variable.lua (Client.Shared.Vanilla), or a json of it"""
    if not path or not os.path.exists(path): return {}
    if path.endswith(".json"): return {int(k): v for k, v in json.load(open(path, encoding="utf-8")).items()}
    s = open(path, encoding="utf-8").read()
    s = s[s.index("Client.Shared.Vanilla"):]
    out = {}
    for m in re.finditer(r"\[(\d+)\]\s*=\s*\{(.*?)\n\t\}", s, re.S):
        body = m.group(2)
        def names(key):
            mm = re.search(key + r"\s*=\s*\{([^}]*)\}", body)
            return re.findall(r'"([^"]+)"', mm.group(1)) if mm else []
        out[int(m.group(1))] = {"aveh": names("aveh"), "adlc": names("adlc")}
    return out

def vehicles(race, gen, classes):
    """the default vehicle and the list the race allows (GTA Online's class bitmasks)"""
    allowed = []
    clbs, aveh = race.get("clbs", 0), race.get("aveh", [])
    adlcs = [race.get("adlc", []), race.get("adlc2", []), race.get("adlc3", [])]
    for cid in range(28):
        if not bit(clbs, cid) or cid not in classes: continue
        c = classes[cid]
        for i, m in enumerate(c["aveh"]):
            if not bit(at(aveh, cid, 0), i): allowed.append(m)
        for k, adlc in enumerate(adlcs):
            for i in range(31):
                if bit(at(adlc, cid, 0), i):
                    j = k * 31 + i
                    if j < len(c["adlc"]): allowed.append(c["adlc"][j])
    ivm, icv = gen.get("ivm", 0), race.get("icv", 0)
    default = None
    if isinstance(ivm, int) and abs(ivm) > 1000: default = str(u32(ivm))   # a model hash
    elif icv in classes and 0 <= ivm < len(classes[icv]["aveh"]): default = classes[icv]["aveh"][ivm]
    if default is None and allowed: default = allowed[0]
    seen, out = set(), []
    for m in ([default] if default else []) + allowed:
        if m not in seen: seen.add(m); out.append(m)
    return default, out

def convert(j, classes, cid):
    m = j["mission"]; gen, race = m["gen"], m["race"]
    name = gen.get("nm", cid)
    desc = "".join(gen.get("dec", [])) if isinstance(gen.get("dec"), list) else str(gen.get("dec", ""))
    laps = int(race.get("lap", 0) or 0)
    rtype = next((t for l, t in TYPE_LETTERS if l in TYPES.get(cid, "")), "Race")
    n = int(race.get("chp", 0))
    o = ['<?xml version="1.0" encoding="utf-8"?>\n<Race>\n',
         f"\t<Name>{escape(name)}</Name>\n\t<Description>{escape(desc)}</Description>\n\t<Creator>Rockstar Games</Creator>\n",
         f"\t<ContentId>{cid}</ContentId>\n\t<Type>{rtype}</Type>\n\t<Laps>{laps}</Laps>\n\t<LapsAvailable>{'true' if laps > 0 else 'false'}</LapsAvailable>\n"]
    default, allowed = vehicles(race, gen, classes)
    if default: o.append(f"\t<DefaultVehicle>{default}</DefaultVehicle>\n")
    o.append("\t<AvailableVehicles>\n" + "".join(f"\t\t<VehicleHash>{v}</VehicleHash>\n" for v in allowed) + "\t</AvailableVehicles>\n")
    o.append("\t<Checkpoints>\n")
    for i in range(n): o.append(V("Vector3", vec(at(race.get("chl"), i, {})), "\t\t"))
    o.append("\t</Checkpoints>\n\t<CheckpointData>\n")
    cpbs1, cpbs2, rnd, rnds = race.get("cpbs1", []), race.get("cpbs2", []), race.get("rndchk", []), race.get("rndchks", [])
    trf = race.get("cptfrm", [])
    for i in range(n):
        b1, b2 = at(cpbs1, i, 0), at(cpbs2, i, 0)
        size = float(at(race.get("chs"), i, 1.0) or 1.0)
        kinds = []
        if bit(b1, 1) or at(rnd, i, False): kinds.append("round")
        if bit(b1, 9): kinds.append("air")
        if bit(b1, 27): kinds.append("warp")
        if bit(b2, 16): kinds.append("pit")
        o.append(f"\t\t<Cp><Heading>{float(at(race.get('chh'), i, 0.0)):.2f}</Heading><Size>{max(0.5, size):.2f}</Size><Kind>{','.join(kinds)}</Kind>")
        t = at(trf, i, -1)
        if isinstance(t, int) and t >= 0: o.append(f"<Transform>{t}</Transform>")
        s2 = vec(at(race.get("sndchk"), i, {}))
        if s2 != (0.0, 0.0, 0.0):
            k2 = []
            if bit(b1, 2) or at(rnds, i, False): k2.append("round")
            if bit(b1, 13): k2.append("air")
            if bit(b1, 28): k2.append("warp")
            size2 = float(at(race.get("chs2"), i, size) or size)
            o.append(f"<Split><X>{s2[0]:.3f}</X><Y>{s2[1]:.3f}</Y><Z>{s2[2]:.3f}</Z><Size>{max(0.5, size2):.2f}</Size><Kind>{','.join(k2)}</Kind></Split>")
        o.append("</Cp>\n")
    o.append("\t</CheckpointData>\n")
    # transforms: cptfrm >= 0 is an index into trfmvm (the race's transform vehicles), -2 a random vehicle (Known Unknowns)
    raw = race.get("trfmvm") or []
    tv = [u32(v) for v in raw if v]
    if tv: o.append("\t<TransformVehicles>\n" + "".join(f"\t\t<VehicleHash>{v}</VehicleHash>\n" for v in tv) + "\t</TransformVehicles>\n")
    rows = []
    for i in range(n):
        t = at(trf, i, -1)
        if isinstance(t, int) and 0 <= t < len(raw) and raw[t]: rows.append((i, str(u32(raw[t]))))
        elif t == -2: rows.append((i, "random"))
    if rows:
        o.append("\t<Transforms>\n" + "".join(f"\t\t<Transform><Checkpoint>{i}</Checkpoint><VehicleHash>{v}</VehicleHash></Transform>\n" for i, v in rows) + "\t</Transforms>\n")
    veh = m.get("veh", {})
    o.append("\t<SpawnPoints>\n")
    for i in range(int(veh.get("no", 0))):
        o.append("\t\t<SpawnPoint>\n" + V("Position", vec(at(veh.get("loc"), i, {})), "\t\t\t") + f"\t\t\t<Heading>{float(at(veh.get('head'), i, 0.0)):.3f}</Heading>\n\t\t</SpawnPoint>\n")
    o.append("\t</SpawnPoints>\n")
    start = vec(at(veh.get("loc"), 0, {})) if veh.get("no") else vec(gen.get("start", {}))
    o.append(V("Trigger", start, "\t"))
    o.append("\t<DecorativeProps>\n")
    nprops = 0
    for key, dyn in (("prop", False), ("dprop", True)):
        p = m.get(key, {})
        for i in range(int(p.get("no", 0))):
            h = u32(at(p.get("model"), i, 0))
            if not h: continue
            loc, rot = vec(at(p.get("loc"), i, {})), vec(at(p.get("vRot"), i, {}))
            if abs(loc[0]) >= 16000 or abs(loc[1]) >= 16000: continue
            extra = ""
            clr = at(p.get("prpclr"), i, None)
            if clr is not None and int(clr) > 0: extra += f"<Texture>{int(clr)}</Texture>"
            if h in BOOST: extra += f"<Speed>{int(at(p.get('prpsba'), i, 2) or 2)}</Speed>"
            elif h in SLOW: extra += f"<Speed>-{int(at(p.get('prpsba'), i, 2) or 2)}</Speed>"
            if dyn: extra += "<Dynamic>true</Dynamic>"
            o.append(f"\t\t<SavedProp>{V('Position', loc, '').replace(chr(10), '')}{V('Rotation', rot, '').replace(chr(10), '')}<Hash>{h}</Hash>{extra}</SavedProp>\n")
            nprops += 1
    o.append("\t</DecorativeProps>\n</Race>\n")
    return name, rtype, n, nprops, "".join(o)

def main():
    src, dst = sys.argv[1], sys.argv[2]
    classes = load_classes(sys.argv[3] if len(sys.argv) > 3 else "")
    tf = os.path.join(os.path.dirname(os.path.abspath(src)), "types.txt")
    if os.path.exists(tf):
        for w in open(tf, encoding="utf-8").read().split():
            k, _, v = w.partition(":"); TYPES[k] = v
    os.makedirs(dst, exist_ok=True)
    stats, bad = {}, []
    for f in sorted(os.listdir(src)):
        if not f.endswith(".json"): continue
        cid = f[:-5]
        try:
            j = json.load(open(os.path.join(src, f), encoding="utf-8"))
            name, rtype, n, nprops, xml = convert(j, classes, cid)
        except Exception as e:
            bad.append(f"{f}: {e}"); continue
        if n < 2: bad.append(f"{f}: {n} checkpoints"); continue
        safe = re.sub(r'[<>:"/\\|?*]', "", name).strip() or cid
        open(os.path.join(dst, safe + ".xml"), "w", encoding="utf-8", newline="\n").write(xml)
        stats[rtype] = stats.get(rtype, 0) + 1
    print(f"{sum(stats.values())} races: " + ", ".join(f"{k} {v}" for k, v in sorted(stats.items())))
    for b in bad: print("skipped", b)

if __name__ == "__main__":
    main()
