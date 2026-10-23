// The control panel's data (ADMIN): how the PC, the game server and GodEye are doing, the settings an ADMIN can change
// from the page (kept in settings.json next to users.json), and the tails of the logs. Read-only probes, no npm packages.
import { execFile } from "node:child_process";
import { closeSync, existsSync, fstatSync, openSync, readFileSync, readSync, statfsSync, writeFileSync } from "node:fs";
import { cpus, freemem, hostname, totalmem, uptime } from "node:os";
import type { Role } from "./auth.ts";

// ---- settings an ADMIN changes on the page
export const SIGNUP_ROLES: Role[] = ["HACKER", "DISPATCH", "OPERATOR"];
export interface Settings {
  registration: "open" | "closed";   // may visitors make their own accounts?
  signupRoles: Role[];               // the roles they may pick (never ADMIN)
  publicMap3d: "login" | "open";     // the 3D map on the public address
}

export class SettingsStore {
  private file: string;
  value: Settings;

  constructor(file: string, defaults: Settings) {
    this.file = file;
    let saved: Partial<Settings> = {};
    try { if (existsSync(file)) saved = JSON.parse(readFileSync(file, "utf8")); } catch { /* defaults */ }
    this.value = SettingsStore.clean({ ...defaults, ...saved }, defaults);
  }

  static clean(s: Partial<Settings>, d: Settings): Settings {
    const roles = Array.isArray(s.signupRoles) ? s.signupRoles.filter((r): r is Role => SIGNUP_ROLES.includes(r as Role)) : d.signupRoles;
    return {
      registration: s.registration === "closed" ? "closed" : s.registration === "open" ? "open" : d.registration,
      signupRoles: [...new Set(roles)],
      publicMap3d: s.publicMap3d === "open" ? "open" : s.publicMap3d === "login" ? "login" : d.publicMap3d,
    };
  }

  update(patch: Partial<Settings>): Settings {
    this.value = SettingsStore.clean({ ...this.value, ...patch }, this.value);
    writeFileSync(this.file, JSON.stringify(this.value, null, 2) + "\n");
    return this.value;
  }
}

// ---- the PC: CPU (from the change in the cores' times), memory, NVIDIA cards, disks
let lastCpu = cpus().map((c) => c.times);
let cpuPct = 0;
setInterval(() => {
  const now = cpus().map((c) => c.times);
  let busy = 0, all = 0;
  now.forEach((t, i) => {
    const p = lastCpu[i] ?? t;
    const d = { user: t.user - p.user, nice: t.nice - p.nice, sys: t.sys - p.sys, idle: t.idle - p.idle, irq: t.irq - p.irq };
    const sum = d.user + d.nice + d.sys + d.idle + d.irq;
    busy += sum - d.idle; all += sum;
  });
  cpuPct = all > 0 ? Math.round((busy / all) * 100) : 0;
  lastCpu = now;
}, 2000).unref();

interface Gpu { name: string; util: number; memUsed: number; memTotal: number; temp: number }
let gpus: Gpu[] = [];
let gpuAt = 0;
function refreshGpus(): void {
  if (Date.now() - gpuAt < 3000) return;
  gpuAt = Date.now();
  execFile("nvidia-smi", ["--query-gpu=name,utilization.gpu,memory.used,memory.total,temperature.gpu", "--format=csv,noheader,nounits"], { timeout: 4000, windowsHide: true }, (err, out) => {
    if (err) { gpus = []; return; }
    gpus = out.trim().split(/\r?\n/).filter(Boolean).map((l) => {
      const [name, util, used, total, temp] = l.split(",").map((s) => s.trim());
      return { name, util: Number(util) || 0, memUsed: Number(used) || 0, memTotal: Number(total) || 0, temp: Number(temp) || 0 };
    });
  });
}

function disk(path: string): { path: string; free: number; total: number } | null {
  try { const s = statfsSync(path); return { path, free: s.bavail * s.bsize, total: s.blocks * s.bsize }; } catch { return null; }
}

export function system(dataPaths: string[]): object {
  refreshGpus();
  const roots = [...new Set(dataPaths.map((p) => (/^[A-Za-z]:/.test(p) ? p.slice(0, 2).toUpperCase() + "\\" : "/")))];
  return {
    host: hostname(), cores: cpus().length, cpu: cpuPct, cpuName: cpus()[0]?.model?.trim() ?? "",
    mem: { total: totalmem(), free: freemem() }, gpus, uptime: Math.round(uptime()),
    disks: roots.map(disk).filter(Boolean),
  };
}

// ---- the last lines of a log, read from its end
export function tail(file: string, lines = 200): string[] {
  if (!existsSync(file)) return [`(no file ${file})`];
  const fd = openSync(file, "r");
  try {
    const size = fstatSync(fd).size, len = Math.min(size, 96 * 1024);
    const buf = Buffer.alloc(len);
    readSync(fd, buf, 0, len, size - len);
    const all = buf.toString("utf8").split(/\r?\n/);
    if (size > len) all.shift();   // a cut first line
    return all.filter((l) => l.length).slice(-lines);
  } finally { closeSync(fd); }
}
