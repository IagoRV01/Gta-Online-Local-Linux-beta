// ---------------------------------------------------------------- New apartments: the doors on the street, Dynasty 8
// (data in NewApartments.inc). The buildings are Rockstar's map with no door of ours, so the first time you come near one
// the entrances are found on the street like GTA Online's: the walk-in corona on the pavement of the face nearest a road,
// facing the building, and the garage corona on that road a little further along the face.
static const NewAptPlace *NaptPlaceOf(int k) { return k >= 0 && k < NNEW_APTS ? NaptPlace(NEW_APTS[k].id) : nullptr; }
// the facade of the building's face f (0 +x, 1 -x, 2 +y, 3 -y of the footprint) at lateral offset t, seen from the street:
// a ray from the road nearest that point towards the building at walking height (roads above or below the pavement there,
// like the freeway, don't count); returns the wall point, the outward direction, the pavement height and the road distance
static bool NaptFacade(const NewAptPlace &p, int f, float t, Vec3 &wall, Vec3 &outDir, float &ground, float &roadD) {
    float hx = p.hx, hy = p.hy;
    float ox = f == 0 ? hx + 3.f : f == 1 ? -(hx + 3.f) : t, oy = f == 2 ? hy + 3.f : f == 3 ? -(hy + 3.f) : t;
    float ix = f < 2 ? 0.f : t, iy = f < 2 ? t : 0.f;
    Vec3 o = NaptFoot(p, ox, oy), in = NaptFoot(p, ix, iy);
    Vec3 node{};
    if (!NB(N<int>(N_GET_CLOSEST_VEHICLE_NODE, o.x, o.y, p.z, &node, 1, 3.0f, 0.0f))) return false;
    roadD = std::hypot(node.x - o.x, node.y - o.y);
    if (roadD > 45.f) return false;
    float z = node.z + 1.2f;
    for (int tries = 0; tries < 3; ++tries) {   // a lamp post or a tree in the way: start a little to the side
        float side = tries == 0 ? 0.f : tries == 1 ? 2.5f : -2.5f;
        float dx = in.x - node.x, dy = in.y - node.y, len = std::hypot(dx, dy);
        if (len < 1.f) return false;
        Vec3 s0 = V3(node.x - dy / len * side, node.y + dx / len * side, z);
        int ray = N<int>(N_START_EXPENSIVE_SYNCHRONOUS_SHAPE_TEST_LOS_PROBE, s0.x, s0.y, z, in.x, in.y, z, 1, 0, 7);
        int hit = 0; Vec3 end{}, normal{}; int ent = 0;
        if (!ray || N<int>(N_GET_SHAPE_TEST_RESULT, ray, &hit, &end, &normal, &ent) != 2 || !hit) continue;
        if (std::hypot(end.x - s0.x, end.y - s0.y) < 3.f) continue;
        outDir = V3(-dx / len, -dy / len, 0.f);
        wall = end;
        Vec3 door = V3(end.x + outDir.x * 1.2f, end.y + outDir.y * 1.2f, end.z);
        ground = GroundAt(door.x, door.y, z + 1.f, node.z);
        if (std::fabs(ground - node.z) > 3.5f) return false;   // that road is not at this pavement's level
        return true;
    }
    return false;
}
static void NaptResolve(int k) {
    const NewAptPlace *pp = NaptPlaceOf(k);
    if (!pp || k >= (int)g_naptBld.size()) return;
    const NewAptPlace &p = *pp;
    // the front: the face whose wall is nearest a road
    int bf = -1; float bScore = 1e9f; Vec3 bWall{}, bOut{}; float bGround = 0.f;
    for (int f = 0; f < 4; ++f) {
        Vec3 wall, out; float g, rd;
        if (!NaptFacade(p, f, 0.f, wall, out, g, rd)) continue;
        float sc = rd;
        if (sc < bScore) { bScore = sc; bf = f; bWall = wall; bOut = out; bGround = g; }
    }
    if (bf < 0) { Log("napt %s: no facade found", p.id); return; }
    OBuilding &b = g_naptBld[k];
    Vec3 door = V3(bWall.x + bOut.x * 1.2f, bWall.y + bOut.y * 1.2f, bWall.z);
    float ground = bGround;
    float outH = HeadingTo(V3(0, 0, 0), bOut), inH = std::fmod(outH + 180.f, 360.f);
    OEntrance &we = b.e[0];
    we.kind = 1; we.mx = door.x; we.my = door.y; we.mz = ground; we.mh = inH;
    we.dx = bWall.x; we.dy = bWall.y; we.dz = ground + 1.4f;
    we.sx = door.x; we.sy = door.y; we.sz = ground;
    we.ax = we.bx = door.x; we.ay = we.by = door.y; we.az = ground - 1.f; we.bz = ground + 3.f; we.w = 2.f;
    b.fx = door.x + bOut.x * 1.5f; b.fy = door.y + bOut.y * 1.5f; b.fz = ground + 1.f; b.fh = outH;
    // the garage door: further along the same facade, on the side nearer a road, a driveway in front of it
    float lat = bf < 2 ? p.hy : p.hx;
    Vec3 gWall = bWall, gOut = bOut; float gGround = ground, gScore = 1e9f;
    for (int s = -1; s <= 1; s += 2) {
        Vec3 wall, out; float g, rd;
        if (!NaptFacade(p, bf, s * lat * 0.55f, wall, out, g, rd)) continue;
        if (std::hypot(wall.x - bWall.x, wall.y - bWall.y) < 8.f) continue;
        float sc = rd;
        if (sc < gScore) { gScore = sc; gWall = wall; gOut = out; gGround = g; }
    }
    if (gScore > 1e8f) {   // no road along the facade: 7 m beside the walk-in door, on the same pavement
        gWall = V3(bWall.x - bOut.y * 7.f, bWall.y + bOut.x * 7.f, bWall.z);
        gScore = bScore;
    }
    Vec3 gm = V3(gWall.x + gOut.x * 4.f, gWall.y + gOut.y * 4.f, gWall.z);
    float gz = GroundAt(gm.x, gm.y, gGround + 2.f, gGround);
    OEntrance &ge = b.e[1];
    ge.kind = 2; ge.mx = gm.x; ge.my = gm.y; ge.mz = gz; ge.mh = inH;
    ge.ax = gWall.x + gOut.x * 1.f; ge.ay = gWall.y + gOut.y * 1.f; ge.az = gz - 1.f;
    ge.bx = gWall.x + gOut.x * 9.f; ge.by = gWall.y + gOut.y * 9.f; ge.bz = gz + 3.f; ge.w = 6.f;
    ge.dx = gWall.x; ge.dy = gWall.y; ge.dz = gz + 1.f;
    ge.sx = gm.x; ge.sy = gm.y; ge.sz = gz;
    b.vx = gWall.x + gOut.x * 10.f; b.vy = gWall.y + gOut.y * 10.f; b.vz = gz + 0.5f; b.vh = outH;
    g_naptResolved[k] = true;
    int pi = g_naptProp[k];
    PROPS[pi].x = we.mx; PROPS[pi].y = we.my; PROPS[pi].z = we.mz;
    auto bl = g_propBlip.find(pi);
    if (bl != g_propBlip.end()) N(N_SET_BLIP_COORDS, bl->second, we.mx, we.my, we.mz);
    Log("napt %s: face %d door %.2f %.2f %.2f h %.0f | garage %.2f %.2f %.2f (road %.0f m / %.0f m)", p.id, bf, we.mx, we.my, we.mz, we.mh, ge.mx, ge.my, ge.mz, bScore, gScore);
}
// one building per tick, when you are near enough for the street around it to be loaded
static void NaptResolveTick(Vec3 mp) {
    static int last = 0;
    if (Now() - last < 1000) return;
    last = Now();
    for (int k = 0; k < (int)g_naptBld.size(); ++k) {
        if (g_naptResolved[k]) continue;
        const NewAptPlace *p = NaptPlaceOf(k);
        if (p && std::hypot(mp.x - p->fx, mp.y - p->fy) < 180.f) { NaptResolve(k); return; }
    }
}
// Dynasty 8 rows for the new apartments: the same slot format as GTA Online's (RWFeedDynasty8), texts given literally
static void NaptLabel(const std::string &t) {
    N(N_BEGIN_TEXT_COMMAND_SCALEFORM_STRING, t.size() > 90 ? "CELL_EMAIL_BCON" : "STRING");
    for (size_t i = 0; i < t.size(); i += 90) N(N_ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME, t.substr(i, 90).c_str());
    N(N_END_TEXT_COMMAND_SCALEFORM_STRING);
}
static void NaptFeedDynasty8(std::vector<int> &slots) {
    for (int k = 0; k < (int)g_naptProp.size(); ++k) {
        int i = g_naptProp[k];
        const NewAptInfo &a = NEW_APTS[k];
        if (Owns(PROPS[i].name)) continue;
        int slot = (int)slots.size();
        slots.push_back(i);
        const NewAptPlace *p = NaptPlaceOf(k);
        N(N_BEGIN_SCALEFORM_MOVIE_METHOD, g_rw.movie, "SET_DATA_SLOT");
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, slot);
        NaptLabel(a.name);
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, p ? p->fx : PROPS[i].x);
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, p ? p->fy : PROPS[i].y);
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, PROPS[i].price);
        char pic[16];
        std::snprintf(pic, sizeof pic, "DYN_MP_%d", a.pic);
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_PLAYER_NAME_STRING, pic);
        NaptLabel(a.desc);
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, a.dynSize);
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, a.dynArea);
        NaptLabel("");
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_BOOL, 0);   // on sale
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, 0);    // trade-in bits
        NaptLabel("");                                   // rebate label
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, 0);    // sale price
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_BOOL, 0);
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_BOOL, 0);
        N(N_END_SCALEFORM_MOVIE_METHOD);
    }
}
