using System.Text.Json;
using System.Text.RegularExpressions;
using CodeWalker.GameFiles;
using SharpDX;

// What the 3D map leaves out, so that it shows the world as it is played and not every state the files hold:
//  - map files only a script asks for (CMapData flags bit 0) that are not the free-roam state: mission wreckage (train
//    and tanker crashes, the burnt farm), the construction site's mission crane, a yacht at each of its 36 moorings, the
//    aircraft carrier, both versions of a billboard, the reflection proxies of the FIB heist...
//  - entities our mods sank (moved 1000 m down) or hide at run time (a list of model + place);
//  - what the game still draws over ground we rebuilt: its merged far models (SLOD1 to 4) carry a construction site long
//    after its own models are gone. Every triangle of a game model that rises more than a metre over one of our ground
//    models (named in the clearing file) is dropped; holes our ground encloses (a building's core) count as ground.
//  - what a far model still shows of models that are gone (sunk or hidden): a LOD or merged SLOD all of whose children
//    are gone goes with them; one that stands for living models too loses its triangles where the gone ones stood.
// The clearing file, clear.json (in the folder of patched ymaps, or given on the command line):
//   { "clear": ["islaneon_hg_ground", "islaneon_mhc_plaza"], "hide": [[<model hash>, x, y, z], ...] }
static partial class Map3D
{
    static readonly Regex ScriptedKeep = new(
        "^(?:h4_(?!islandx_disc_|beach_party|islandairstrip_doorsopen|islandx_yacht)" +
        "|ap1_03_bbrd_dcl|apa_ch2_04_(?:mansion|props)_original|hei_ch1_(?:06e|06f|09)_(?:mansion|props)_original|hei_ch1_roads_original" +
        "|bh1_47_joshhse_unburnt|bnkheist_apt_norm|canyonriver01_s?lod|cargoship_|cs1_02_cf_offmission|cs3_05_water_grp|cs5_4_trains" +
        "|des_smash2_startimap|des_stilthouse_imapstart|dockcrane1|dt1_05_hc_remove|(?:dt1_17|hw1_02|hw1_emissive|sc1_01|sc1_14)_oldbill" +
        "|facelobby_|fiblobby_|farm_s?lod|farmint_s?lod|ferris_finale_anim|gasstation_ipl_group|id2_14_pre_no_int|methtrailer_grp1" +
        "|pcranecont|po1_08_mapcrane|refit_unload|rc12b_hospitalinterior|shr_int|smboat|sp1_10_real_interior|(?:hei_)?trailerpark[a-e]_grp1|vb_29_day" +
        "|hei_dlc_(?:casino_aircon|windows_casino)|vw_(?:casino_billboard|dlc_casino_door)|m23_1_garage|m24_1_bailoffice|m24_1_legacyfixes" +
        "|m24_2_garment_factory|m25_1_carwash|m26_1_|sf_(?:billboards|musicrooftop|plaque|dlc_fixer)" +
        "|tr_tuner_(?:meetup|shop_(?:burton|mesa|rancho|strawberry))|xm_hatches_terrain|xm3_(?:doc_sign|garage_fix|sum2_fix|warehouse))",
        RegexOptions.Compiled);

    // is this ymap part of the world in free roam? (ours always; the game's unless a script has to ask for it)
    static bool DrawYmap(YmapFile y, string name, bool ours) => ours || (y._CMapData.flags & 1) == 0 || ScriptedKeep.IsMatch(name);

    sealed class Clearing
    {
        public const float Cell = 2f, Over = 1f, Sunk = -300f;
        public HashSet<uint> Ground = new();                      // our ground models
        public Dictionary<uint, List<Vector3>> Hide = new();      // game models our mods hide, where
        public Dictionary<(int, int), float> Cells = new();       // the 2 m cells our ground covers or encloses: its height
        public float X0 = float.MaxValue, Y0 = float.MaxValue, X1 = float.MinValue, Y1 = float.MinValue;
        public bool Any => Cells.Count > 0;
        public bool Hidden(uint arch, Vector3 p) => Hide.TryGetValue(arch, out var l) && l.Any(h => Vector3.DistanceSquared(h, p) < 6.25f);
        public bool Near(Vector3 mn, Vector3 mx) => mx.X >= X0 && mn.X <= X1 && mx.Y >= Y0 && mn.Y <= Y1;
        public static (int, int) CellOf(float x, float y) => ((int)MathF.Floor(x / Cell), (int)MathF.Floor(y / Cell));
        // a game triangle (lowered by `lift` for an underlay) standing over our ground
        public bool Drops(Vector3 a, Vector3 b, Vector3 c, float lift) =>
            Cells.TryGetValue(CellOf((a.X + b.X + c.X) / 3, (a.Y + b.Y + c.Y) / 3), out float z) && Math.Max(a.Z, Math.Max(b.Z, c.Z)) + lift > z + Over;

