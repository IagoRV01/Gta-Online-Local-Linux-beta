using CodeWalker.GameFiles;
using SharpDX;

// map3d diagnostics: GodEyeMap <gta> shaders3d <minX> <minY> <maxX> <maxY> [extra dlc.rpf...]
// For the LOD and SLOD entities in that box: which shaders their models use, how far their vertices reach from the
// entity, the vertex layouts, and one tree billboard dumped vertex by vertex.
static partial class Map3D
{
    public static int Shaders(string gta, string[] args)
    {
        float[] b = args.Take(4).Select(float.Parse).ToArray();
        var ix = Scan(gta, args.Skip(4));
        var shaders = new Dictionary<string, (int n, int verts)>();
        var layouts = new Dictionary<string, int>();
        var reach = new SortedDictionary<int, int>();
        bool dumped = false; int shownEm = 0;
        var seen = new HashSet<uint>();
        foreach (var (name, fe) in ix.Ymaps)
        {
            YmapFile y;
            try { y = ix.Loose.TryGetValue(name, out var loose) ? Load(loose) : RpfFile.GetFile<YmapFile>(fe); } catch { continue; }
            if (y?.AllEntities == null) continue;
            foreach (var en in y.AllEntities)
            {
                var lod = en._CEntityDef.lodLevel;
                if (lod == rage__eLodType.LODTYPES_DEPTH_HD || lod == rage__eLodType.LODTYPES_DEPTH_ORPHANHD) continue;
                var p = en.Position;
                if (p.X < b[0] || p.X > b[2] || p.Y < b[1] || p.Y > b[3]) continue;
                uint arch = en._CEntityDef.archetypeName;
                var d = LoadDrawable(ix, arch);
                if (d?.DrawableModels?.High == null) continue;
                float far = 0;
                foreach (var model in d.DrawableModels.High)
                    foreach (var g in model.Geometries ?? Array.Empty<DrawableGeometry>())
                    {
                        var vd = g.VertexData;
                        if (vd?.VertexBytes == null || vd.Info == null) continue;
                        string sh = ShaderName(g.Shader);
                        string key = $"{lod.ToString().Replace("LODTYPES_DEPTH_", "")} {sh}";
                        var c = shaders.GetValueOrDefault(key);
                        shaders[key] = (c.n + 1, c.verts + vd.VertexCount);
                        string lay = $"{sh}: flags {vd.Info.Flags:x} types {(ulong)vd.Info.Types:x} stride {vd.VertexStride}";
                        layouts[lay] = layouts.GetValueOrDefault(lay) + 1;
                        for (int i = 0; i < vd.VertexCount; i++)
                        {
                            int o = i * vd.VertexStride;
                            var v = new Vector3(BitConverter.ToSingle(vd.VertexBytes, o), BitConverter.ToSingle(vd.VertexBytes, o + 4), BitConverter.ToSingle(vd.VertexBytes, o + 8));
                            if (float.IsFinite(v.X)) far = Math.Max(far, (v * en.Scale).Length());
                        }
                        if (!dumped && sh.Contains("trees_lod") && seen.Add(arch))
                        {
                            dumped = true;
                            Console.WriteLine($"-- {sh} in {JenkIndex.GetString(arch)}: {vd.VertexCount} verts, {g.IndexBuffer?.Indices?.Length} indices, stride {vd.VertexStride}, flags {vd.Info.Flags:x}, types {(ulong)vd.Info.Types:x}");
                            for (int i = 0; i < Math.Min(8, vd.VertexCount); i++)
                                Console.WriteLine("   " + BitConverter.ToString(vd.VertexBytes, i * vd.VertexStride, vd.VertexStride));
                            Console.WriteLine("   indices: " + string.Join(",", (g.IndexBuffer?.Indices ?? Array.Empty<ushort>()).Take(12)));
                            var pl = g.Shader?.ParametersList;
                            if (pl?.Parameters != null)
                                for (int i = 0; i < pl.Parameters.Length && i < pl.Hashes.Length; i++)
                                    Console.WriteLine($"   param {JenkIndex.GetString((uint)pl.Hashes[i])}: {(pl.Parameters[i].Data is TextureBase tb ? "tex " + tb.Name : pl.Parameters[i].Data?.ToString())}");
                        }
                    }
                // a few models with light panels: every geometry, its shader, size and textures
                if (shownEm < 6 && d.DrawableModels.High.SelectMany(m => m.Geometries ?? Array.Empty<DrawableGeometry>()).Any(g => ShaderName(g.Shader).Contains("emissive")) && seen.Add(arch))
                {
                    shownEm++;
                    Console.WriteLine($"-- {JenkIndex.GetString(arch)} ({lod.ToString().Replace("LODTYPES_DEPTH_", "")})");
                    foreach (var g in d.DrawableModels.High.SelectMany(m => m.Geometries ?? Array.Empty<DrawableGeometry>()))
                    {
                        var pl = g.Shader?.ParametersList;
                        var texs = new List<string>();
                        if (pl?.Parameters != null)
                            for (int i = 0; i < pl.Parameters.Length && i < pl.Hashes.Length; i++)
                                if (pl.Parameters[i].Data is TextureBase tb) texs.Add($"{JenkIndex.GetString((uint)pl.Hashes[i])}={tb.Name ?? tb.NameHash.ToString("x8")}");
                        Console.WriteLine($"   {ShaderName(g.Shader),-26} {g.VertexData?.VertexCount,6} verts  {string.Join("  ", texs)}");
                    }
                }
                int bucket = far < 64 ? 64 : far < 128 ? 128 : far < 256 ? 256 : far < 512 ? 512 : far < 1024 ? 1024 : far < 2048 ? 2048 : 99999;
                reach[bucket] = reach.GetValueOrDefault(bucket) + 1;
            }
        }
        Console.WriteLine("-- shaders (level shader: geometries, vertices)");
        foreach (var kv in shaders.OrderByDescending(k => k.Value.verts).Take(45)) Console.WriteLine($"   {kv.Key,-40} {kv.Value.n,6} {kv.Value.verts,9}");
        Console.WriteLine("-- how far an entity's vertices reach (m): entities");
        foreach (var kv in reach) Console.WriteLine($"   < {kv.Key,5}: {kv.Value}");
        Console.WriteLine("-- vertex layouts");
        foreach (var kv in layouts.OrderByDescending(k => k.Value).Take(25)) Console.WriteLine($"   {kv.Value,6}  {kv.Key}");
        return 0;
    }

