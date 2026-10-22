using System.Globalization;
using System.Text;
using System.Text.Json;
using System.Xml.Linq;
using CodeWalker.GameFiles;
using CodeWalker.Utils;
using SharpDX;

// map3d: writes godeye/data/map3d/
//   index.json                  tiles, water, extents
//   t_<x>_<y>.far.bin           every tile: the SLOD1 models (swapped out up close), the LOD models with no SLOD1 above,
//                               and the SLOD2 models 4 m lower (the game's distant hills, which SLOD1 leaves out)
//   t_<x>_<y>.near.bin          up close: the LOD models that replace that tile's SLOD1 ones
//   tex/<hash>.bc1              64 px textures, BC1 (with 1-bit alpha), decoded by the browser
// A tile file: "GEM1", u32 json length, json {origin, scale, batches[]}, vertices (u16 x3 position, f16 x2 uv,
// i8 x4 normal: 14 bytes), u16 indices per batch (each batch is at most 65535 vertices).
static partial class Map3D
{
    const float Tile = 1024;
    const int TexMax = 64;

    sealed class Batch
    {
        public string Tex = "";        // texture hash (hex), "" = untextured
        public bool Alpha;
        public int Layer;              // 0 always (far), 1 far but replaced by the tile's near file, 2 near
        public List<Vector3> P = new(); public List<Vector2> U = new(); public List<Vector3> N = new();
        public List<int> I = new();
    }

    sealed class Mesh { public string Tex = ""; public bool Alpha; public Vector3[] P = Array.Empty<Vector3>(); public Vector2[] U = Array.Empty<Vector2>(); public Vector3[] N = Array.Empty<Vector3>(); public int[] I = Array.Empty<int>(); }

    sealed class TileData
    {
        public Dictionary<(string, bool, int), List<Batch>> Far = new(), Near = new();
        public int Ents;
        public float Z0 = float.MaxValue, Z1 = float.MinValue;
    }

