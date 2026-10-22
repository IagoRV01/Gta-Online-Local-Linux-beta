// Names for what the telemetry only carries as numbers: vehicle and weapon model hashes, zone codes.
import { readFileSync } from "node:fs";

// GTA's model/weapon hash (Jenkins one-at-a-time over the lowercase name)
export function joaat(text: string): number {
  let h = 0;
  for (const ch of text.toLowerCase()) {
    h = (h + ch.charCodeAt(0)) >>> 0;
    h = (h + (h << 10)) >>> 0;
    h = (h ^ (h >>> 6)) >>> 0;
  }
  h = (h + (h << 3)) >>> 0;
  h = (h ^ (h >>> 11)) >>> 0;
  h = (h + (h << 15)) >>> 0;
  return h;
}

const CLASSES = ["Compact", "Sedan", "SUV", "Coupe", "Muscle", "Sports Classic", "Sports", "Super", "Motorcycle", "Off-Road",
  "Industrial", "Utility", "Van", "Cycle", "Boat", "Helicopter", "Plane", "Service", "Emergency", "Military", "Commercial", "Train",
  "Open Wheel"];

export interface VehicleName { model: string; name: string; cls: string }
const vehicles = new Map<number, VehicleName>();

export function loadVehicles(file: string): number {
  for (const line of readFileSync(file, "utf8").split(/\r?\n/)) {
    const [model, cls] = line.trim().split(",");
    if (!model) continue;
    const name = model.charAt(0).toUpperCase() + model.slice(1);
    vehicles.set(joaat(model), { model, name, cls: CLASSES[Number(cls)] ?? "" });
  }
  return vehicles.size;
}

export function vehicleName(hash: number): VehicleName {
  return vehicles.get(hash >>> 0) ?? { model: "", name: `0x${(hash >>> 0).toString(16).toUpperCase()}`, cls: "" };
}

const WEAPONS = `unarmed knife nightstick hammer bat golfclub crowbar bottle dagger hatchet knuckle machete flashlight switchblade
poolcue wrench battleaxe stone_hatchet candycane stunrod pistol pistol_mk2 combatpistol appistol stungun stungun_mp pistol50 snspistol
snspistol_mk2 heavypistol vintagepistol flaregun marksmanpistol revolver revolver_mk2 doubleaction raypistol ceramicpistol navyrevolver
gadgetpistol pistolxm3 tecpistol microsmg smg smg_mk2 assaultsmg combatpdw machinepistol minismg raycarbine pumpshotgun pumpshotgun_mk2
sawnoffshotgun assaultshotgun bullpupshotgun musket heavyshotgun dbshotgun autoshotgun combatshotgun assaultrifle assaultrifle_mk2
carbinerifle carbinerifle_mk2 advancedrifle specialcarbine specialcarbine_mk2 bullpuprifle bullpuprifle_mk2 compactrifle militaryrifle
heavyrifle tacticalrifle battlerifle mg combatmg combatmg_mk2 gusenberg sniperrifle heavysniper heavysniper_mk2 marksmanrifle
marksmanrifle_mk2 precisionrifle rpg grenadelauncher grenadelauncher_smoke minigun firework railgun railgunxm3 hominglauncher
compactlauncher rayminigun emplauncher snowlauncher grenade bzgas molotov stickybomb proxmine snowball pipebomb ball smokegrenade flare
acidpackage petrolcan fireextinguisher hazardcan fertilizercan hackingdevice`.split(/\s+/);
const weapons = new Map<number, string>();
for (const w of WEAPONS) {
  const pretty = w.replace(/_mk2$/, " Mk II").replace(/_/g, " ").replace(/\b\w/g, (c) => c.toUpperCase());
  weapons.set(joaat("weapon_" + w), pretty);
}
weapons.set(joaat("gadget_parachute"), "Parachute");