    static YmapFile Load(string path) { var y = new YmapFile(); y.Load(File.ReadAllBytes(path)); return y; }
}

// GodEyeMap <gta> findytd <name part>: every texture dictionary whose path contains that text, with its textures
static partial class Map3D
{
    public static int FindYtd(string gta, string part)
    {
        int n = 0;
        foreach (var rel in GameRpfs(gta))
        {
            RpfFile rpf;
            try { rpf = new RpfFile(Path.Combine(gta, rel), rel); rpf.ScanStructure(null, null); } catch { continue; }
            foreach (var e in AllEntriesDeep(rpf).OfType<RpfFileEntry>())
            {
                if (!e.Path.EndsWith(".ytd", StringComparison.OrdinalIgnoreCase) || e.Path.IndexOf(part, StringComparison.OrdinalIgnoreCase) < 0) continue;
                if (++n > 400) continue;
                string texs = "";
                try
                {
                    var y = RpfFile.GetFile<YtdFile>(e);
                    texs = string.Join(" ", (y?.TextureDict?.Textures?.data_items ?? Array.Empty<Texture>()).Take(12).Select(t => $"{t.Name}:{t.Width}x{t.Height}:{t.Format}"));
                }
                catch (Exception ex) { texs = "(" + ex.Message + ")"; }
                Console.WriteLine($"{e.Path}  {texs}");
            }
        }
        Console.WriteLine($"{n} dictionaries");
        return 0;
    }

