// ---------------------------------------------------------------- New apartments (IslandBuilder Apartments.cs)
// Rockstar's own apartment interiors, cloned into buildings the game leaves empty (downtown towers, beach-front towers,
// Paleto Bay houses) and placed inside them like GTA Online places its interiors in Eclipse Towers. Each one is a
// GTA Online building to the rest of Gori Online: a walk-in door and a garage entrance (blue coronas), the street exits,
// and the GTA Online interior frame of the template the interior was cloned from, so every interior point (where you
// come in, the front door you leave by, the wardrobe...) is GTA Online's, moved to this placement. Sold on Dynasty 8.
// Included from GoriOnline.cpp right after online_props.h (data); NaptResolveTick runs with the properties.
#include "apartments_data.h"

struct NewAptInfo { const char *id, *name, *desc; uint8_t type; int price, cars, dynSize, dynArea, pic; };
static const NewAptInfo NEW_APTS[] = {
    {"dt10", "Pillbox Hill Tower, Apt 52",
     "A two-storey executive apartment 150 m above Pillbox Hill, in one of the tallest towers in Downtown Los Santos. Floor-to-ceiling windows over the financial district, a 10-car garage and room for a heist planning board.",
     T_APT, 1150000, 10, 6, 6, 83},
    {"dt12", "Vespucci Blvd Tower, Apt 31",
     "Executive living on the 31st floor of a glass tower at the southern edge of Downtown, with the whole city at your feet. Two storeys, a 10-car garage and a private elevator.",
     T_APT, 1050000, 10, 6, 6, 83},
    {"dt09", "Alta Street Tower, Apt 24",
     "A two-storey executive apartment in the heart of Downtown, minutes from the banks you are planning to visit. Panoramic windows, a 10-car garage and heist planning room.",
     T_APT, 990000, 10, 6, 6, 83},
    {"sm12", "Del Perro Beach Residences, Penthouse",
     "A high-end penthouse a short walk from Del Perro Pier: ocean views from the living room, a 10-car garage and everything you need to plan your next big score.",
     T_APT, 545000, 10, 6, 6, 62},
    {"sm18", "Vespucci Canals Tower, Penthouse",
     "Wake up above the canals and the beach in this high-end penthouse in Vespucci. Ocean views, a 10-car garage and a planning room for your crew.",
     T_APT, 510000, 10, 6, 6, 62},
    {"sm01", "Pacific Bluffs Tower, Penthouse",
     "The quiet side of the coast: a high-end penthouse at the top of a Pacific Bluffs tower, looking over the ocean and the Great Ocean Highway. 10-car garage.",
     T_APT, 520000, 10, 6, 6, 62},
    {"pb_coast", "1 Procopio Promenade",
     "A modern two-storey home on the Paleto Bay coast, far from the noise of Los Santos. Sea views from every floor, a 10-car garage and a planning room for heists up north.",
     T_HOUSE, 695000, 10, 6, 0, 77},
    {"pb_hill", "3022 Cascabel Ave",
     "A comfortable house on the hill above Paleto Bay, with views over the bay and the forest. 6-car garage.",
     T_HOUSE, 168000, 6, 5, 0, 69},
    {"pb_village", "0216 Duluoz Ave",
     "A small, affordable home in the middle of Paleto Bay village: close to the bank, the diner and the sheriff's office. 2-car garage.",
     T_HOUSE, 98000, 2, 4, 0, 70},
    {"pb_main", "2471 Pyrite Ave",
     "A quiet family house on the west side of Paleto Bay, a block from the main street. 6-car garage.",
     T_HOUSE, 158000, 6, 5, 0, 68},
};
static const int NNEW_APTS = (int)(sizeof NEW_APTS / sizeof *NEW_APTS);

// The GTA Online template of each interior kind (online_props.h O_PINT, the metadata map its points come from), and
// Rockstar's own placement of the same MLO that template is measured on (origin, heading as the MLO instance has it).
// mid / low: GTA Online gives their points literally (exit area a-b, entry spot sp + heading).
struct AptTemplate {
    const char *kind; int map; float tx, ty, tz, trot, mx, my, mz, myaw, w; bool lit;
    float ax, ay, az, bx, by, bz, spx, spy, spz, sph;
};
static const AptTemplate APT_TEMPLATES[] = {
    {"exec01", 4, -787.7805f, 334.9232f, 186.1134f, 90.f, -787.781f, 334.923f, 186.113f, 90.f, 3.50f, false},     // Eclipse Towers penthouses
    {"high", 0, -794.9184f, 339.6266f, 200.4135f, 180.f, -791.294f, 338.071f, 200.414f, 180.f, 3.50f, false},     // Eclipse Towers, Apt 31
    {"stilts", 3, 120.0541f, 553.7930f, 181.0943f, 6.f, 122.535f, 542.508f, 182.897f, 6.f, 3.50f, false},         // 3677 Whispymound Drive
    {"mid", -1, 342.8157f, -997.4288f, -100.f, 0.f, 342.8157f, -997.4288f, -100.f, 0.f, 2.50f, true,
     347.0897f, -1002.5540f, -96.9462f, 347.0861f, -1003.8003f, -100.8860f, 347.3975f, -1001.4458f, -100.1982f, 4.88f},
    {"low", -1, 260.3297f, -997.4288f, -100.f, 0.f, 260.3297f, -997.4288f, -100.f, 0.f, 1.75f, true,
     265.0820f, -1000.7009f, -100.0086f, 265.0666f, -1002.6346f, -97.2586f, 264.5677f, -1000.6248f, -100.0086f, 38.47f},
};