    public static int Export(string gta, string outDir, string[] extra)
    {
        var ix = Scan(gta, extra);
        Directory.CreateDirectory(outDir);
        Directory.CreateDirectory(Path.Combine(outDir, "tex"));

        // ---- all ymaps, linked to their parent ymaps (LOD models live in *_lod ymaps, SLODs above them)
        var ymaps = new Dictionary<uint, (string name, YmapFile y)>();
        foreach (var (name, fe) in ix.Ymaps)
            try
            {
                // North Yankton (the prologue) sits where Cayo Perico is; the game only ever loads one of them
                if (name.StartsWith("prologue") || name.StartsWith("plg_") || name.StartsWith("des_protree")) continue;
                YmapFile y;
                if (ix.Loose.TryGetValue(name, out var loose)) { y = new YmapFile(); y.Load(File.ReadAllBytes(loose)); }
                else y = RpfFile.GetFile<YmapFile>(fe);
                if (y?.AllEntities != null) ymaps[JenkHash.GenHash(Path.GetFileNameWithoutExtension(name))] = (name, y);
            }
            catch { }
        foreach (var (_, (_, y)) in ymaps)
            if (y._CMapData.parent != 0 && ymaps.TryGetValue(y._CMapData.parent, out var p)) y.ConnectToParent(p.y);
        Console.WriteLine($"{ymaps.Count} ymaps loaded");

        // ---- what goes where
        var tiles = new Dictionary<(int, int), TileData>();
        var meshes = new Dictionary<uint, List<Mesh>>();
        var textures = new Dictionary<string, (uint tex, uint txd, DrawableBase d)>();   // tex key -> where to find it
        int nFar = 0, nSlod = 0, nNear = 0, nUnder = 0, nOurs = 0, noModel = 0;
        (int, int) TileOf(Vector3 p) => ((int)MathF.Floor(p.X / Tile), (int)MathF.Floor(p.Y / Tile));
        foreach (var (_, (name, y)) in ymaps)
        {
            bool ours = ix.Ours.Contains(name);
            foreach (var en in y.AllEntities)
            {
                var lod = en._CEntityDef.lodLevel;
                int layer; YmapEntityDef root = en;
                if (lod == rage__eLodType.LODTYPES_DEPTH_SLOD1) layer = 1;
                else if (lod == rage__eLodType.LODTYPES_DEPTH_SLOD2 || lod == rage__eLodType.LODTYPES_DEPTH_SLOD3 || lod == rage__eLodType.LODTYPES_DEPTH_SLOD4) layer = 3;   // underlays: hills the SLOD1 models leave out
                else if (lod == rage__eLodType.LODTYPES_DEPTH_LOD)
                {
                    var a = en.Parent;
                    while (a != null && a._CEntityDef.lodLevel != rage__eLodType.LODTYPES_DEPTH_SLOD1) a = a.Parent;
                    if (a != null) { layer = 2; root = a; } else layer = 0;
                }
                else if (ours && (lod == rage__eLodType.LODTYPES_DEPTH_ORPHANHD || lod == rage__eLodType.LODTYPES_DEPTH_HD) && en.Parent == null) layer = 0;   // our island has no LOD models
                else continue;
                uint arch = en._CEntityDef.archetypeName;
                if (!meshes.TryGetValue(arch, out var ms)) meshes[arch] = ms = LoadMeshes(ix, arch, textures);
                if (ms == null || ms.Count == 0) { noModel++; continue; }
                var key = TileOf(root.Position);
                if (!tiles.TryGetValue(key, out var t)) tiles[key] = t = new TileData();
                t.Ents++;
                if (layer == 1) nSlod++; else if (layer == 2) nNear++; else if (layer == 3) nUnder++; else nFar++;
                if (ours) nOurs++;
                var q = en.Orientation; var s = en.Scale; var pos = en.Position;
                if (layer == 3) pos.Z -= lod == rage__eLodType.LODTYPES_DEPTH_SLOD2 ? 4 : lod == rage__eLodType.LODTYPES_DEPTH_SLOD3 ? 8 : 12;   // each coarser level a little lower
                foreach (var m in ms)
                {
                    var dict = layer == 2 ? t.Near : t.Far;
                    var bk = (m.Tex, m.Alpha, layer);
                    if (!dict.TryGetValue(bk, out var list)) dict[bk] = list = new List<Batch> { new Batch { Tex = m.Tex, Alpha = m.Alpha, Layer = layer } };
                    var b = list[^1];
                    if (b.P.Count + m.P.Length > 65535) list.Add(b = new Batch { Tex = m.Tex, Alpha = m.Alpha, Layer = layer });
                    int baseV = b.P.Count;
                    for (int i = 0; i < m.P.Length; i++)
                    {
                        var w = pos + Vector3.Transform(m.P[i] * s, q);
                        b.P.Add(w); b.U.Add(m.U[i]); b.N.Add(Vector3.Normalize(Vector3.Transform(m.N[i], q)));
                        t.Z0 = Math.Min(t.Z0, w.Z); t.Z1 = Math.Max(t.Z1, w.Z);
                    }
                    foreach (int i in m.I) b.I.Add(baseV + i);
                }
            }
        }
        Console.WriteLine($"placed: {nSlod} SLOD1 + {nUnder} SLOD2-4 underlay + {nFar} always-drawn + {nNear} near LOD models ({nOurs} from our own packs), {noModel} without a model; {tiles.Count} tiles");

        // ---- textures
        int texOk = 0, texFail = 0;
        var texNames = new HashSet<string>();
        foreach (var (key, (tex, txd, d)) in textures)
        {
            var path = Path.Combine(outDir, "tex", key + ".bc1");
            var t = FindTexture(ix, tex, txd, d);
            if (t == null) { texFail++; continue; }
            try { File.WriteAllBytes(path, EncodeTexture(t)); texOk++; texNames.Add(key); }
            catch { texFail++; }
        }
        Console.WriteLine($"textures: {texOk} written, {texFail} not found or not decodable");

        // ---- tiles
        long bytesFar = 0, bytesNear = 0;
        var index = new List<object>();
        foreach (var ((tx, ty), t) in tiles.OrderBy(k => k.Key))
        {
            string bn = $"t_{tx}_{ty}";
            long f = WriteTile(Path.Combine(outDir, bn + ".far.bin"), tx, ty, t, t.Far.Values.SelectMany(v => v), texNames);
            long n = t.Near.Count > 0 ? WriteTile(Path.Combine(outDir, bn + ".near.bin"), tx, ty, t, t.Near.Values.SelectMany(v => v), texNames) : 0;
            bytesFar += f; bytesNear += n;
            index.Add(new { x = tx, y = ty, far = f, near = n, z0 = Math.Round(t.Z0), z1 = Math.Round(t.Z1), ents = t.Ents });
        }

        // ---- water: the sea is a plane at 0; lakes and Cayo's own water from the water files
        var water = new List<float[]>();
        foreach (var (rpfRel, inner) in new[] { ("common.rpf", "\\levels\\gta5\\water.xml"), ("update/update.rpf", "\\levels\\gta5\\water_heistisland.xml") })
        {
            try
            {
                var rpf = new RpfFile(Path.Combine(gta, rpfRel), rpfRel); rpf.ScanStructure(null, null);
                var e = AllEntriesDeep(rpf).OfType<RpfFileEntry>().FirstOrDefault(x => x.Path.EndsWith(inner, StringComparison.OrdinalIgnoreCase));
                if (e == null) continue;
                var xd = XDocument.Parse(Encoding.UTF8.GetString(e.File.ExtractFile(e)).TrimStart('﻿'));
                float V(XElement it, string n) => float.Parse(it.Element(n)!.Attribute("value")!.Value, CultureInfo.InvariantCulture);
                foreach (var it in xd.Descendants("WaterQuads").Elements("Item"))
                    if (V(it, "z") > 0.5f) water.Add(new[] { V(it, "minX"), V(it, "minY"), V(it, "maxX"), V(it, "maxY"), V(it, "z") });
            }
            catch (Exception ex) { Console.Error.WriteLine($"{inner}: {ex.Message}"); }
        }
        // `built` versions the tile and texture URLs, so browsers never mix a new index with cached old tiles
        var idx = new { version = 1, built = DateTime.UtcNow.ToString("yyyyMMddHHmmss"), tile = Tile, tex = TexMax, tiles = index, water };
        File.WriteAllText(Path.Combine(outDir, "index.json"), JsonSerializer.Serialize(idx));
        Console.WriteLine($"wrote {index.Count} tiles: far {bytesFar / 1048576} MB, near {bytesNear / 1048576} MB; {water.Count} raised water quads");
        return 0;
    }

