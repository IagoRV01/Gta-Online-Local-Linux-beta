// Which zone a point is in, from the zone boxes in map.json (popzone.ipl, exported locally by tools/godeye-map).
// The game nests small boxes inside big ones (a pier inside the ocean, a lot inside a district): the smallest box that
// holds the point wins, then the smallest box by ground area ignoring height.
import { ZONE_NAMES } from "./names.ts";

type Box = [number, number, number, number, number, number, string];
export interface ZoneLabel { code: string; name: string; x: number; y: number; area: number }

export class Zones {
  private boxes: Box[] = [];

  load(boxes: Box[]): void {
    this.boxes = boxes.filter((b) => b.length === 7 && b[6] !== "OCEANA" && b[6] !== "ISHEISTZONE" && b[6] !== "PROL");
  }

  get count(): number { return this.boxes.length; }

  codeAt(x: number, y: number, z: number): string {
    let best = "", bestVol = Infinity, best2d = "", bestArea = Infinity;
    for (const [x0, y0, z0, x1, y1, z1, code] of this.boxes) {
      if (x < x0 || x > x1 || y < y0 || y > y1) continue;
      const area = (x1 - x0) * (y1 - y0);
      if (area < bestArea) { bestArea = area; best2d = code; }
      if (z < z0 - 5 || z > z1 + 5) continue;
      const vol = area * (z1 - z0);
      if (vol < bestVol) { bestVol = vol; best = code; }
    }
    return best || best2d || (this.boxes.length ? "OCEANA" : "");
  }

  nameAt(x: number, y: number, z: number): { code: string; name: string } {
    const code = this.codeAt(x, y, z);
    return { code, name: ZONE_NAMES[code] ?? (code ? code : "") };
  }

  // one label per zone code: the area-weighted centre of its boxes (tiny "fix" boxes ignored)
  labels(): ZoneLabel[] {
    const acc = new Map<string, { sx: number; sy: number; a: number }>();
    for (const [x0, y0, , x1, y1, , code] of this.boxes) {
      const a = (x1 - x0) * (y1 - y0);
      if (a < 20000) continue;
      const e = acc.get(code) ?? { sx: 0, sy: 0, a: 0 };
      e.sx += ((x0 + x1) / 2) * a; e.sy += ((y0 + y1) / 2) * a; e.a += a;
      acc.set(code, e);
    }
    return [...acc].map(([code, e]) => ({ code, name: ZONE_NAMES[code] ?? code, x: Math.round(e.sx / e.a), y: Math.round(e.sy / e.a), area: Math.round(e.a) }));
  }
}