static std::vector<OBuilding> g_naptBld;          // one GTA Online building each (O_BUILDINGS continues with these)
static std::vector<OInterior> g_naptPint;         // their GTA Online interior frames
static std::vector<int> g_naptProp;               // PROPS index of each
static std::vector<bool> g_naptResolved;          // entrances found on the street (NaptResolveTick)
static std::deque<std::string> g_naptStr;         // storage for the InteriorDef strings
static std::vector<InteriorDef> g_naptInt;

static const NewAptPlace *NaptPlace(const char *id) {
    for (int k = 0; k < NNEW_APT_PLACES; ++k) if (!std::strcmp(NEW_APT_PLACES[k].id, id)) return &NEW_APT_PLACES[k];
    return nullptr;
}
static const AptTemplate *NaptTemplate(const char *kind) {
    for (const AptTemplate &t : APT_TEMPLATES) if (!std::strcmp(t.kind, kind)) return &t;
    return nullptr;
}
// a point of Rockstar's placement moved into ours: rotate about the MLO origin by the heading difference
static void NaptMove(const NewAptPlace &p, const AptTemplate &t, float x, float y, float z, float &ox, float &oy, float &oz) {
    float d = (p.h - t.myaw) * 3.14159265f / 180.f, c = std::cos(d), s = std::sin(d);
    float vx = x - t.mx, vy = y - t.my;
    ox = p.x + vx * c - vy * s; oy = p.y + vx * s + vy * c; oz = p.z + (z - t.mz);
}
// the building footprint's point at local (lx, ly), and its heading
static Vec3 NaptFoot(const NewAptPlace &p, float lx, float ly) {
    float r = p.fh * 3.14159265f / 180.f, c = std::cos(r), s = std::sin(r);
    return V3(p.fx + lx * c - ly * s, p.fy + lx * s + ly * c, p.z);
}
// PROPS rows for the new apartments, their buildings, interiors and interior frames (BuildProps)
static void NaptAddProps(std::vector<PropDef> &props) {
    for (int k = 0; k < NNEW_APTS; ++k) {
        const NewAptInfo &a = NEW_APTS[k];
        const NewAptPlace *p = NaptPlace(a.id);
        const AptTemplate *t = p ? NaptTemplate(p->kind) : nullptr;
        if (!p || !t) continue;   // not in this build of the island DLC
        // the interior: our IPL, the MLO origin (GTA Online's entry spot is used when the template has one)
        g_naptStr.push_back(std::string("napt_") + a.id);
        const char *iid = g_naptStr.back().c_str();
        g_naptStr.push_back(p->ipl);
        g_naptInt.push_back({iid, p->x, p->y, p->z + 1.0f, p->h, g_naptStr.back().c_str(), ""});
        // a provisional building: the doors are found on the street when you first come near (NaptResolveTick)
        OBuilding b{};
        b.id = 1000 + k; b.type = 0; b.ne = 2;
        Vec3 f = NaptFoot(*p, 0.f, -(p->hy + 3.f));
        b.e[0].kind = 1; b.e[0].mx = f.x; b.e[0].my = f.y; b.e[0].mz = p->z; b.e[0].mh = p->fh;
        b.e[1].kind = 2; b.e[1].mx = f.x + 6.f; b.e[1].my = f.y; b.e[1].mz = p->z; b.e[1].mh = p->fh;
        b.fx = f.x; b.fy = f.y; b.fz = p->z; b.fh = p->fh + 180.f;
        b.vx = f.x + 6.f; b.vy = f.y; b.vz = p->z; b.vh = p->fh + 180.f;
        g_naptBld.push_back(b);
        g_naptResolved.push_back(false);
        // GTA Online's interior frame of the template, moved here
        OInterior o{};
        o.pid = 0; o.map = t->map; o.tx = t->tx; o.ty = t->ty; o.tz = t->tz; o.trot = t->trot; o.w = t->w; o.lit = t->lit;
        NaptMove(*p, *t, t->tx, t->ty, t->tz, o.ox, o.oy, o.oz);
        o.orot = t->trot + (p->h - t->myaw);
        if (t->lit) {
            NaptMove(*p, *t, t->ax, t->ay, t->az, o.ax, o.ay, o.az);
            NaptMove(*p, *t, t->bx, t->by, t->bz, o.bx, o.by, o.bz);
            NaptMove(*p, *t, t->spx, t->spy, t->spz, o.spx, o.spy, o.spz);
            o.sph = t->sph + (p->h - t->myaw);
        }
        g_naptPint.push_back(o);
        PropDef d;
        d.name = a.name; d.type = a.type; d.price = a.price; d.x = f.x; d.y = f.y; d.z = p->z; d.h = p->fh;
        d.interior = iid; d.ob = NO_BUILDINGS + (int)g_naptBld.size() - 1; d.cars = a.cars;
        g_naptProp.push_back((int)props.size());
        props.push_back(d);
    }
}
static const OBuilding *BldAt(int ob) {
    if (ob >= 0 && ob < NO_BUILDINGS) return &O_BUILDINGS[ob];
    if (ob >= NO_BUILDINGS && ob - NO_BUILDINGS < (int)g_naptBld.size()) return &g_naptBld[ob - NO_BUILDINGS];
    return nullptr;
}
static int NaptIndexOfProp(int i) {
    for (int k = 0; k < (int)g_naptProp.size(); ++k) if (g_naptProp[k] == i) return k;
    return -1;
}
static const InteriorDef *FindInteriorNapt(const char *id) {
    for (const InteriorDef &d : g_naptInt) if (!std::strcmp(d.id, id)) return &d;
    return nullptr;
}