    // ---- geometry: the highest-detail model of the archetype's drawable, in its own space, batched by texture
    static readonly uint[] DiffuseNames = { JenkHash.GenHash("diffusesampler"), JenkHash.GenHash("texturesampler_layer0"), JenkHash.GenHash("texturesampler"), JenkHash.GenHash("diffusesampler2") };

    static List<Mesh> LoadMeshes(Index ix, uint arch, Dictionary<string, (uint, uint, DrawableBase)> textures)
    {
        var d = LoadDrawable(ix, arch);
        if (d?.DrawableModels?.High == null) return null;
        ix.Archetypes.TryGetValue(arch, out var a);
        uint txd = a?.TextureDict ?? 0;
        var list = new List<Mesh>();
        foreach (var model in d.DrawableModels.High)
            foreach (var g in model.Geometries ?? Array.Empty<DrawableGeometry>())
            {
                var vd = g.VertexData; var ixs = g.IndexBuffer?.Indices;
                if (vd?.VertexBytes == null || ixs == null || vd.Info == null) continue;
                string shader = g.Shader != null ? JenkIndex.GetString(g.Shader.Name).ToLowerInvariant() : "";
                if (shader.Contains("shadow") || shader.Contains("mirror")) continue;
                bool isWater = shader.Contains("water");   // rivers, ponds and pools: drawn as water at their own height
                // the texture: the first diffuse-like sampler
                string texKey = ""; bool alpha = shader.Contains("alpha") || shader.Contains("cutout") || shader.Contains("decal") || shader.Contains("trees") || shader.Contains("cable");
                var pl = g.Shader?.ParametersList;
                if (pl?.Parameters != null)
                    foreach (var want in DiffuseNames)
                    {
                        for (int i = 0; i < pl.Parameters.Length && i < pl.Hashes.Length; i++)
                            if ((uint)pl.Hashes[i] == want && pl.Parameters[i].Data is TextureBase tb && tb.NameHash != 0)
                            {
                                texKey = tb.NameHash.ToString("x8");
                                if (!textures.ContainsKey(texKey)) textures[texKey] = (tb.NameHash, txd, d);
                                break;
                            }
                        if (texKey != "") break;
                    }
                // vertex layout: one bit per component in Flags, 4 bits of type per component in Types
                ulong flags = vd.Info.Flags, types = (ulong)vd.Info.Types;
                int stride = vd.VertexStride, off = 0, offN = -1, offT = -1, typeN = 0, typeT = 0;
                for (int c = 0; c < 16; c++)
                {
                    if (((flags >> c) & 1) == 0) continue;
                    var ct = (VertexComponentType)((types >> (c * 4)) & 0xF);
                    if (c == 3) { offN = off; typeN = (int)ct; }
                    if (c == 6) { offT = off; typeT = (int)ct; }
                    off += VertexComponentTypes.GetSizeInBytes(ct);
                }
                int nv = vd.VertexCount;
                var m = new Mesh { Tex = isWater ? "@water" : texKey, Alpha = !isWater && alpha, P = new Vector3[nv], U = new Vector2[nv], N = new Vector3[nv], I = new int[ixs.Length] };
                var vb = vd.VertexBytes;
                for (int i = 0; i < nv; i++)
                {
                    int o = i * stride;
                    m.P[i] = new Vector3(BitConverter.ToSingle(vb, o), BitConverter.ToSingle(vb, o + 4), BitConverter.ToSingle(vb, o + 8));
                    m.N[i] = offN >= 0 ? ReadVec3(vb, o + offN, typeN) : Vector3.UnitZ;
                    m.U[i] = offT >= 0 ? ReadVec2(vb, o + offT, typeT) : Vector2.Zero;
                }
                for (int i = 0; i < ixs.Length; i++) m.I[i] = ixs[i] < nv ? ixs[i] : 0;
                list.Add(m);
            }
        return list;
    }

