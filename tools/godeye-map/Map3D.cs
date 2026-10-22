using SharpDX;
using CodeWalker.GameFiles;

// The real map in 3D for GodEye, from the player's own game files: the game's LOD models (the ones GTA draws in the
// distance) cover every building and hill at a polygon count a browser can draw, plus Cayo Perico and our own
// island DLCs. Never shipped: GodEye serves what this writes into godeye/data/map3d on the player's own PC.
static partial class Map3D
{
    // every RPF the game would read: base, update, DLC packs, and mods/ copies first (like the game does)
    static List<string> GameRpfs(string gta)
    {
        string Pick(string rel) => File.Exists(Path.Combine(gta, "mods", rel)) ? Path.Combine("mods", rel) : rel;
        var rels = Directory.GetFiles(gta, "x64*.rpf").Select(Path.GetFileName).OrderBy(n => n).Select(n => Pick(n!)).ToList();
        rels.Add(Pick("update/update.rpf"));
        var seen = new HashSet<string>();
        foreach (var root in new[] { Path.Combine(gta, "update", "x64", "dlcpacks"), Path.Combine(gta, "mods", "update", "x64", "dlcpacks") })
            if (Directory.Exists(root))
                foreach (var f in Directory.GetFiles(root, "*.rpf", SearchOption.AllDirectories).OrderBy(f => f))
                {
                    var rel = Path.GetRelativePath(gta, f).Replace('\\', '/');
                    if (rel.StartsWith("mods/")) rel = rel.Substring(5);
                    if (seen.Add(rel)) rels.Add(Pick(rel));
                }
        return rels;
    }

    static IEnumerable<RpfEntry> AllEntriesDeep(RpfFile rpf)
    {
        if (rpf.AllEntries != null) foreach (var e in rpf.AllEntries) yield return e;
        if (rpf.Children != null) foreach (var c in rpf.Children) foreach (var e in AllEntriesDeep(c)) yield return e;
    }

    // what the game files hold: ymaps (later packs replace earlier ones), drawables, texture dictionaries, archetypes
    class Index
    {
        public Dictionary<string, RpfFileEntry> Ymaps = new();
        public Dictionary<uint, RpfFileEntry> Ydrs = new(), Ydds = new(), Yfts = new(), Ytds = new();
        public Dictionary<uint, Archetype> Archetypes = new();
        public Dictionary<uint, uint> TxdParents = new();   // gtxd.meta: a texture dictionary's parent
        public HashSet<string> Ours = new();                 // ymaps from the extra packs (our islands): drawn at full detail
        public Dictionary<string, string> Loose = new();     // loose .ymap files that replace the game's (patched copies)
    }

    static Index Scan(string gta, IEnumerable<string> extraRpfs)
    {
        var ix = new Index();
        var ytyps = new List<RpfFileEntry>();
        void Add(RpfFile rpf, bool ours = false)
        {
            foreach (var e in AllEntriesDeep(rpf))
            {
                if (e is not RpfFileEntry fe) continue;
                string n = fe.NameLower;
                uint h() => JenkHash.GenHash(Path.GetFileNameWithoutExtension(n));
                if (n.EndsWith(".ymap"))
                {   // update.rpf's dlc_patch\<pack> copies replace the packs' own, as in the game
                    if (ix.Ymaps.TryGetValue(n, out var had) && had.Path.Contains("dlc_patch") && !fe.Path.Contains("dlc_patch")) continue;
                    ix.Ymaps[n] = fe;
                    if (ours) ix.Ours.Add(n);
                }
                else if (n.EndsWith(".ydr")) ix.Ydrs[h()] = fe;
                else if (n.EndsWith(".ydd")) ix.Ydds[h()] = fe;
                else if (n.EndsWith(".yft") && !n.EndsWith("_hi.yft")) ix.Yfts[h()] = fe;
                else if (n.EndsWith(".ytd")) ix.Ytds[h()] = fe;
                else if (n.EndsWith(".ytyp")) ytyps.Add(fe);
                else if (n == "gtxd.meta" || n == "gtxd.ymt") ReadGtxd(fe, ix);
            }
        }
        foreach (var rel in GameRpfs(gta))
        {
            try { var rpf = new RpfFile(Path.Combine(gta, rel), rel); rpf.ScanStructure(null, null); Add(rpf); }
            catch (Exception ex) { Console.Error.WriteLine(rel + ": " + ex.Message); }
        }
        foreach (var path in extraRpfs)
        {
            if (Directory.Exists(path))
            {   // patched copies of game ymaps (e.g. pulled from a PC whose mods/ edits them): they win by name
                foreach (var f in Directory.GetFiles(path, "*.ymap")) ix.Loose[Path.GetFileName(f).ToLowerInvariant()] = f;
                Console.WriteLine($"loose ymaps {path}: {ix.Loose.Count}");
                continue;
            }
            try { var rpf = new RpfFile(path, Path.GetFileName(path)); rpf.ScanStructure(null, null); Add(rpf, true); Console.WriteLine($"extra pack {path}"); }
            catch (Exception ex) { Console.Error.WriteLine(path + ": " + ex.Message); }
        }
        foreach (var k in ix.Ymaps.Keys.Where(k => ix.Ymaps.ContainsKey("hei_" + k)).ToList()) ix.Ymaps.Remove(k);   // the heist pack's copies replace these
        foreach (var fe in ytyps)
            try { var yt = RpfFile.GetFile<YtypFile>(fe); if (yt?.AllArchetypes != null) foreach (var a in yt.AllArchetypes) ix.Archetypes[a.Hash] = a; }
            catch { }
        Console.WriteLine($"{ix.Ymaps.Count} ymaps, {ix.Archetypes.Count} archetypes, {ix.Ydrs.Count} ydr, {ix.Ydds.Count} ydd, {ix.Yfts.Count} yft, {ix.Ytds.Count} ytd, {ix.TxdParents.Count} txd parents");
        return ix;
    }

