using System.Globalization;
using System.Text;
using System.Xml.Linq;
using CodeWalker.GameFiles;

// GodEyeMap <GTA V folder> <out.json>: the data GodEye draws its own stylized 2D map from, made locally from the player's own game files
// (never shipped: the repo only has this tool). A coarse terrain grid (metres, water marked), the vehicle road network as
// segments with a class, the junctions that have traffic lights (Phase 10's traffic hacks), and the zone boxes.
static class MapExport
{
    static int Main(string[] args)
    {
        if (args.Length < 2)
        {
            Console.WriteLine("usage: GodEyeMap <GTA V folder> <out map.json>                       the 2D map data");
            Console.WriteLine("       GodEyeMap <GTA V folder> map3d <out dir> [extra dlc.rpf...]    the 3D map tiles");
            Console.WriteLine("       GodEyeMap <GTA V folder> stats3d [extra dlc.rpf...]            what the 3D map would hold");
            return 1;
        }
        GTA5Keys.LoadFromPath(args[0], false, null);
        if (args[1] == "stats3d") return Map3D.Stats(args[0], args.Skip(2).ToArray());
        if (args[1] == "shaders3d") return Map3D.Shaders(args[0], args.Skip(2).ToArray());
        if (args[1] == "findytd") return Map3D.FindYtd(args[0], args[2]);
        if (args[1] == "map3d") return Map3D.Export(args[0], args[2], args.Skip(3).ToArray());
        Run(args[0], args[1]);
        return 0;
    }

    static RpfFile OpenRpf(string path, string rel)
    {
        var rpf = new RpfFile(path, rel);
        rpf.ScanStructure(null, e => Console.Error.WriteLine("rpf error: " + e));
        return rpf;
    }

    static IEnumerable<RpfEntry> AllEntriesDeep(RpfFile rpf)
    {
        if (rpf.AllEntries != null) foreach (var e in rpf.AllEntries) yield return e;
        if (rpf.Children != null) foreach (var c in rpf.Children) foreach (var e in AllEntriesDeep(c)) yield return e;
    }

    // the mainland (Cayo Perico and the empty sea round it left out)
    const float X0 = -4200, Y0 = -4400, X1 = 4600, Y1 = 8400;