    static float Half(byte[] b, int o) => (float)BitConverter.ToHalf(b, o);

    static Vector3 ReadVec3(byte[] b, int o, int type) => (VertexComponentType)type switch
    {
        VertexComponentType.Float3 or VertexComponentType.Float4 => new Vector3(BitConverter.ToSingle(b, o), BitConverter.ToSingle(b, o + 4), BitConverter.ToSingle(b, o + 8)),
        VertexComponentType.Half4 => new Vector3(Half(b, o), Half(b, o + 2), Half(b, o + 4)),
        VertexComponentType.RGBA8SNorm => new Vector3((sbyte)b[o] / 127f, (sbyte)b[o + 1] / 127f, (sbyte)b[o + 2] / 127f),
        VertexComponentType.Colour or VertexComponentType.UByte4 => new Vector3(b[o] / 127.5f - 1, b[o + 1] / 127.5f - 1, b[o + 2] / 127.5f - 1),
        _ => Vector3.UnitZ,
    };

    static Vector2 ReadVec2(byte[] b, int o, int type) => (VertexComponentType)type switch
    {
        VertexComponentType.Float2 or VertexComponentType.Float3 or VertexComponentType.Float4 => new Vector2(BitConverter.ToSingle(b, o), BitConverter.ToSingle(b, o + 4)),
        VertexComponentType.Half2 or VertexComponentType.Half4 => new Vector2(Half(b, o), Half(b, o + 2)),
        _ => Vector2.Zero,
    };

    // ---- textures: the drawable's own dictionary, the archetype's, then up the gtxd parents
    static readonly Dictionary<uint, TextureDictionary> TxdCache = new();

    static Texture FindTexture(Index ix, uint tex, uint txd, DrawableBase d)
    {
        var own = d?.ShaderGroup?.TextureDictionary?.Lookup(tex);
        if (own?.Data != null) return own;
        for (int depth = 0; txd != 0 && depth < 8; depth++)
        {
            if (!TxdCache.TryGetValue(txd, out var td))
            {
                td = null;
                if (ix.Ytds.TryGetValue(txd, out var fe)) try { td = RpfFile.GetFile<YtdFile>(fe)?.TextureDict; } catch { }
                TxdCache[txd] = td;
            }
            var t = td?.Lookup(tex);
            if (t?.Data != null) return t;
            txd = ix.TxdParents.TryGetValue(txd, out var p) ? p : 0;
        }
        return null;
    }

