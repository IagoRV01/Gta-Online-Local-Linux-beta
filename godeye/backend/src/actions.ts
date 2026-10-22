// GodEye's control actions (Phase 10): who may use them, how often, and the game server command each becomes.
// Every input is checked here and again by the game server; the browser only picks from these.
import type { Role } from "./auth.ts";

export interface ActionRequest { action: string; target?: number | "all"; value?: string; text?: string; seconds?: number; camera?: number | "all" }
export interface ActionDef { roles: Role[]; cooldownS: number; label: string; player: boolean }

export const ACTIONS: Record<string, ActionDef> = {
  wanted:    { roles: ["DISPATCH"], cooldownS: 10, label: "Wanted level", player: true },
  alert:     { roles: ["DISPATCH"], cooldownS: 30, label: "Police alert", player: true },
  roadblock: { roles: ["DISPATCH"], cooldownS: 60, label: "Roadblock", player: true },
  traffic:   { roles: ["HACKER"], cooldownS: 30, label: "Traffic lights", player: false },
  blackout:  { roles: ["HACKER", "OPERATOR"], cooldownS: 30, label: "Blackout", player: false },
  weather:   { roles: ["OPERATOR"], cooldownS: 15, label: "Weather", player: false },
  time:      { roles: ["OPERATOR"], cooldownS: 10, label: "Time of day", player: false },
  notify:    { roles: ["OPERATOR"], cooldownS: 5, label: "Message", player: false },
  camera:    { roles: ["OPERATOR", "HACKER"], cooldownS: 2, label: "Camera", player: false },
};

export const WEATHERS = ["CLEAR", "EXTRASUNNY", "CLOUDS", "OVERCAST", "RAIN", "CLEARING", "THUNDER", "SMOG", "FOGGY", "XMAS", "SNOW", "SNOWLIGHT", "BLIZZARD", "HALLOWEEN", "NEUTRAL", "auto"];
const TIMES: Record<string, string> = { dawn: "6 30", day: "12 0", dusk: "19 30", night: "0 0" };
const TRAFFIC = ["red", "green", "random", "normal"];
export const CAMERA_MODES = ["cctv", "follow", "heli", "drone", "cinematic", "third", "chase"];

export function allowed(role: Role, action: string): boolean {
  const a = ACTIONS[action];
  return !!a && (role === "ADMIN" || a.roles.includes(role));
}

// printable text only (no line breaks: one command per line on the admin API), at most 100 characters
export function cleanText(s: unknown): string {
  return String(s ?? "").replace(/[\u0000-\u001f\u007f]/g, " ").replace(/\s+/g, " ").trim().slice(0, 100);
}

// The server command for a request, or an error message. `playing(id)` says whether that player is in the world.
export function toCommand(r: ActionRequest, playing: (id: number) => boolean): { line?: string; error?: string } {
  const def = ACTIONS[r.action];
  if (!def) return { error: "unknown action" };
  let target = "";
  if (def.player || (r.action === "notify" && r.target !== "all")) {
    const id = Number(r.target);
    if (!Number.isInteger(id) || id < 1 || id > 65535 || !playing(id)) return { error: "that player is not in the world" };
    target = String(id);
  }
  switch (r.action) {
    case "wanted": {
      const v = String(r.value ?? "");
      if (!/^([+-][1-5]|[0-5])$/.test(v)) return { error: "wanted: 0-5, +1..+5 or -1..-5" };
      return { line: `wanted ${target} ${v}` };
    }
    case "alert": return { line: `alert ${target} ${cleanText(r.text) || "Dispatch has your description."}` };
    case "roadblock": return { line: `roadblock ${target}` };
    case "traffic": {
      const v = String(r.value ?? "");
      if (!TRAFFIC.includes(v)) return { error: "traffic: red, green, random or normal" };
      const secs = v === "normal" ? 0 : Math.round(Number(r.seconds ?? 120));
      if (!Number.isFinite(secs) || secs < 0 || secs > 600) return { error: "traffic: 0-600 seconds" };
      return { line: `traffic ${v} ${secs}` };
    }
    case "blackout": {
      const v = String(r.value ?? "");
      return v === "on" || v === "off" ? { line: `blackout ${v}` } : { error: "blackout: on or off" };
    }
    case "weather": {
      const v = String(r.value ?? "");
      return WEATHERS.includes(v) ? { line: `weather ${v}` } : { error: "unknown weather" };
    }
    case "time": {
      const t = TIMES[String(r.value ?? "")];
      return t ? { line: `time ${t}` } : { error: "time: dawn, day, dusk or night" };
    }
    case "camera": {   // value = mode; cctv: text = the CCTV id; the others: target = the player
      const cam = r.camera === "all" ? "all" : Number(r.camera);
      if (cam !== "all" && (!Number.isInteger(cam) || cam < 1 || cam > 65535)) return { error: "camera: a camera client or all" };
      const mode = String(r.value ?? "");
      if (!CAMERA_MODES.includes(mode)) return { error: "camera: unknown mode" };
      if (mode === "cctv") {
        const id = String(r.text ?? "");
        return /^[A-Za-z0-9_]{1,15}$/.test(id) ? { line: `camera ${cam} cctv ${id}` } : { error: "camera: unknown CCTV camera" };
      }
      const id = Number(r.target);
      if (!Number.isInteger(id) || !playing(id)) return { error: "that player is not in the world" };
      return { line: `camera ${cam} ${mode} ${id}` };
    }
    case "notify": {
      const text = cleanText(r.text);
      if (!text) return { error: "the message is empty" };
      return { line: `notify ${r.target === "all" ? "all" : target} ${text}` };
    }
  }
  return { error: "unknown action" };
}

// per user: the cooldown of each action, and at most 20 actions a minute
export class Limits {
  private next = new Map<string, number>();
  private recent = new Map<string, number[]>();

  check(user: string, role: Role, action: string): { ok: boolean; waitS?: number } {
    const now = Date.now();
    const until = this.next.get(user + "|" + action) ?? 0;
    if (until > now) return { ok: false, waitS: Math.ceil((until - now) / 1000) };
    const r = (this.recent.get(user) ?? []).filter((t) => now - t < 60000);
    this.recent.set(user, r);
    if (r.length >= 20) return { ok: false, waitS: Math.ceil((60000 - (now - r[0])) / 1000) };
    return { ok: true };
  }

  used(user: string, role: Role, action: string): number {
    const now = Date.now();
    const cd = (ACTIONS[action]?.cooldownS ?? 0) * (role === "ADMIN" ? 0.2 : 1) * 1000;
    this.next.set(user + "|" + action, now + cd);
    this.recent.get(user)?.push(now) ?? this.recent.set(user, [now]);
    return now + cd;
  }

  cooldowns(user: string): Record<string, number> {
    const out: Record<string, number> = {};
    const now = Date.now();
    for (const a of Object.keys(ACTIONS)) { const u = this.next.get(user + "|" + a) ?? 0; if (u > now) out[a] = u; }
    return out;
  }
}