    public static void Run(string gta, string outFile)
    {
        var upd = OpenRpf(Path.Combine(gta, "update/update.rpf"), "update/update.rpf");
        var common = OpenRpf(Path.Combine(gta, "common.rpf"), "common.rpf");
        var sb = new StringBuilder();
        var ci = CultureInfo.InvariantCulture;
        sb.Append("{\"version\":1");

        // ---- terrain: heightmap.dat (max height per cell) + water.xml (lakes above sea level)
        var hmEntry = AllEntriesDeep(upd).OfType<RpfFileEntry>().First(e => e.Path.EndsWith("\\levels\\gta5\\heightmap.dat", StringComparison.OrdinalIgnoreCase));
        var hm = new HeightmapFile(); hm.Load(hmEntry.File.ExtractFile(hmEntry), hmEntry);
        var wEntry = AllEntriesDeep(common).OfType<RpfFileEntry>().First(e => e.Path.EndsWith("\\levels\\gta5\\water.xml", StringComparison.OrdinalIgnoreCase));
        var wx = XDocument.Parse(Encoding.UTF8.GetString(wEntry.File.ExtractFile(wEntry)).TrimStart('﻿'));
        float V(XElement it, string n) => float.Parse(it.Element(n)!.Attribute("value")!.Value, ci);
        var quads = wx.Descendants("WaterQuads").Elements("Item").Select(it => (x0: V(it, "minX"), x1: V(it, "maxX"), y0: V(it, "minY"), y1: V(it, "maxY"), z: V(it, "z"))).ToList();
        int W = hm.Width, H = hm.Height;
        float cx = (hm.BBMax.X - hm.BBMin.X) / W, cy = (hm.BBMax.Y - hm.BBMin.Y) / H, zr = (hm.BBMax.Z - hm.BBMin.Z) / 255f;
        Console.WriteLine($"heightmap {W}x{H}, cells {cx:0.0}x{cy:0.0} m, bbox {hm.BBMin} .. {hm.BBMax}; {quads.Count} water quads");
        // resample on a square 25 m grid over the mainland box
        const float C = 25;
        int gw = (int)((X1 - X0) / C), gh = (int)((Y1 - Y0) / C);
        var cells = new int[gw * gh];
        var lows = new int[gw * gh];       // the lowest point of each cell: the 3D view's backdrop under the models
        var waterZ = new float[gw * gh];   // the water level over water cells (0 sea, ~30 the Alamo Sea...)
        for (int j = 0; j < gh; j++)
            for (int i = 0; i < gw; i++)
            {
                float x = X0 + (i + 0.5f) * C, y = Y0 + (j + 0.5f) * C;
                int hx = (int)((x - hm.BBMin.X) / cx), hy = (int)((y - hm.BBMin.Y) / cy);
                float z = -50, zmin = -50;   // outside the heightmap: open sea
                if (hx >= 0 && hy >= 0 && hx < W && hy < H)
                {
                    byte b = hm.MaxHeights[hy * W + hx], bm = hm.MinHeights[hy * W + hx];
                    if (b > 0) z = hm.BBMin.Z + b * zr;
                    if (bm > 0) zmin = hm.BBMin.Z + bm * zr;
                }
                bool water = z < 0.3f || (zmin < 0.3f && z <= zr + 0.3f);   // the sea, and shallows one step above it
                // a lake: under one of water.xml's raised quads, the cell's lowest point below the surface and its highest
                // within one heightmap step (3.2 m) of it
                if (!water)
                    foreach (var q in quads)
                        if (q.z > 0.5f && x >= q.x0 && x <= q.x1 && y >= q.y0 && y <= q.y1 && zmin < q.z && z <= q.z + zr + 0.3f) { water = true; waterZ[j * gw + i] = q.z; break; }
                cells[j * gw + i] = water ? -1 : Math.Max(0, (int)MathF.Round(z));
                lows[j * gw + i] = water ? -1 : Math.Max(0, (int)MathF.Floor(Math.Max(zmin, 0)));
            }
        // specks of seabed poking above sea level offshore: land patches under 40 cells (2.5 hectares) become sea
        var comp = new int[gw * gh];
        for (int s0 = 0; s0 < gw * gh; s0++)
        {
            if (cells[s0] < 0 || comp[s0] != 0) continue;
            var stack = new Stack<int>(); var members = new List<int>(); stack.Push(s0); comp[s0] = 1;
            while (stack.Count > 0)
            {
                int c = stack.Pop(); members.Add(c);
                int ci0 = c % gw, cj0 = c / gw;
                foreach (var (di, dj) in new[] { (1, 0), (-1, 0), (0, 1), (0, -1) })
                {
                    int ni = ci0 + di, nj = cj0 + dj;
                    if (ni < 0 || nj < 0 || ni >= gw || nj >= gh) continue;
                    int nc = nj * gw + ni;
                    if (cells[nc] >= 0 && comp[nc] == 0) { comp[nc] = 1; stack.Push(nc); }
                }
            }
            if (members.Count < 40) foreach (var c in members) cells[c] = -1;
            }
        sb.Append($",\"grid\":{{\"x0\":{X0},\"y0\":{Y0},\"cell\":{C},\"w\":{gw},\"h\":{gh},\"z\":[");
        sb.Append(string.Join(",", cells));
        sb.Append("],\"zmin\":[").Append(string.Join(",", lows));
        sb.Append("]}");

        // ---- roads: every vehicle node link once, in metres; class 3 motorway, 2 main road, 1 street, 0 dirt track
        var ynds = new Dictionary<int, YndFile>();
        foreach (var e in AllEntriesDeep(upd).OfType<RpfFileEntry>().Where(e => e.Path.Contains("\\paths.rpf\\", StringComparison.OrdinalIgnoreCase) && e.NameLower.StartsWith("nodes") && e.NameLower.EndsWith(".ynd")))
        {
            try { var y = RpfFile.GetFile<YndFile>(e); if (y?.Nodes != null) ynds[y.AreaID] = y; } catch (Exception ex) { Console.WriteLine($"{e.Name}: {ex.Message}"); }
        }
        bool In(SharpDX.Vector3 p) => p.X >= X0 && p.X <= X1 && p.Y >= Y0 && p.Y <= Y1;
        bool Boat(SharpDX.Vector3 p)   // a node on a water cell, near the water's surface (a bridge's nodes are high above it)
        {
            int c = (int)((p.Y - Y0) / C) * gw + (int)((p.X - X0) / C);
            return c >= 0 && c < cells.Length && cells[c] < 0 && p.Z < waterZ[c] + 4;
        }
        var seen = new HashSet<(int, int, int, int)>();
        var segs = new List<string>();
        var lights = new List<string>();
        int[] perClass = new int[4];
        foreach (var y in ynds.Values)
        {
            var links = y.NodeDictionary?.Links;
            foreach (var n in y.Nodes)
            {
                if (n.IsPedNode || !In(n.Position)) continue;
                if (n.Special == YndNodeSpecialType.TrafficLightJunctionStop)
                    lights.Add($"{n.Position.X.ToString("0", ci)},{n.Position.Y.ToString("0", ci)}");
                if (links == null) continue;
                for (int k = 0; k < n.LinkCount; k++)
                {
                    int li = n.LinkID + k;
                    if (li < 0 || li >= links.Length) continue;
                    var l = links[li];
                    if (!ynds.TryGetValue(l.AreaID, out var y2) || l.NodeID >= y2.Nodes.Length) continue;
                    var m = y2.Nodes[l.NodeID];
                    if (m.IsPedNode || !In(m.Position)) continue;
                    if (Boat(n.Position) && Boat(m.Position)) continue;   // boat routes
                    var key = (n.AreaID, n.NodeID, m.AreaID, m.NodeID);
                    var rev = (m.AreaID, m.NodeID, n.AreaID, n.NodeID);
                    if (seen.Contains(rev) || !seen.Add(key)) continue;
                    int fwd = (l.Flags2.Value >> 5) & 7, back = (l.Flags2.Value >> 2) & 7, lanes = fwd + back;
                    int cls = n.OffRoad || m.OffRoad ? 0 : (n.Highway && m.Highway) || (fwd >= 3 || back >= 3) ? 3
                            : lanes >= 3 || (n.Speed >= YndNodeSpeed.Fast && m.Speed >= YndNodeSpeed.Fast && lanes >= 2) ? 2 : 1;
                    perClass[cls]++;
                    segs.Add(string.Format(ci, "{0:0},{1:0},{2:0},{3:0},{4}", n.Position.X, n.Position.Y, m.Position.X, m.Position.Y, cls));
                }
            }
        }
        Console.WriteLine($"{ynds.Count} ynd areas, {segs.Count} road segments (dirt {perClass[0]}, street {perClass[1]}, main {perClass[2]}, motorway {perClass[3]}), {lights.Count} traffic light stops");
        sb.Append(",\"roads\":[").Append(string.Join(",", segs)).Append(']');
        sb.Append(",\"lights\":[").Append(string.Join(",", lights)).Append(']');
        // ---- zones: popzone.ipl's boxes with their zone code (DOWNT, SANDY...), as GET_NAME_OF_ZONE names them
        var pz = AllEntriesDeep(upd).OfType<RpfFileEntry>().First(e => e.Path.EndsWith("\\levels\\gta5\\popzone.ipl", StringComparison.OrdinalIgnoreCase));
        var zones = new List<string>();
        foreach (var line in Encoding.UTF8.GetString(pz.File.ExtractFile(pz)).Split('\n'))
        {
            var f = line.Split(',').Select(t => t.Trim()).ToArray();
            if (f.Length < 8 || !float.TryParse(f[1], NumberStyles.Float, ci, out _)) continue;
            zones.Add($"[{f[1]},{f[2]},{f[3]},{f[4]},{f[5]},{f[6]},\"{f[7].ToUpperInvariant()}\"]");
        }
        Console.WriteLine($"{zones.Count} zone boxes");
        sb.Append(",\"zones\":[").Append(string.Join(",", zones)).Append(']');
        sb.Append('}');
        File.WriteAllText(outFile, sb.ToString());
        Console.WriteLine($"wrote {outFile} ({new FileInfo(outFile).Length / 1024} KB)");
    }
}