    // the mip at most TexMax wide, as BGRA from CodeWalker, then BC1
    static byte[] EncodeTexture(Texture t)
    {
        int mip = 0;
        while (mip + 1 < t.Levels && Math.Max(t.Width >> mip, t.Height >> mip) > TexMax) mip++;
        int w = Math.Max(1, t.Width >> mip), h = Math.Max(1, t.Height >> mip);
        var px = DDSIO.GetPixels(t, mip) ?? throw new Exception("format");
        if (px.Length < w * h * 4) throw new Exception("size");
        int bw = (w + 3) / 4, bh = (h + 3) / 4;
        var outb = new byte[8 + bw * bh * 8];
        BitConverter.GetBytes((ushort)w).CopyTo(outb, 0); BitConverter.GetBytes((ushort)h).CopyTo(outb, 2);
        bool anyAlpha = false;
        var rgb = new (int r, int g, int b, bool a)[16];
        for (int by = 0; by < bh; by++)
            for (int bx = 0; bx < bw; bx++)
            {
                bool blockAlpha = false;
                for (int k = 0; k < 16; k++)
                {
                    int x = Math.Min(w - 1, bx * 4 + (k & 3)), y = Math.Min(h - 1, by * 4 + (k >> 2)), o = (y * w + x) * 4;
                    rgb[k] = (px[o + 2], px[o + 1], px[o], px[o + 3] >= 128);
                    if (!rgb[k].a) blockAlpha = true;
                }
                anyAlpha |= blockAlpha;
                EncodeBlock(rgb, blockAlpha, outb, 8 + (by * bw + bx) * 8);
            }
        outb[4] = (byte)(anyAlpha ? 1 : 0);
        return outb;
    }

    static ushort To565(int r, int g, int b) => (ushort)(((r * 31 + 127) / 255 << 11) | ((g * 63 + 127) / 255 << 5) | ((b * 31 + 127) / 255));
    static (int, int, int) From565(ushort c) => (((c >> 11) & 31) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31);

    // BC1: the two extreme colours of the opaque pixels along their widest channel; 3-colour mode with a transparent
    // index when the block has cut-out pixels
    static void EncodeBlock((int r, int g, int b, bool a)[] px, bool alpha, byte[] o, int at)
    {
        int lo = -1, hi = -1; float lv = float.MaxValue, hv = float.MinValue;
        int mr = 0, mg = 0, mb = 0, cnt = 0;
        foreach (var p in px) if (p.a) { mr += p.r; mg += p.g; mb += p.b; cnt++; }
        if (cnt == 0) { BitConverter.GetBytes((ushort)0).CopyTo(o, at); BitConverter.GetBytes((ushort)0xFFFF).CopyTo(o, at + 2); BitConverter.GetBytes(0xFFFFFFFFu).CopyTo(o, at + 4); return; }
        mr /= cnt; mg /= cnt; mb /= cnt;
        int vr = 0, vg = 0, vb = 0;
        foreach (var p in px) if (p.a) { vr += Math.Abs(p.r - mr); vg += Math.Abs(p.g - mg); vb += Math.Abs(p.b - mb); }
        for (int k = 0; k < 16; k++)
        {
            if (!px[k].a) continue;
            float v = vr >= vg && vr >= vb ? px[k].r : vg >= vb ? px[k].g : px[k].b;
            v += (px[k].r + px[k].g + px[k].b) * 0.01f;
            if (v < lv) { lv = v; lo = k; }
            if (v > hv) { hv = v; hi = k; }
        }
        ushort c0 = To565(px[hi].r, px[hi].g, px[hi].b), c1 = To565(px[lo].r, px[lo].g, px[lo].b);
        if (alpha ? c0 > c1 : c0 < c1) (c0, c1) = (c1, c0);
        if (!alpha && c0 == c1) { if (c1 > 0) c1--; else c0++; }
        var (r0, g0, b0) = From565(c0); var (r1, g1, b1) = From565(c1);
        var pal = alpha
            ? new[] { (r0, g0, b0), (r1, g1, b1), ((r0 + r1) / 2, (g0 + g1) / 2, (b0 + b1) / 2) }
            : new[] { (r0, g0, b0), (r1, g1, b1), ((2 * r0 + r1) / 3, (2 * g0 + g1) / 3, (2 * b0 + b1) / 3), ((r0 + 2 * r1) / 3, (g0 + 2 * g1) / 3, (b0 + 2 * b1) / 3) };
        uint bits = 0;
        for (int k = 0; k < 16; k++)
        {
            int idx;
            if (!px[k].a) idx = 3;
            else
            {
                idx = 0; int best = int.MaxValue;
                for (int c = 0; c < pal.Length; c++)
                {
                    int dr = px[k].r - pal[c].Item1, dg = px[k].g - pal[c].Item2, db = px[k].b - pal[c].Item3, dd = dr * dr * 3 + dg * dg * 4 + db * db * 2;
                    if (dd < best) { best = dd; idx = c; }
                }
            }
            bits |= (uint)idx << (k * 2);
        }
        BitConverter.GetBytes(c0).CopyTo(o, at); BitConverter.GetBytes(c1).CopyTo(o, at + 2); BitConverter.GetBytes(bits).CopyTo(o, at + 4);
    }