        // Where the models that are gone stood: per 4 m column, from their lowest to their highest point. A model that
        // stands for a gone one too (its LOD, the merged SLOD of its block) is drawn without what it has in those columns
        // (above the gone model's foot: the ground under it stays).
        public const float SunkBy = 1000f, Col = 4f;
        public Dictionary<(int, int), (float lo, float hi)> Gone = new();
        static (int, int) ColOf(float x, float y) => ((int)MathF.Floor(x / Col), (int)MathF.Floor(y / Col));
        public void Stood(Vector3 a, Vector3 b, Vector3 c)
        {
            float e = MathF.Max(Vector3.Distance(a, b), MathF.Max(Vector3.Distance(b, c), Vector3.Distance(c, a)));
            int n = Math.Clamp((int)MathF.Ceiling(e / 2f), 1, 400);   // a point every 2 m at most
            for (int i = 0; i <= n; i++)
                for (int j = 0; i + j <= n; j++)
                {
                    var p = a + (b - a) * ((float)i / n) + (c - a) * ((float)j / n);
                    var (ci, cj) = ColOf(p.X, p.Y);
                    for (int di = -1; di <= 1; di++)
                        for (int dj = -1; dj <= 1; dj++)   // and the columns next to it: a far model is not exact
                            Gone[(ci + di, cj + dj)] = Gone.TryGetValue((ci + di, cj + dj), out var r) ? (Math.Min(r.lo, p.Z), Math.Max(r.hi, p.Z)) : (p.Z, p.Z);
                }
        }
        public bool WasThere(Vector3 a, Vector3 b, Vector3 c, float lift)
        {
            float z = (a.Z + b.Z + c.Z) / 3 + lift;
            return Gone.TryGetValue(ColOf((a.X + b.X + c.X) / 3, (a.Y + b.Y + c.Y) / 3), out var r) && z > r.lo + 0.5f && z < r.hi + 1.5f;
        }
    }