export function weaponName(hash: number): string {
  if (!hash) return "";
  return weapons.get(hash >>> 0) ?? `0x${(hash >>> 0).toString(16).toUpperCase()}`;
}

// Zone codes (popzone.ipl, GET_NAME_OF_ZONE) to the area names players know
export const ZONE_NAMES: Record<string, string> = {
  AIRP: "Los Santos International Airport", ALAMO: "Alamo Sea", ALTA: "Alta", ARMYB: "Fort Zancudo", BANHAMCA: "Banham Canyon Dr",
  BANNING: "Banning", BAYTRE: "Baytree Canyon", BEACH: "Vespucci Beach", BHAMCA: "Banham Canyon", BRADP: "Braddock Pass",
  BRADT: "Braddock Tunnel", BURTON: "Burton", CALAFB: "Calafia Bridge", CANNY: "Raton Canyon", CCREAK: "Cassidy Creek",
  CHAMH: "Chamberlain Hills", CHIL: "Vinewood Hills", CHU: "Chumash", CMSW: "Chiliad Mountain State Wilderness", CYPRE: "Cypress Flats",
  DAVIS: "Davis", DELBE: "Del Perro Beach", DELPE: "Del Perro", DELSOL: "Puerto Del Sol", DESRT: "Grand Senora Desert",
  DOWNT: "Downtown", DTVINE: "Downtown Vinewood", EAST_V: "East Vinewood", EBURO: "El Burro Heights", ELGORL: "El Gordo Lighthouse",
  ELYSIAN: "Elysian Island", GALFISH: "Galilee", GALLI: "Galileo Park", GOLF: "GWC and Golfing Society", GRAPES: "Grapeseed",
  GREATC: "Great Chaparral", HARMO: "Harmony", HAWICK: "Hawick", HORS: "Vinewood Racetrack", HUMLAB: "Humane Labs and Research",
  ISHEISTZONE: "Cayo Perico", JAIL: "Bolingbroke Penitentiary", KOREAT: "Little Seoul", LACT: "Land Act Reservoir",
  LAGO: "Lago Zancudo", LDAM: "Land Act Dam", LEGSQU: "Legion Square", LMESA: "La Mesa", LOSPUER: "La Puerta", MIRR: "Mirror Park",
  MORN: "Morningwood", MOVIE: "Richards Majestic", MTCHIL: "Mount Chiliad", MTGORDO: "Mount Gordo", MTJOSE: "Mount Josiah",
  MURRI: "Murrieta Heights", NCHU: "North Chumash", NOOSE: "N.O.O.S.E.", OBSERV: "Galileo Observatory", OCEANA: "Pacific Ocean",
  PALCOV: "Paleto Cove", PALETO: "Paleto Bay", PALFOR: "Paleto Forest", PALHIGH: "Palomino Highlands",
  PALMPOW: "Palmer-Taylor Power Station", PBLUFF: "Pacific Bluffs", PBOX: "Pillbox Hill", PROCOB: "Procopio Beach",
  PROL: "North Yankton", RANCHO: "Rancho", RGLEN: "Richman Glen", RICHM: "Richman", ROCKF: "Rockford Hills",
  RTRAK: "Redwood Lights Track", SANAND: "San Andreas", SANCHIA: "San Chianski Mountain Range", SANDY: "Sandy Shores",
  SKID: "Mission Row", SLAB: "Stab City", STAD: "Maze Bank Arena", STRAW: "Strawberry", TATAMO: "Tataviam Mountains",
  TERMINA: "Terminal", TEXTI: "Textile City", TONGVAH: "Tongva Hills", TONGVAV: "Tongva Valley", VCANA: "Vespucci Canals",
  VESP: "Vespucci", VINE: "Vinewood", WINDF: "Ron Alternates Wind Farm", WVINE: "West Vinewood", ZANCUDO: "Zancudo River",
  ZP_ORT: "Port of South Los Santos", ZQ_UAR: "Davis Quartz",
};