    // ---- one tile file
    static long WriteTile(string path, int tx, int ty, TileData t, IEnumerable<Batch> batches, HashSet<string> texNames)
    {
        var list = batches.Where(b => b.I.Count > 0).ToList();
        float ox = tx * Tile - 64, oy = ty * Tile - 64, oz = MathF.Floor(t.Z0) - 1;   // entities can reach past their tile
        float span = Tile + 128, zspan = Math.Max(16, t.Z1 - oz + 2);
        var meta = new List<object>();
        using var vbuf = new MemoryStream(); using var ibuf = new MemoryStream();
        using (var vw = new BinaryWriter(vbuf, Encoding.UTF8, true)) using (var iw = new BinaryWriter(ibuf, Encoding.UTF8, true))
        {
            int vStart = 0, iStart = 0;
            foreach (var b in list)
            {
                for (int i = 0; i < b.P.Count; i++)
                {
                    var p = b.P[i];
                    vw.Write(Q((p.X - ox) / span)); vw.Write(Q((p.Y - oy) / span)); vw.Write(Q((p.Z - oz) / zspan));
                    vw.Write(BitConverter.HalfToUInt16Bits((System.Half)Clamp(b.U[i].X))); vw.Write(BitConverter.HalfToUInt16Bits((System.Half)Clamp(b.U[i].Y)));
                    var n = b.N[i];
                    vw.Write((sbyte)Math.Round(n.X * 127)); vw.Write((sbyte)Math.Round(n.Y * 127)); vw.Write((sbyte)Math.Round(n.Z * 127)); vw.Write((sbyte)0);
                }
                foreach (int i in b.I) iw.Write((ushort)i);
                meta.Add(new { tex = texNames.Contains(b.Tex) || b.Tex == "@water" ? b.Tex : "", alpha = b.Alpha, layer = b.Layer, v0 = vStart, vn = b.P.Count, i0 = iStart, @in = b.I.Count });
                vStart += b.P.Count; iStart += b.I.Count;
            }
        }
        var header = JsonSerializer.SerializeToUtf8Bytes(new { origin = new[] { ox, oy, oz }, scale = new[] { span, span, zspan }, batches = meta });
        int pad = (4 - (8 + header.Length) % 4) % 4;   // vertex data 4-byte aligned for the browser's typed arrays
        using var f = File.Create(path);
        using var w = new BinaryWriter(f);
        w.Write(Encoding.ASCII.GetBytes("GEM1")); w.Write(header.Length + pad); w.Write(header); for (int i = 0; i < pad; i++) w.Write((byte)' ');
        vbuf.Position = 0; vbuf.CopyTo(f);
        if (vbuf.Length % 4 != 0) w.Write((ushort)0);   // 14-byte vertices: keep the indices aligned
        ibuf.Position = 0; ibuf.CopyTo(f);
        return f.Length;
    }

    static ushort Q(float v) => (ushort)Math.Clamp(MathF.Round(v * 65535f), 0, 65535);
    static float Clamp(float v) => float.IsFinite(v) ? Math.Clamp(v, -60000f, 60000f) : 0f;
}