    static void ReadGtxd(RpfFileEntry fe, Index ix)
    {
        try
        {
            var data = fe.File.ExtractFile(fe);
            var gtxd = new GtxdFile(); gtxd.Load(data, fe);
            if (gtxd.TxdRelationships != null) foreach (var kv in gtxd.TxdRelationships) ix.TxdParents[JenkHash.GenHash(kv.Key.ToLowerInvariant())] = JenkHash.GenHash(kv.Value.ToLowerInvariant());
        }
        catch { }
    }

    public static int Stats(string gta, string[] extra)
    {
        var ix = Scan(gta, extra);
        if (Environment.GetEnvironmentVariable("STATS_FAIL") == "1")
        {   // the ymaps that do not load, and the ones with no entities
            int bad = 0, empty = 0;
            foreach (var (name, fe) in ix.Ymaps.OrderBy(k => k.Key))
            {
                try
                {
                    var y = RpfFile.GetFile<YmapFile>(fe);
                    if (y?.AllEntities == null) { empty++; if (name.Contains("lod")) Console.WriteLine($"EMPTY {name}"); }
                }
                catch (Exception ex) { bad++; Console.WriteLine($"FAIL {name}: {ex.GetType().Name} {ex.Message}"); }
            }
            Console.WriteLine($"{bad} fail, {empty} empty");
            return 0;
        }
        var box = Environment.GetEnvironmentVariable("STATS_BOX");   // "x0,y0,x1,y1": list the ymaps reaching into it
        if (box != null)
        {
            var b = box.Split(',').Select(v => float.Parse(v, System.Globalization.CultureInfo.InvariantCulture)).ToArray();
            foreach (var (name, fe) in ix.Ymaps.OrderBy(k => k.Key))
            {
                YmapFile y;
                try { y = RpfFile.GetFile<YmapFile>(fe); } catch { continue; }
                if (y?.AllEntities == null) continue;
                var mn = y._CMapData.entitiesExtentsMin; var mx = y._CMapData.entitiesExtentsMax;
                if (mx.X < b[0] || mn.X > b[2] || mx.Y < b[1] || mn.Y > b[3]) continue;
                var lv = string.Join(",", y.AllEntities.GroupBy(e => e._CEntityDef.lodLevel).Select(g => $"{g.Key.ToString().Replace("LODTYPES_DEPTH_", "")}:{g.Count()}"));
                Console.WriteLine($"{name}  ({mn.X:0},{mn.Y:0})-({mx.X:0},{mx.Y:0})  {lv}");
            }
            return 0;
        }
        var count = new Dictionary<rage__eLodType, int>();
        var unique = new Dictionary<rage__eLodType, HashSet<uint>>();
        var islands = new Dictionary<string, int>();
        foreach (var (name, fe) in ix.Ymaps)
        {
            YmapFile y;
            try { y = RpfFile.GetFile<YmapFile>(fe); } catch { continue; }
            if (y?.AllEntities == null) continue;
            foreach (var en in y.AllEntities)
            {
                var lod = en._CEntityDef.lodLevel;
                count[lod] = count.GetValueOrDefault(lod) + 1;
                (unique.TryGetValue(lod, out var s) ? s : unique[lod] = new()).Add(en._CEntityDef.archetypeName);
                if (name.StartsWith("h4_")) islands["cayo " + lod] = islands.GetValueOrDefault("cayo " + lod) + 1;
                if (name.StartsWith("islaneon")) islands["islaneon " + lod] = islands.GetValueOrDefault("islaneon " + lod) + 1;
            }
        }
        foreach (var kv in count.OrderBy(k => k.Key))
        {
            var u = unique[kv.Key];
            long tris = 0; int found = 0, sampled = 0;
            foreach (var a in u.Take(400))
            {
                sampled++;
                var d = LoadDrawable(ix, a);
                if (d == null) continue;
                found++;
                foreach (var m in d.DrawableModels?.High ?? Array.Empty<DrawableModel>()) foreach (var g in m.Geometries) tris += (g.IndexBuffer?.Indices?.Length ?? 0) / 3;
            }
            double avg = found > 0 ? (double)tris / found : 0;
            Console.WriteLine($"{kv.Key,-24} {kv.Value,8} entities  {u.Count,7} archetypes  ~{avg:0} tris/model ({found}/{sampled} sampled)  ~{avg * u.Count / 1e6:0.0} M tris unique");
        }
        foreach (var kv in islands.OrderBy(k => k.Key)) Console.WriteLine($"  {kv.Key}: {kv.Value}");
        return 0;
    }

    static DrawableBase LoadDrawable(Index ix, uint archHash)
    {
        try
        {
            ix.Archetypes.TryGetValue(archHash, out var arch);
            uint asset = arch?._BaseArchetypeDef.assetName ?? archHash;
            if (ix.Ydrs.TryGetValue(asset, out var fe)) return RpfFile.GetFile<YdrFile>(fe)?.Drawable;
            if (ix.Yfts.TryGetValue(asset, out fe)) return RpfFile.GetFile<YftFile>(fe)?.Fragment?.Drawable;
            uint dd = arch?.DrawableDict ?? 0;
            if (dd != 0 && ix.Ydds.TryGetValue(dd, out fe))
            {
                var ydd = RpfFile.GetFile<YddFile>(fe);
                if (ydd?.Dict != null && ydd.Dict.TryGetValue(asset, out var dr)) return dr;
            }
        }
        catch { }
        return null;
    }
}