    static Clearing LoadClearing(Index ix, Dictionary<uint, (string name, YmapFile y)> ymaps, Dictionary<uint, List<Mesh>> meshes,
                                 Dictionary<string, (uint, uint, DrawableBase)> textures)
    {
        var c = new Clearing();
        if (ix.ClearFile == null) return c;
        using (var doc = JsonDocument.Parse(File.ReadAllText(ix.ClearFile)))
        {
            if (doc.RootElement.TryGetProperty("clear", out var g))
                foreach (var n in g.EnumerateArray()) c.Ground.Add(JenkHash.GenHash(n.GetString()!.ToLowerInvariant()));
            if (doc.RootElement.TryGetProperty("hide", out var hl))
                foreach (var h in hl.EnumerateArray())
                {
                    uint hash = h[0].GetUInt32();
                    (c.Hide.TryGetValue(hash, out var l) ? l : c.Hide[hash] = new()).Add(new Vector3(h[1].GetSingle(), h[2].GetSingle(), h[3].GetSingle()));
                }
        }
        foreach (var (_, (name, y)) in ymaps)
        {
            if (!ix.Ours.Contains(name)) continue;
            foreach (var en in y.AllEntities)
            {
                uint arch = en._CEntityDef.archetypeName;
                if (!c.Ground.Contains(arch)) continue;
                if (!meshes.TryGetValue(arch, out var ms)) meshes[arch] = ms = LoadMeshes(ix, arch, textures);
                if (ms == null) continue;
                // the cells under this model's triangles, with the surface's height at each cell's middle
                var cells = new Dictionary<(int, int), float>();
                var q = en.Orientation; var s = en.Scale; var pos = en.Position;
                foreach (var m in ms)
                    for (int k = 0; k + 2 < m.I.Length; k += 3)
                    {
                        Vector3 a = pos + Vector3.Transform(m.P[m.I[k]] * s, q), b = pos + Vector3.Transform(m.P[m.I[k + 1]] * s, q), d = pos + Vector3.Transform(m.P[m.I[k + 2]] * s, q);
                        float den = (b.Y - d.Y) * (a.X - d.X) + (d.X - b.X) * (a.Y - d.Y);
                        if (MathF.Abs(den) < 1e-4f) continue;   // upright: a kerb, a wall
                        var (i0, j0) = Clearing.CellOf(Math.Min(a.X, Math.Min(b.X, d.X)), Math.Min(a.Y, Math.Min(b.Y, d.Y)));
                        var (i1, j1) = Clearing.CellOf(Math.Max(a.X, Math.Max(b.X, d.X)), Math.Max(a.Y, Math.Max(b.Y, d.Y)));
                        for (int i = i0; i <= i1; i++)
                            for (int j = j0; j <= j1; j++)
                            {
                                float px = (i + 0.5f) * Clearing.Cell, py = (j + 0.5f) * Clearing.Cell;
                                float w0 = ((b.Y - d.Y) * (px - d.X) + (d.X - b.X) * (py - d.Y)) / den, w1 = ((d.Y - a.Y) * (px - d.X) + (a.X - d.X) * (py - d.Y)) / den, w2 = 1 - w0 - w1;
                                if (w0 < -0.02f || w1 < -0.02f || w2 < -0.02f) continue;
                                float z = w0 * a.Z + w1 * b.Z + w2 * d.Z;
                                if (!cells.TryGetValue((i, j), out float had) || z > had) cells[(i, j)] = z;
                            }
                    }
                if (cells.Count == 0) continue;
                // holes it encloses (the cores of the buildings standing on it): not reachable from outside its box
                int a0 = cells.Keys.Min(k => k.Item1) - 1, a1 = cells.Keys.Max(k => k.Item1) + 1, b0 = cells.Keys.Min(k => k.Item2) - 1, b1 = cells.Keys.Max(k => k.Item2) + 1;
                var outside = new HashSet<(int, int)>();
                var todo = new Queue<(int, int)>();
                void Out(int i, int j) { if (i < a0 || i > a1 || j < b0 || j > b1 || cells.ContainsKey((i, j)) || !outside.Add((i, j))) return; todo.Enqueue((i, j)); }
                for (int i = a0; i <= a1; i++) { Out(i, b0); Out(i, b1); }
                for (int j = b0; j <= b1; j++) { Out(a0, j); Out(a1, j); }
                while (todo.Count > 0) { var (i, j) = todo.Dequeue(); Out(i + 1, j); Out(i - 1, j); Out(i, j + 1); Out(i, j - 1); }
                int covered = cells.Count, holes = 0;
                var edge = new Queue<(int, int)>(cells.Keys);
                while (edge.Count > 0)
                {   // each hole cell takes the height of the ground next to it
                    var (i, j) = edge.Dequeue();
                    float z = cells[(i, j)];
                    foreach (var (di, dj) in new[] { (1, 0), (-1, 0), (0, 1), (0, -1) })
                    {
                        var n = (i + di, j + dj);
                        if (n.Item1 < a0 || n.Item1 > a1 || n.Item2 < b0 || n.Item2 > b1 || cells.ContainsKey(n) || outside.Contains(n)) continue;
                        cells[n] = z; holes++; edge.Enqueue(n);
                    }
                }
                foreach (var (k, z) in cells)
                {
                    if (!c.Cells.TryGetValue(k, out float had) || z > had) c.Cells[k] = z;
                    c.X0 = Math.Min(c.X0, k.Item1 * Clearing.Cell); c.X1 = Math.Max(c.X1, (k.Item1 + 1) * Clearing.Cell);
                    c.Y0 = Math.Min(c.Y0, k.Item2 * Clearing.Cell); c.Y1 = Math.Max(c.Y1, (k.Item2 + 1) * Clearing.Cell);
                }
                Console.WriteLine($"cleared ground {JenkIndex.GetString(arch)} ({name}): {covered} cells of {Clearing.Cell} m covered, {holes} enclosed, heights {cells.Values.Min():0.0} to {cells.Values.Max():0.0}");
            }
        }
        Console.WriteLine($"clearing: {c.Ground.Count} ground models named, {c.Cells.Count} cells; {c.Hide.Values.Sum(l => l.Count)} hidden game models listed");
        return c;
    }
}