    // GodEyeMap <gta> ents3d <minX> <minY> <maxX> <maxY> [extra dlc.rpf or loose ymap dir...]: every entity standing in the
    // box, any LOD level, with its ymap, position and LOD distance (to find what is left of a cleared site)
    public static int Ents(string gta, string[] args)
    {
        float[] b = args.Take(4).Select(float.Parse).ToArray();
        var ix = Scan(gta, args.Skip(4));
        foreach (var (name, fe) in ix.Ymaps.OrderBy(k => k.Key))
        {
            YmapFile y;
            try { y = ix.Loose.TryGetValue(name, out var loose) ? Load(loose) : RpfFile.GetFile<YmapFile>(fe); } catch { continue; }
            if (y?.AllEntities == null) continue;
            foreach (var en in y.AllEntities)
            {
                var p = en.Position;
                if (p.X < b[0] || p.X > b[2] || p.Y < b[1] || p.Y > b[3]) continue;
                var d = en._CEntityDef;
                string arch = ix.Ydrs.TryGetValue(d.archetypeName, out var ydr) ? ydr.NameLower : ix.Yfts.TryGetValue(d.archetypeName, out var yft) ? yft.NameLower : JenkIndex.GetString(d.archetypeName);
                if (ix.Archetypes.TryGetValue(d.archetypeName, out var at))
                {
                    uint dd = at._BaseArchetypeDef.drawableDictionary, td = at._BaseArchetypeDef.textureDictionary;
                    if (dd != 0) arch += " ydd:" + (ix.Ydds.TryGetValue(dd, out var f1) ? f1.NameLower : dd.ToString("x8"));
                    arch += " txd:" + (ix.Ytds.TryGetValue(td, out var f2) ? f2.NameLower : td.ToString("x8"));
                }
                Console.Write($"#{Array.IndexOf(y.AllEntities, en),-4} ");
                Console.WriteLine($"{name,-34} {d.lodLevel.ToString().Replace("LODTYPES_DEPTH_", ""),-9} {arch,-36} {p.X,8:0.0} {p.Y,8:0.0} {p.Z,8:0.0} lod {d.lodDist,6:0} parent {d.parentIndex}{(ix.Loose.ContainsKey(name) ? " (loose)" : "")}");
            }
        }
        return 0;
    }

    // GodEyeMap <gta> nodes3d <minX> <minY> <maxX> <maxY>: the path nodes in the box, every flag property that is set
    public static int Nodes(string gta, string[] args)
    {
        float[] b = args.Take(4).Select(float.Parse).ToArray();
        foreach (var rel in GameRpfs(gta))
        {
            RpfFile rpf;
            try { rpf = new RpfFile(Path.Combine(gta, rel), rel); rpf.ScanStructure(null, null); } catch { continue; }
            foreach (var e in AllEntriesDeep(rpf).OfType<RpfFileEntry>().Where(e => e.NameLower.StartsWith("nodes") && e.NameLower.EndsWith(".ynd")))
            {
                YndFile y;
                try { y = RpfFile.GetFile<YndFile>(e); } catch { continue; }
                if (y?.Nodes == null) continue;
                foreach (var n in y.Nodes)
                {
                    var p = n.Position;
                    if (p.X < b[0] || p.X > b[2] || p.Y < b[1] || p.Y > b[3]) continue;
                    var on = n.GetType().GetProperties().Where(pp => pp.PropertyType == typeof(bool) && pp.GetIndexParameters().Length == 0 && (bool)pp.GetValue(n)!).Select(pp => pp.Name);
                    Console.WriteLine($"{e.Path} {n.AreaID}:{n.NodeID} {p.X:0.0} {p.Y:0.0} {p.Z:0.0} links {n.LinkCount} f {n.Flags0.Value:x2} {n.Flags1.Value:x2} {n.Flags2.Value:x2} {n.Flags3.Value:x2} {n.Flags4.Value:x2} {string.Join(",", on)}");
                }
            }
        }
        return 0;
    }

    // GodEyeMap <gta> kids3d <parent ymap name> <index...> [extra...]: the entities whose LOD parent is one of those entities
    public static int Kids(string gta, string[] args)
    {
        string parent = Path.GetFileNameWithoutExtension(args[0]).ToLowerInvariant();
        var want = args.Skip(1).TakeWhile(a => int.TryParse(a, out _)).Select(int.Parse).ToHashSet();
        var ix = Scan(gta, args.Skip(1 + want.Count));
        uint ph = JenkHash.GenHash(parent);
        foreach (var (name, fe) in ix.Ymaps.OrderBy(k => k.Key))
        {
            YmapFile y;
            try { y = ix.Loose.TryGetValue(name, out var loose) ? Load(loose) : RpfFile.GetFile<YmapFile>(fe); } catch { continue; }
            if (y?.AllEntities == null || y._CMapData.parent != ph) continue;
            foreach (var en in y.AllEntities)
            {
                var d = en._CEntityDef;
                if (!want.Contains(d.parentIndex)) continue;
                string arch = ix.Ydrs.TryGetValue(d.archetypeName, out var ydr) ? ydr.NameLower : JenkIndex.GetString(d.archetypeName);
                Console.WriteLine($"{name,-30} parent {d.parentIndex,-4} {d.lodLevel.ToString().Replace("LODTYPES_DEPTH_", ""),-6} {arch,-36} {en.Position.X,8:0.0} {en.Position.Y,8:0.0} {en.Position.Z,8:0.0} lod {d.lodDist:0}");
            }
        }
        return 0;
    }
}
