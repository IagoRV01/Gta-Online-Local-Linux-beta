// ---------------------------------------------------------------- Internet: the phone's browser, GTA Online style
// Opening the phone's Internet app opens this browser instead: vehicle websites with their pictures, Dynasty 8 Real Estate,
// Maze Bank Foreclosures and property upgrades (styles, decor, lighting...) applied when you enter the property.
// Mouse (click, wheel), keyboard (arrows, Enter, Backspace) and controller (D-pad, A, B). Included from GoriOnline.cpp.
#include "webimg_data.h"

// ---- property customisation data
struct CustOpt { const char *label; int price; const char *val; };
struct CustCat { const char *interior; const char *name; int kind; const CustOpt *opts; int n; int def; };   // kind 0 entity sets, 1 office IPL style, 2 apartment IPL style
#define NOPT(a) (int)(sizeof(a) / sizeof(a[0]))
static const CustOpt O_OFFICE[] = {{"Old Spice Warm", 325000, "01a"}, {"Old Spice Classical", 380000, "01b"}, {"Old Spice Vintage", 420000, "01c"},
    {"Executive Contrast", 150000, "02a"}, {"Executive Rich", 0, "02b"}, {"Executive Cool", 200000, "02c"},
    {"Power Broker Ice", 700000, "03a"}, {"Power Broker Conservative", 780000, "03b"}, {"Power Broker Polished", 850000, "03c"}};
static const CustOpt O_MHCSUITE[] = {{"Skyline Noir", 0, "s01"}, {"Skyline Ivory", 450000, "s02"}, {"Heritage Walnut", 600000, "s03"}};   // the Mile High Club suite (IslandBuilder MhcSuite.cs)
static const CustOpt O_DCEQUIP[] = {{"Standard Servers", 0, "dc_equipment_basic"}, {"High-Density Racks", 1155000, "dc_equipment_basic;dc_equipment_upgrade"}};   // data center
static const CustOpt O_DCSTAFF[] = {{"Skeleton Crew", 0, "dc_staff_basic"}, {"Full Hacker Team", 598500, "dc_staff_basic;dc_staff_upgrade"}};
static const CustOpt O_DCSEC[] = {{"Guard Desk", 0, "dc_security_basic"}, {"Cameras & Lockdown", 351000, "dc_security_basic;dc_security_upgrade"}};
static const CustOpt O_DCLIGHT[] = {{"Ice Blue", 0, "dc_lights_blue"}, {"Red Alert", 25000, "dc_lights_red"}, {"Matrix Green", 25000, "dc_lights_green"}};
static const CustOpt O_APT[] = {{"Modern", 0, "01"}, {"Moody", 140000, "02"}, {"Vibrant", 140000, "03"}, {"Sharp", 160000, "04"},
    {"Monochrome", 160000, "05"}, {"Seductive", 180000, "06"}, {"Regal", 200000, "07"}, {"Aqua", 200000, "08"}};
static const CustOpt O_GDECOR[] = {{"Decor 1", 0, "garage_decor_01"}, {"Decor 2", 175000, "garage_decor_02"}, {"Decor 3", 205000, "garage_decor_03"}, {"Decor 4", 235000, "garage_decor_04"}};
static const CustOpt O_GLIGHT[] = {{"Lighting 1", 0, "lighting_option01"}, {"Lighting 2", 40000, "lighting_option02"}, {"Lighting 3", 45000, "lighting_option03"},
    {"Lighting 4", 50000, "lighting_option04"}, {"Lighting 5", 55000, "lighting_option05"}, {"Lighting 6", 60000, "lighting_option06"},
    {"Lighting 7", 65000, "lighting_option07"}, {"Lighting 8", 70000, "lighting_option08"}, {"Lighting 9", 75000, "lighting_option09"}};
static const CustOpt O_GNUM[] = {{"Signage 1", 0, "numbering_style01"}, {"Signage 2", 20000, "numbering_style02"}, {"Signage 3", 20000, "numbering_style03"},
    {"Signage 4", 25000, "numbering_style04"}, {"Signage 5", 25000, "numbering_style05"}, {"Signage 6", 30000, "numbering_style06"},
    {"Signage 7", 30000, "numbering_style07"}, {"Signage 8", 35000, "numbering_style08"}, {"Signage 9", 35000, "numbering_style09"}};
static const CustOpt O_MURAL[] = {{"Mural 1", 0, "mural_01"}, {"Mural 2", 25000, "mural_02"}, {"Mural 3", 25000, "mural_03"}, {"Mural 4", 30000, "mural_04"},
    {"Mural 5", 30000, "mural_05"}, {"Mural 6", 35000, "mural_06"}, {"Mural 7", 35000, "mural_07"}, {"Mural 8", 40000, "mural_08"}, {"Mural 9", 40000, "mural_09"}};
static const CustOpt O_CLUBDECOR[] = {{"Classic", 0, "walls_01:3;furnishings_01:3;decorative_01"}, {"Modern", 95000, "walls_02:3;furnishings_02:3;decorative_02"}};
static const CustOpt O_NCSTYLE[] = {{"Traditional", 0, "Int01_ba_Style01;Int01_ba_style01_podium"}, {"Edgy", 275000, "Int01_ba_Style02;Int01_ba_style02_podium"},
    {"Glamorous", 495000, "Int01_ba_Style03;Int01_ba_style03_podium"}};
static const CustOpt O_NCNAME[] = {{"Galaxy", 0, "Int01_ba_clubname_01"}, {"Studio Los Santos", 0, "Int01_ba_clubname_02"}, {"Omega", 0, "Int01_ba_clubname_03"},
    {"Technologie", 0, "Int01_ba_clubname_04"}, {"Gefangnis", 0, "Int01_ba_clubname_05"}, {"Maisonette Los Santos", 0, "Int01_ba_clubname_06"},
    {"Tony's Fun House", 0, "Int01_ba_clubname_07"}, {"The Palace", 0, "Int01_ba_clubname_08"}, {"Paradise", 0, "Int01_ba_clubname_09"}};
static const CustOpt O_NCDJ[] = {{"Solomun", 0, "Int01_ba_dj01;DJ_01_Lights_02"}, {"Tale Of Us", 0, "Int01_ba_dj02;DJ_02_Lights_02"},
    {"Dixon", 0, "Int01_ba_dj03;DJ_03_Lights_02"}, {"The Black Madonna", 0, "Int01_ba_dj04;DJ_04_Lights_02"}};
static const CustOpt O_BUNKER[] = {{"Style A", 0, "Bunker_Style_A"}, {"Style B", 230000, "Bunker_Style_B"}, {"Style C", 360000, "Bunker_Style_C"}};
static const CustOpt O_HTINT[] = {{"Shell 1", 0, "set_tint_shell:1"}, {"Shell 2", 50000, "set_tint_shell:2"}, {"Shell 3", 50000, "set_tint_shell:3"},
    {"Shell 4", 60000, "set_tint_shell:4"}, {"Shell 5", 60000, "set_tint_shell:5"}, {"Shell 6", 70000, "set_tint_shell:6"}, {"Shell 7", 70000, "set_tint_shell:7"}};
static const CustOpt O_HFLOOR[] = {{"Floor 1", 0, "set_floor_1"}, {"Floor 2", 90000, "set_floor_2"}};
static const CustOpt O_HDECAL[] = {{"Decal 1", 0, "set_floor_decal_1:1"}, {"Decal 2", 20000, "set_floor_decal_2:1"}, {"Decal 3", 20000, "set_floor_decal_3:1"},
    {"Decal 4", 25000, "set_floor_decal_4:1"}, {"Decal 5", 25000, "set_floor_decal_5:1"}, {"Decal 6", 30000, "set_floor_decal_6:1"},
    {"Decal 7", 30000, "set_floor_decal_7:1"}, {"Decal 8", 35000, "set_floor_decal_8:1"}, {"Decal 9", 35000, "set_floor_decal_9:1"}};
static const CustOpt O_HOFFICE[] = {{"Basic", 0, "set_office_basic"}, {"Traditional", 155000, "set_office_traditional"}, {"Modern", 175000, "set_office_modern"}};
static const CustOpt O_HBED[] = {{"Traditional", 0, "set_bedroom_traditional;set_bedroom_tint:1"}, {"Modern", 120000, "set_bedroom_modern;set_bedroom_tint:1"}};
static const CustOpt O_HLIGHT[] = {{"Hangar A", 0, "set_lighting_hangar_a"}, {"Hangar B", 60000, "set_lighting_hangar_b"}, {"Hangar C", 80000, "set_lighting_hangar_c"}};
static const CustOpt O_FDECAL[] = {{"Decal 1", 0, "set_int_02_decal_01"}, {"Decal 2", 15000, "set_int_02_decal_02"}, {"Decal 3", 15000, "set_int_02_decal_03"},
    {"Decal 4", 20000, "set_int_02_decal_04"}, {"Decal 5", 20000, "set_int_02_decal_05"}, {"Decal 6", 25000, "set_int_02_decal_06"},
    {"Decal 7", 25000, "set_int_02_decal_07"}, {"Decal 8", 30000, "set_int_02_decal_08"}, {"Decal 9", 30000, "set_int_02_decal_09"}};
static const CustOpt O_FLOUNGE[] = {{"Lounge 1", 0, "set_int_02_lounge1:1"}, {"Lounge 2", 110000, "set_int_02_lounge2:1"}, {"Lounge 3", 160000, "set_int_02_lounge3:1"}};
static const CustOpt O_FSLEEP[] = {{"Quarters 1", 0, "set_int_02_sleep:1"}, {"Quarters 2", 85000, "set_int_02_sleep2:1"}, {"Quarters 3", 130000, "set_int_02_sleep3:1"}};
static const CustOpt O_AGWALL[] = {{"Wallpaper 1", 0, "Entity_Set_Wpaper_1"}, {"Wallpaper 2", 30000, "Entity_Set_Wpaper_2"}, {"Wallpaper 3", 30000, "Entity_Set_Wpaper_3"},
    {"Wallpaper 4", 35000, "Entity_Set_Wpaper_4"}, {"Wallpaper 5", 35000, "Entity_Set_Wpaper_5"}, {"Wallpaper 6", 40000, "Entity_Set_Wpaper_6"},
    {"Wallpaper 7", 40000, "Entity_Set_Wpaper_7"}, {"Wallpaper 8", 45000, "Entity_Set_Wpaper_8"}, {"Wallpaper 9", 45000, "Entity_Set_Wpaper_9"}};
static const CustOpt O_PPAT[] = {{"Pattern 1", 0, "Set_Pent_Pattern_01:1"}, {"Pattern 2", 40000, "Set_Pent_Pattern_02:1"}, {"Pattern 3", 40000, "Set_Pent_Pattern_03:1"},
    {"Pattern 4", 45000, "Set_Pent_Pattern_04:1"}, {"Pattern 5", 45000, "Set_Pent_Pattern_05:1"}, {"Pattern 6", 50000, "Set_Pent_Pattern_06:1"},
    {"Pattern 7", 50000, "Set_Pent_Pattern_07:1"}, {"Pattern 8", 55000, "Set_Pent_Pattern_08:1"}, {"Pattern 9", 55000, "Set_Pent_Pattern_09:1"}};
static const CustOpt O_PTINT[] = {{"Colour 1", 0, "Set_Pent_Tint_Shell:1"}, {"Colour 2", 40000, "Set_Pent_Tint_Shell:2"}, {"Colour 3", 40000, "Set_Pent_Tint_Shell:3"},
    {"Colour 4", 45000, "Set_Pent_Tint_Shell:4"}, {"Colour 5", 45000, "Set_Pent_Tint_Shell:5"}, {"Colour 6", 50000, "Set_Pent_Tint_Shell:6"}};
static const CustOpt O_AMURAL[] = {{"Mural 1", 0, "entity_set_mural_option_01"}, {"Mural 2", 25000, "entity_set_mural_option_02"}, {"Mural 3", 25000, "entity_set_mural_option_03"},
    {"Mural 4", 30000, "entity_set_mural_option_04"}, {"Mural 5", 30000, "entity_set_mural_option_05"}, {"Mural 6", 35000, "entity_set_mural_option_06"},
    {"Mural 7", 35000, "entity_set_mural_option_07"}, {"Mural 8", 40000, "entity_set_mural_option_08"}};
static const CustOpt O_AFLOOR[] = {{"Floor 1", 0, "entity_set_floor_option_01"}, {"Floor 2", 20000, "entity_set_floor_option_02"}, {"Floor 3", 20000, "entity_set_floor_option_03"},
    {"Floor 4", 25000, "entity_set_floor_option_04"}, {"Floor 5", 25000, "entity_set_floor_option_05"}, {"Floor 6", 30000, "entity_set_floor_option_06"},
    {"Floor 7", 30000, "entity_set_floor_option_07"}, {"Floor 8", 35000, "entity_set_floor_option_08"}};
static const CustCat CUSTOM[] = {
    {"office_arc", "Office Style", 1, O_OFFICE, NOPT(O_OFFICE), 4}, {"office_mbt", "Office Style", 1, O_OFFICE, NOPT(O_OFFICE), 4},
    {"office_lom", "Office Style", 1, O_OFFICE, NOPT(O_OFFICE), 4}, {"office_mbw", "Office Style", 1, O_OFFICE, NOPT(O_OFFICE), 4},
    {"office_mhc", "Office Style", 1, O_MHCSUITE, NOPT(O_MHCSUITE), 0},   // the Mile High Club: our own suite, three looks
    {"datacenter", "Equipment", 0, O_DCEQUIP, NOPT(O_DCEQUIP), 0}, {"datacenter", "Staff", 0, O_DCSTAFF, NOPT(O_DCSTAFF), 0},
    {"datacenter", "Security", 0, O_DCSEC, NOPT(O_DCSEC), 0}, {"datacenter", "Lighting", 0, O_DCLIGHT, NOPT(O_DCLIGHT), 0},
    {"officegarage", "Garage Decor", 0, O_GDECOR, NOPT(O_GDECOR), 0}, {"officegarage", "Garage Lighting", 0, O_GLIGHT, NOPT(O_GLIGHT), 0},
    {"officegarage", "Garage Signage", 3, O_GNUM, NOPT(O_GNUM), 0},
    {"exec1", "Interior Style", 2, O_APT, NOPT(O_APT), 0}, {"exec2", "Interior Style", 2, O_APT, NOPT(O_APT), 3}, {"exec3", "Interior Style", 2, O_APT, NOPT(O_APT), 6},
    {"clubhouse1", "Decor", 0, O_CLUBDECOR, NOPT(O_CLUBDECOR), 1}, {"clubhouse1", "Mural", 0, O_MURAL, NOPT(O_MURAL), 0},
    {"clubhouse2", "Decor", 0, O_CLUBDECOR, NOPT(O_CLUBDECOR), 0}, {"clubhouse2", "Mural", 0, O_MURAL, NOPT(O_MURAL), 8},
    {"nightclub", "Club Style", 0, O_NCSTYLE, NOPT(O_NCSTYLE), 1}, {"nightclub", "Club Name", 0, O_NCNAME, NOPT(O_NCNAME), 0}, {"nightclub", "Resident DJ", 0, O_NCDJ, NOPT(O_NCDJ), 3},
    {"bunker", "Bunker Style", 0, O_BUNKER, NOPT(O_BUNKER), 0},
    {"hangar", "Shell Colour", 0, O_HTINT, NOPT(O_HTINT), 0}, {"hangar", "Flooring", 0, O_HFLOOR, NOPT(O_HFLOOR), 1}, {"hangar", "Floor Decal", 0, O_HDECAL, NOPT(O_HDECAL), 0},
    {"hangar", "Office", 0, O_HOFFICE, NOPT(O_HOFFICE), 2}, {"hangar", "Living Quarters", 0, O_HBED, NOPT(O_HBED), 1}, {"hangar", "Lighting", 0, O_HLIGHT, NOPT(O_HLIGHT), 0},
    {"facility", "Decal", 0, O_FDECAL, NOPT(O_FDECAL), 0}, {"facility", "Lounge", 0, O_FLOUNGE, NOPT(O_FLOUNGE), 2}, {"facility", "Personal Quarters", 0, O_FSLEEP, NOPT(O_FSLEEP), 2},
    {"agency", "Wallpaper", 0, O_AGWALL, NOPT(O_AGWALL), 7},
    {"penthouse", "Pattern", 0, O_PPAT, NOPT(O_PPAT), 0}, {"penthouse", "Colour", 0, O_PTINT, NOPT(O_PTINT), 0},
    {"arcade", "Mural", 0, O_AMURAL, NOPT(O_AMURAL), 4}, {"arcade", "Floor", 0, O_AFLOOR, NOPT(O_AFLOOR), 0},
};
static const int NCUSTOM = sizeof(CUSTOM) / sizeof(CUSTOM[0]);
static const char *CustInterior(int pi) { return PROPS[pi].type == T_OFFICE && std::strcmp(PROPS[pi].interior, "office_mhc") ? "office" : PROPS[pi].interior; }   // the Mile High Club has its own styles
static bool CatFor(int pi, const CustCat &c) {
    std::string in = PROPS[pi].interior;
    if (!std::strcmp(c.interior, "officegarage")) return PROPS[pi].type == T_OFFICE;   // the Mile High Club garage has the same sets
    return in == c.interior;
}
static int CustChoice(int pi, const CustCat &c) {
    auto it = g_cust.find(std::string(PROPS[pi].name) + "|" + c.name);
    return it == g_cust.end() ? c.def : std::min(c.n - 1, std::max(0, it->second));
}
// effective interior for a property: IPL style swaps + entity set choices; 'unsets' get the alternatives to switch off
static InteriorDef CustomisedInterior(int pi, const InteriorDef &d, std::string &ipls, std::string &sets, std::vector<std::string> &unsets) {
    ipls = d.ipls; sets = d.sets;
    for (const CustCat &c : CUSTOM) {
        if (!CatFor(pi, c) || !std::strcmp(c.interior, "officegarage")) continue;
        int ch = CustChoice(pi, c);
        if (c.kind == 1) { size_t p = ipls.find("_office_"); if (p != std::string::npos) { for (int k = 0; k < c.n; ++k) { std::string alt = ipls.substr(0, p + 8) + c.opts[k].val; if (k != ch && NB(N<int>(N_IS_IPL_ACTIVE, alt.c_str()))) N(N_REMOVE_IPL, alt.c_str()); } ipls.replace(p + 8, 3, c.opts[ch].val); } }
        else if (c.kind == 2) { size_t p = ipls.find("apa_v_mp_h_"); if (p != std::string::npos) { for (int k = 0; k < c.n; ++k) { std::string alt = ipls; alt.replace(p + 11, 2, c.opts[k].val); if (k != ch && NB(N<int>(N_IS_IPL_ACTIVE, alt.c_str()))) N(N_REMOVE_IPL, alt.c_str()); } ipls.replace(p + 11, 2, c.opts[ch].val); } }
        else {
            // drop every option of the category from the default sets, then add the chosen one
            std::vector<std::string> keep;
            for (auto &s : Split(sets, ';')) {
                std::string nm = Split(s, ':')[0]; bool mine = false;
                for (int k = 0; k < c.n; ++k) for (auto &o : Split(c.opts[k].val, ';')) if (Split(o, ':')[0] == nm) mine = true;
                if (!mine && !s.empty()) keep.push_back(s);
            }
            for (int k = 0; k < c.n; ++k) if (k != ch) for (auto &o : Split(c.opts[k].val, ';')) unsets.push_back(Split(o, ':')[0]);
            sets.clear();
            for (auto &s : keep) sets += s + ";";
            sets += c.opts[ch].val;
        }
    }
    InteriorDef r = d; r.ipls = ipls.c_str(); r.sets = sets.c_str();
    return r;
}
static std::string OfficeGarageSets(int pi, int floor, std::vector<std::string> &unsets) {
    std::string s;
    for (const CustCat &c : CUSTOM) {
        if (std::strcmp(c.interior, "officegarage")) continue;
        int ch = CustChoice(pi, c);
        std::string suffix = c.kind == 3 ? "_n" + std::to_string(floor + 1) : "";
        for (int k = 0; k < c.n; ++k) if (k != ch) unsets.push_back(std::string(c.opts[k].val) + suffix);
        s += (s.empty() ? "" : ";") + std::string(c.opts[ch].val) + suffix;
    }
    return s;
}

// ---- browser
enum { WP_HOME, WP_SITE, WP_CAR, WP_ESTATE, WP_PTYPE, WP_MYPROPS, WP_CUSTOM, WP_SECURO, WP_SECCARGO, WP_DATANET };
struct WebPage { int type, a, b, scroll; };
struct Widget { float x, y, w, h; std::string text, sub; int r, g, b, a; const WebImg *img; std::function<void()> act; int tr, tg, tb; float ts; int align; };
static bool g_webOpen = false;
static std::vector<WebPage> g_webStack;
static int g_webFocus = -1, g_webConfirm = -1, g_webConfirmUntil = 0;
static std::string g_webToast; static int g_webToastUntil = 0;
static const WebImg *ImgOf(const std::string &model) { for (const WebImg &w : WEB_IMG) if (model == w.model) return &w; return nullptr; }
static void WebGo(int type, int a = 0, int b = 0) { g_webStack.push_back({type, a, b, 0}); g_webFocus = -1; Sound("SELECT", "HUD_FRONTEND_DEFAULT_SOUNDSET"); }
static void WebBack() { if (g_webStack.size() > 1) g_webStack.pop_back(); else g_webOpen = false; g_webFocus = -1; Sound("BACK", "HUD_FRONTEND_DEFAULT_SOUNDSET"); }
static void WebToast(const std::string &t) { g_webToast = t; g_webToastUntil = Now() + 3500; }
// ---- the office computer: a SecuroServ desktop (special cargo, vehicle cargo, safe, upgrades, foreclosures, garage)
static std::function<void()> g_secPending;   // runs once the computer is closed (missions, the garage)
static void SecLeave(std::function<void()> f) { g_secPending = f; g_webOpen = false; }
static void OpenSecuroServ(int pi) { g_webOpen = true; g_webStack.clear(); g_webStack.push_back({WP_SECURO, pi, 0, 0}); g_webFocus = -1; if (g_menuOpen) CloseMenu(); Sound("SELECT", "HUD_FRONTEND_DEFAULT_SOUNDSET"); }
static void OpenDataNet(int pi) { g_webOpen = true; g_webStack.clear(); g_webStack.push_back({WP_DATANET, pi, 0, 0}); g_webFocus = -1; if (g_menuOpen) CloseMenu(); Sound("SELECT", "HUD_FRONTEND_DEFAULT_SOUNDSET"); }
static void HackContract(int pi);
static void StealSupplies(int pi);
static void SellProduct(int pi);
static float DcSpeed(int pi);
static int OwnedOfType(int type) { for (auto &o : g_props) { int i = FindProp(o); if (i >= 0 && PROPS[i].type == type) return i; } return -1; }
static void OpenInternet() { g_webOpen = true; g_webStack.clear(); g_webStack.push_back({WP_HOME, 0, 0, 0}); g_webFocus = -1; if (g_menuOpen) CloseMenu(); }

static const int SITE_COL[][3] = {{120, 10, 15}, {235, 120, 20}, {70, 90, 45}, {25, 70, 150}, {15, 40, 80}};
static const char *SITE_URL[] = {"www.legendarymotorsport.net", "www.southernsanandreassuperautos.com", "www.warstock-cache-and-carry.com", "www.elitastravel.com", "www.docktease.com"};

// one "confirm by clicking twice" purchase button
static void BuyButton(std::vector<Widget> &w, float x, float y, float ww, float h, int id, int price, std::function<void()> buy) {
    bool armed = g_webConfirm == id && Now() < g_webConfirmUntil;
    w.push_back({x, y, ww, h, armed ? "CONFIRM " + Money(price) : "BUY " + Money(price), "", armed ? 230 : 40, armed ? 140 : 150, armed ? 20 : 60, 255, nullptr,
                 [id, buy] { if (g_webConfirm == id && Now() < g_webConfirmUntil) { g_webConfirm = -1; g_payFromBank = true; buy(); g_payFromBank = false; } else { g_webConfirm = id; g_webConfirmUntil = Now() + 4000; } },
                 255, 255, 255, 0.42f, 0});
}
static void BuildWeb(std::vector<Widget> &w, std::string &url, std::string &title, int &tr, int &tg, int &tb) {
    WebPage &pg = g_webStack.back();
    const float X0 = 0.10f, X1 = 0.90f, Y0 = 0.22f, Y1 = 0.90f;
    tr = 40; tg = 45; tb = 60;
    switch (pg.type) {
    case WP_HOME: {
        url = "www.eyefind.info"; title = "eyefind"; tr = 30; tg = 110; tb = 200;
        struct T { std::string n, s; int r, g, b; std::function<void()> act; };
        std::vector<T> tiles;
        for (int s = 0; s < SITE_COUNT; ++s) tiles.push_back({SITE_NAMES[s], "Vehicles", SITE_COL[s][0], SITE_COL[s][1], SITE_COL[s][2], [s] { WebGo(WP_SITE, s, -1); }});
        tiles.push_back({"Dynasty 8 Real Estate", "Homes", 20, 120, 70, [] { WebGo(WP_ESTATE, 0); }});
        tiles.push_back({"Maze Bank Foreclosures", "Businesses", 190, 20, 40, [] { WebGo(WP_ESTATE, 1); }});
        tiles.push_back({"My Properties", "Upgrades & renovations", 110, 40, 160, [] { WebGo(WP_MYPROPS); }});
        float tw = 0.24f, th = 0.17f;
        for (size_t k = 0; k < tiles.size(); ++k) {
            float x = X0 + 0.02f + (k % 3) * (tw + 0.02f), y = Y0 + 0.02f + (k / 3) * (th + 0.025f);
            w.push_back({x, y, tw, th, tiles[k].n, tiles[k].s, tiles[k].r, tiles[k].g, tiles[k].b, 255, nullptr, tiles[k].act, 255, 255, 255, 0.5f, 0});
        }
        break;
    }
    case WP_SITE: {
        int site = pg.a, cls = pg.b;
        url = SITE_URL[site]; title = SITE_NAMES[site]; tr = SITE_COL[site][0]; tg = SITE_COL[site][1]; tb = SITE_COL[site][2];
        int counts[23] = {0};
        for (auto &c : g_cat) if (SiteOf(c) == site && c.cls >= 0 && c.cls < 23) counts[c.cls]++;
        float bx = X0;
        w.push_back({bx, Y0, 0.07f, 0.035f, "ALL", "", cls < 0 ? 255 : 60, cls < 0 ? 255 : 60, cls < 0 ? 255 : 70, 255, nullptr, [site] { WebPage &p = g_webStack.back(); p.b = -1; p.scroll = 0; }, cls < 0 ? 0 : 255, cls < 0 ? 0 : 255, cls < 0 ? 0 : 255, 0.3f, 0});
        bx += 0.075f;
        for (int k = 0; k < 23; ++k) if (counts[k]) {
            float bw = 0.012f + std::strlen(CLASS_NAMES[k]) * 0.0062f;
            if (bx + bw > X1) break;
            bool sel = cls == k;
            w.push_back({bx, Y0, bw, 0.035f, CLASS_NAMES[k], "", sel ? 255 : 60, sel ? 255 : 60, sel ? 255 : 70, 255, nullptr, [k] { WebPage &p = g_webStack.back(); p.b = k; p.scroll = 0; }, sel ? 0 : 255, sel ? 0 : 255, sel ? 0 : 255, 0.3f, 0});
            bx += bw + 0.005f;
        }
        std::vector<const CatCar *> list;
        for (auto &c : g_cat) if (SiteOf(c) == site && (cls < 0 || c.cls == cls)) list.push_back(&c);
        std::sort(list.begin(), list.end(), [](const CatCar *a, const CatCar *b) { return a->price < b->price; });
        const int cols = 4, rows = 2; float cw = 0.19f, ch = 0.29f;
        int maxScroll = std::max(0, ((int)list.size() + cols - 1) / cols - rows);
        pg.scroll = std::min(pg.scroll, maxScroll);
        for (int k = pg.scroll * cols; k < (int)list.size() && k < (pg.scroll + rows) * cols; ++k) {
            int i = k - pg.scroll * cols;
            const CatCar *c = list[k];
            float x = X0 + 0.01f + (i % cols) * (cw + 0.01f), y = Y0 + 0.05f + (i / cols) * (ch + 0.02f);
            int idx = (int)(c - &g_cat[0]);
            w.push_back({x, y, cw, ch, c->name, Money(c->price), 245, 245, 248, 255, ImgOf(c->model), [idx] { WebGo(WP_CAR, idx); }, 20, 20, 30, 0.36f, 1});
        }
        if (maxScroll > 0) {
            int totalRows = ((int)list.size() + cols - 1) / cols, pages = (totalRows + rows - 1) / rows;
            w.push_back({X1 - 0.16f, Y1 - 0.04f, 0.075f, 0.035f, "< PREV", "", 60, 60, 70, 255, nullptr, [] { WebPage &p = g_webStack.back(); p.scroll = std::max(0, p.scroll - 2); }, 255, 255, 255, 0.3f, 0});
            w.push_back({X1 - 0.08f, Y1 - 0.04f, 0.075f, 0.035f, "NEXT >", "", 60, 60, 70, 255, nullptr, [maxScroll] { WebPage &p = g_webStack.back(); p.scroll = std::min(maxScroll, p.scroll + 2); }, 255, 255, 255, 0.3f, 0});
            w.push_back({X0, Y1 - 0.04f, 0.2f, 0.035f, std::to_string(list.size()) + " vehicles - page " + std::to_string(std::min(pages, (pg.scroll + rows - 1) / rows + 1)) + "/" + std::to_string(pages), "", 0, 0, 0, 0, nullptr, nullptr, 60, 60, 70, 0.3f, 1});
        }
        break;
    }
    case WP_CAR: {
        const CatCar &c = g_cat[pg.a];
        int site = SiteOf(c);
        url = std::string(SITE_URL[site]) + "/" + c.model; title = SITE_NAMES[site]; tr = SITE_COL[site][0]; tg = SITE_COL[site][1]; tb = SITE_COL[site][2];
        w.push_back({X0 + 0.01f, Y0, 0.42f, 0.42f, "", "", 245, 245, 248, 255, ImgOf(c.model), nullptr, 0, 0, 0, 0.f, 0});
        if (!ImgOf(c.model)) w.push_back({X0 + 0.01f, Y0, 0.42f, 0.42f, "NO IMAGE", "", 0, 0, 0, 0, nullptr, nullptr, 120, 120, 130, 0.5f, 0});
        w.push_back({X0 + 0.45f, Y0 + 0.02f, 0.33f, 0.05f, c.name, "", 0, 0, 0, 0, nullptr, nullptr, 20, 20, 30, 0.75f, 1});
        w.push_back({X0 + 0.45f, Y0 + 0.08f, 0.33f, 0.04f, CLASS_NAMES[c.cls < 23 ? c.cls : 0], "", 0, 0, 0, 0, nullptr, nullptr, 90, 90, 100, 0.4f, 1});
        w.push_back({X0 + 0.45f, Y0 + 0.13f, 0.33f, 0.05f, Money(c.price), "", 0, 0, 0, 0, nullptr, nullptr, 30, 140, 60, 0.7f, 1});
        int owned = 0; for (auto &g : g_garage) if (g.model == c.model) owned++;
        std::string info = c.weapon ? "Weaponized vehicle." : AirOrSea(c.cls) ? (c.cls == 14 ? "Delivered to your marina berth." : "Delivered to your hangar.") : "Delivered to a free slot in your garages.";
        if (owned) info += "  You own " + std::to_string(owned) + ".";
        w.push_back({X0 + 0.45f, Y0 + 0.2f, 0.33f, 0.04f, info, "", 0, 0, 0, 0, nullptr, nullptr, 60, 60, 70, 0.33f, 1});
        const CatCar *pc = &c;
        BuyButton(w, X0 + 0.45f, Y0 + 0.28f, 0.2f, 0.06f, 1000 + pg.a, c.price, [pc] { int before = (int)g_garage.size(); BuyCar(pc); if ((int)g_garage.size() > before) WebToast(pc->name + " purchased!"); });
        break;
    }
    case WP_ESTATE: {
        bool d8 = pg.a == 0;
        url = d8 ? "www.dynasty8realestate.com" : "www.mazebank.com/foreclosures"; title = d8 ? "Dynasty 8 Real Estate" : "Maze Bank Foreclosures";
        tr = d8 ? 20 : 190; tg = d8 ? 120 : 20; tb = d8 ? 70 : 40;
        static const int resid[] = {T_APT, T_HOUSE, T_MANSION, T_PENTHOUSE};
        static const int biz[] = {T_OFFICE, T_CLUB, T_METH, T_WEED, T_COKE, T_CASH, T_DOCS, T_BUNKER, T_HANGAR, T_FACILITY, T_NIGHTCLUB, T_ARCADE, T_AUTOSHOP, T_AGENCY, T_VEHWARE, T_KOSATKA, T_DATACENTER};
        std::vector<int> types = d8 ? std::vector<int>(std::begin(resid), std::end(resid)) : std::vector<int>(std::begin(biz), std::end(biz));
        float tw = 0.185f, th = 0.11f;
        for (size_t k = 0; k < types.size(); ++k) {
            int t = types[k], n = 0, owned = 0;
            for (int i = 0; i < NPROPS; ++i) if (PROPS[i].type == t) { n++; if (Owns(PROPS[i].name)) owned++; }
            float x = X0 + 0.01f + (k % 4) * (tw + 0.01f), y = Y0 + (k / 4) * (th + 0.015f);
            w.push_back({x, y, tw, th, TYPES[t].label, std::to_string(n) + " listings" + (owned ? " - " + std::to_string(owned) + " owned" : ""), tr, tg, tb, 255, nullptr, [t] { WebGo(WP_PTYPE, t); }, 255, 255, 255, 0.4f, 0});
        }
        break;
    }
    case WP_PTYPE: {
        int t = pg.a; bool d8 = TYPES[t].residence || t == T_GARAGE;
        url = d8 ? "www.dynasty8realestate.com/listings" : "www.mazebank.com/foreclosures/listings"; title = TYPES[t].label;
        tr = d8 ? 20 : 190; tg = d8 ? 120 : 20; tb = d8 ? 70 : 40;
        std::vector<int> list; for (int i = 0; i < NPROPS; ++i) if (PROPS[i].type == t) list.push_back(i);
        const int perPage = 7; int maxScroll = std::max(0, ((int)list.size() - 1) / perPage);
        pg.scroll = std::min(pg.scroll, maxScroll);
        for (int k = pg.scroll * perPage; k < (int)list.size() && k < (pg.scroll + 1) * perPage; ++k) {
            int i = list[k], row = k - pg.scroll * perPage; float y = Y0 + row * 0.085f;
            bool owned = Owns(PROPS[i].name);
            int gar = GarageSlots(i);
            w.push_back({X0, y, 0.44f, 0.075f, PROPS[i].name, gar ? std::to_string(gar) + "-vehicle garage" : TYPES[t].label, 245, 245, 248, 255, nullptr, nullptr, 20, 20, 30, 0.42f, 1});
            w.push_back({X0 + 0.45f, y + 0.015f, 0.1f, 0.045f, "WAYPOINT", "", 70, 70, 85, 255, nullptr, [i] { Vec3 e = PropExt(i); N(N_SET_NEW_WAYPOINT, e.x, e.y); WebToast("Waypoint set."); }, 255, 255, 255, 0.32f, 0});
            if (owned) {
                w.push_back({X0 + 0.56f, y + 0.015f, 0.1f, 0.045f, "OWNED", "", 30, 150, 60, 255, nullptr, nullptr, 255, 255, 255, 0.32f, 0});
                bool cust = false; for (const CustCat &c : CUSTOM) if (CatFor(i, c)) cust = true;
                if (cust) w.push_back({X0 + 0.67f, y + 0.015f, 0.12f, 0.045f, "UPGRADES", "", 110, 40, 160, 255, nullptr, [i] { WebGo(WP_CUSTOM, i); }, 255, 255, 255, 0.32f, 0});
            } else BuyButton(w, X0 + 0.56f, y + 0.015f, 0.23f, 0.045f, 2000 + i, PROPS[i].price, [i] { BuyProperty(i); if (Owns(PROPS[i].name)) WebToast(std::string(PROPS[i].name) + " purchased!"); });
        }
        if (maxScroll > 0) {
            w.push_back({X1 - 0.16f, Y1 - 0.04f, 0.075f, 0.035f, "< PREV", "", 60, 60, 70, 255, nullptr, [] { WebPage &p = g_webStack.back(); if (p.scroll > 0) p.scroll--; }, 255, 255, 255, 0.3f, 0});
            w.push_back({X1 - 0.08f, Y1 - 0.04f, 0.075f, 0.035f, "NEXT >", "", 60, 60, 70, 255, nullptr, [maxScroll] { WebPage &p = g_webStack.back(); if (p.scroll < maxScroll) p.scroll++; }, 255, 255, 255, 0.3f, 0});
        }
        break;
    }
    case WP_SECURO: {
        int pi = pg.a;
        url = "www.securoserv.net/ceo"; title = "SecuroServ"; tr = 160; tg = 12; tb = 20;
        BizState *b = BizOf(PROPS[pi].name);
        int crates = b ? (int)b->stock : 0, safe = b ? b->safe : 0;
        w.push_back({X0, Y0, 0.8f, 0.05f, std::string("Welcome back, CEO.  ") + PROPS[pi].name, "", 0, 0, 0, 0, nullptr, nullptr, 30, 30, 34, 0.5f, 1});
        int vw = OwnedOfType(T_VEHWARE); BizState *vb = vw >= 0 ? BizOf(PROPS[vw].name) : nullptr;
        struct T { std::string n, s; int r, g, b; std::function<void()> act; };
        std::vector<T> tiles = {
            {"Special Cargo", std::to_string(crates) + "/" + std::to_string(CargoMax(T_OFFICE)) + " crates - worth " + Money(crates * CargoValue(T_OFFICE)), 150, 15, 22, [pi] { WebGo(WP_SECCARGO, pi); }},
            {"Vehicle Cargo", vw < 0 ? "Buy a Vehicle Warehouse" : std::to_string(vb ? (int)vb->stock : 0) + "/" + std::to_string(CargoMax(T_VEHWARE)) + " vehicles", 60, 60, 70,
             [vw] { if (vw >= 0) WebGo(WP_SECCARGO, vw); else WebGo(WP_PTYPE, T_VEHWARE); }},
            {"Office Safe", Money(safe), 40, 110, 60, [pi] {
                BizState *s = BizOf(PROPS[pi].name);
                if (!s || s->safe <= 0) { WebToast("The safe is empty."); return; }
                AddCash(s->safe); WebToast("Collected " + Money(s->safe)); s->safe = 0; Save(); }},
            {"Office & Garage Upgrades", "Styles, decor, lighting, signage", 110, 40, 160, [pi] { WebGo(WP_CUSTOM, pi); }},
            {"Maze Bank Foreclosures", "Warehouses and businesses", 190, 20, 40, [] { WebGo(WP_ESTATE, 1); }},
            {"Vehicle Garage", "Take the elevator down", 25, 70, 150, [pi] { SecLeave([pi] { int p = pi; g_inside = -1; EnterGarage(p, 0); }); }},
        };
        float tw = 0.255f, th = 0.19f;
        for (size_t k = 0; k < tiles.size(); ++k) {
            float x = X0 + (k % 3) * (tw + 0.018f), y = Y0 + 0.08f + (k / 3) * (th + 0.025f);
            w.push_back({x, y, tw, th, tiles[k].n, tiles[k].s, tiles[k].r, tiles[k].g, tiles[k].b, 255, nullptr, tiles[k].act, 255, 255, 255, 0.5f, 0});
        }
        w.push_back({X0, Y1 - 0.045f, 0.8f, 0.04f, "SecuroServ - protecting assets since 2011.  Backspace / B to log off.", "", 0, 0, 0, 0, nullptr, nullptr, 110, 110, 118, 0.3f, 1});
        break;
    }
    case WP_DATANET: {   // the data center's terminal
        int pi = pg.a;
        url = "darknet://gori-data/ops"; title = "DataNet"; tr = 0; tg = 120; tb = 200;
        BizState *b = BizOf(PROPS[pi].name);
        float stock = b ? b->stock : 0.f, sup = b ? b->supplies : 0.f; int safe = b ? b->safe : 0;
        const BizInfo *info = ProductionOf(T_DATACENTER);
        int value = info ? (int)(info->fullValue * stock / 100.f) : 0;
        char speed[16]; std::snprintf(speed, sizeof speed, "x%.1f", DcSpeed(pi));
        w.push_back({X0, Y0, 0.8f, 0.05f, std::string("Gori Data - ") + PROPS[pi].name + "   production speed " + speed, "", 0, 0, 0, 0, nullptr, nullptr, 30, 30, 34, 0.45f, 1});
        auto bar = [&](float y, const char *label, float pct, int r, int g, int bb) {
            w.push_back({X0, y, 0.3f, 0.04f, std::string(label) + " " + std::to_string((int)pct) + "%", "", 0, 0, 0, 0, nullptr, nullptr, 40, 40, 48, 0.4f, 1});
            w.push_back({X0 + 0.31f, y + 0.006f, 0.45f, 0.028f, "", "", 45, 45, 52, 255, nullptr, nullptr, 0, 0, 0, 0.f, 0});
            if (pct > 0.f) w.push_back({X0 + 0.31f, y + 0.006f, 0.45f * std::min(1.f, pct / 100.f), 0.028f, "", "", r, g, bb, 255, nullptr, nullptr, 0, 0, 0, 0.f, 0});
        };
        bar(Y0 + 0.07f, "Encrypted data", stock, 0, 170, 255);
        bar(Y0 + 0.12f, "Hardware supplies", sup, 230, 160, 20);
        struct T { std::string n, s; int r, g, b; std::function<void()> act; };
        std::vector<T> tiles = {
            {"Sell Data", Money(value), 40, 150, 60, [pi, value] { if (value <= 0) { WebToast("No data to sell yet."); return; } SecLeave([pi] { SellProduct(pi); }); }},
            {"Buy Supplies", Money(SUPPLY_COST), 230, 140, 20, [pi] {
                BizState *s = BizOf(PROPS[pi].name);
                if (s && s->supplies >= 99.f) { WebToast("Supplies are already full."); return; }
                PayWhat what("business supplies (property)");
                g_payFromBank = true; bool ok = s && Pay(SUPPLY_COST); g_payFromBank = false;
                if (ok) { s->supplies = 100.f; Save(); WebToast("Supplies delivered."); } }},
            {"Steal Supplies", "Hijack a hardware shipment (+40%)", 160, 60, 30, [pi] { SecLeave([pi] { StealSupplies(pi); }); }},
            {"Hacking Contract", "Break into a corporate server", 0, 120, 200, [pi] { SecLeave([pi] { HackContract(pi); }); }},
            {"Crypto Wallet", Money(safe) + " mined", 120, 60, 170, [pi] {
                BizState *s = BizOf(PROPS[pi].name);
                if (!s || s->safe <= 0) { WebToast("Nothing mined yet."); return; }
                AddCash(s->safe); WebToast("Cashed out " + Money(s->safe)); s->safe = 0; Save(); }},
            {"Upgrades", "Equipment, staff, security, lighting", 60, 60, 70, [pi] { WebGo(WP_CUSTOM, pi); }},
        };
        float tw = 0.255f, th = 0.15f;
        for (size_t k = 0; k < tiles.size(); ++k) {
            float x = X0 + (k % 3) * (tw + 0.018f), y = Y0 + 0.19f + (k / 3) * (th + 0.02f);
            w.push_back({x, y, tw, th, tiles[k].n, tiles[k].s, tiles[k].r, tiles[k].g, tiles[k].b, 255, nullptr, tiles[k].act, 255, 255, 255, 0.46f, 0});
        }
        w.push_back({X0, Y1 - 0.045f, 0.8f, 0.04f, "Upgrades apply the next time you walk in.  Backspace / B to log off.", "", 0, 0, 0, 0, nullptr, nullptr, 110, 110, 118, 0.3f, 1});
        break;
    }
    case WP_SECCARGO: {
        int pi = pg.a, type = PROPS[pi].type;
        bool veh = type == T_VEHWARE;
        url = veh ? "www.securoserv.net/ceo/vehiclecargo" : "www.securoserv.net/ceo/specialcargo"; title = veh ? "Vehicle Cargo" : "Special Cargo"; tr = 160; tg = 12; tb = 20;
        BizState *b = BizOf(PROPS[pi].name);
        int n = b ? (int)b->stock : 0, max = CargoMax(type), value = veh ? CargoValue(type) : n * CargoValue(type);
        w.push_back({X0, Y0, 0.5f, 0.05f, PROPS[pi].name, "", 0, 0, 0, 0, nullptr, nullptr, 30, 30, 34, 0.55f, 1});
        w.push_back({X0, Y0 + 0.07f, 0.5f, 0.04f, std::string(veh ? "Vehicles in stock: " : "Crates in stock: ") + std::to_string(n) + "/" + std::to_string(max), "", 0, 0, 0, 0, nullptr, nullptr, 60, 60, 70, 0.42f, 1});
        // stock bar
        w.push_back({X0, Y0 + 0.125f, 0.5f, 0.03f, "", "", 45, 45, 52, 255, nullptr, nullptr, 0, 0, 0, 0.f, 0});
        if (n > 0) w.push_back({X0, Y0 + 0.125f, 0.5f * std::min(1.f, n / (float)max), 0.03f, "", "", 190, 25, 35, 255, nullptr, nullptr, 0, 0, 0, 0.f, 0});
        w.push_back({X0, Y0 + 0.17f, 0.5f, 0.04f, std::string(veh ? "Export value: " : "Sale value: ") + Money(value), "", 0, 0, 0, 0, nullptr, nullptr, 30, 140, 60, 0.45f, 1});
        w.push_back({X0, Y0 + 0.26f, 0.24f, 0.07f, veh ? "SOURCE VEHICLE" : "SOURCE CARGO", "Steal more stock", 150, 15, 22, 255, nullptr, [pi] { SecLeave([pi] { SourceCargo(pi); }); }, 255, 255, 255, 0.42f, 0});
        w.push_back({X0 + 0.26f, Y0 + 0.26f, 0.24f, 0.07f, veh ? "EXPORT VEHICLE" : "SELL CARGO", Money(value), n > 0 ? 40 : 70, n > 0 ? 150 : 70, n > 0 ? 60 : 80, 255, nullptr,
                     [pi, n] { if (n <= 0) { WebToast("Nothing to sell yet."); return; } SecLeave([pi] { SellCargo(pi); }); }, 255, 255, 255, 0.42f, 0});
        w.push_back({X0 + 0.55f, Y0, 0.25f, 0.33f, "", "", 235, 235, 238, 255, nullptr, nullptr, 0, 0, 0, 0.f, 0});
        w.push_back({X0 + 0.56f, Y0 + 0.01f, 0.23f, 0.31f, veh ? "Steal top cars, export them."
                                                              : "Steal crates, sell them in bulk.",
                     "", 0, 0, 0, 0, nullptr, nullptr, 50, 50, 58, 0.34f, 1});
        break;
    }
    case WP_MYPROPS: {
        url = "www.eyefind.info/myproperties"; title = "My Properties"; tr = 110; tg = 40; tb = 160;
        std::vector<int> list; for (auto &o : g_props) { int i = FindProp(o); if (i >= 0) list.push_back(i); }
        const int perPage = 7; int maxScroll = std::max(0, ((int)list.size() - 1) / perPage);
        pg.scroll = std::min(pg.scroll, maxScroll);
        for (int k = pg.scroll * perPage; k < (int)list.size() && k < (pg.scroll + 1) * perPage; ++k) {
            int i = list[k], row = k - pg.scroll * perPage; float y = Y0 + row * 0.085f;
            w.push_back({X0, y, 0.44f, 0.075f, PROPS[i].name, TYPES[PROPS[i].type].label, 245, 245, 248, 255, nullptr, nullptr, 20, 20, 30, 0.42f, 1});
            w.push_back({X0 + 0.45f, y + 0.015f, 0.1f, 0.045f, "WAYPOINT", "", 70, 70, 85, 255, nullptr, [i] { Vec3 e = PropExt(i); N(N_SET_NEW_WAYPOINT, e.x, e.y); WebToast("Waypoint set."); }, 255, 255, 255, 0.32f, 0});
            bool cust = false; for (const CustCat &c : CUSTOM) if (CatFor(i, c)) cust = true;
            if (cust) w.push_back({X0 + 0.56f, y + 0.015f, 0.12f, 0.045f, "UPGRADES", "", 110, 40, 160, 255, nullptr, [i] { WebGo(WP_CUSTOM, i); }, 255, 255, 255, 0.32f, 0});
        }
        if (maxScroll > 0) {
            w.push_back({X1 - 0.16f, Y1 - 0.04f, 0.075f, 0.035f, "< PREV", "", 60, 60, 70, 255, nullptr, [] { WebPage &p = g_webStack.back(); if (p.scroll > 0) p.scroll--; }, 255, 255, 255, 0.3f, 0});
            w.push_back({X1 - 0.08f, Y1 - 0.04f, 0.075f, 0.035f, "NEXT >", "", 60, 60, 70, 255, nullptr, [maxScroll] { WebPage &p = g_webStack.back(); if (p.scroll < maxScroll) p.scroll++; }, 255, 255, 255, 0.3f, 0});
        }
        break;
    }
    case WP_CUSTOM: {
        int pi = pg.a;
        url = "www.eyefind.info/myproperties/upgrades"; title = PROPS[pi].name; tr = 110; tg = 40; tb = 160;
        std::vector<const CustCat *> cats; for (const CustCat &c : CUSTOM) if (CatFor(pi, c)) cats.push_back(&c);
        int sel = std::min((int)cats.size() - 1, std::max(0, pg.b));
        for (size_t k = 0; k < cats.size(); ++k) {
            bool s = (int)k == sel;
            w.push_back({X0, Y0 + k * 0.05f, 0.18f, 0.044f, cats[k]->name, "", s ? 110 : 60, s ? 40 : 60, s ? 160 : 70, 255, nullptr, [k] { WebPage &p = g_webStack.back(); p.b = (int)k; }, 255, 255, 255, 0.34f, 0});
        }
        if (cats.empty()) break;
        const CustCat &c = *cats[sel];
        int cur = CustChoice(pi, c);
        for (int k = 0; k < c.n; ++k) {
            float x = X0 + 0.2f + (k % 3) * 0.195f, y = Y0 + (k / 3) * 0.085f;
            bool is = k == cur;
            std::string key = std::string(PROPS[pi].name) + "|" + c.name;
            const CustOpt &o = c.opts[k];
            const CustCat *pc = &c; const CustOpt *po = &o;
            w.push_back({x, y, 0.185f, 0.075f, o.label, is ? "INSTALLED" : o.price ? Money(o.price) : "FREE", is ? 30 : 245, is ? 150 : 245, is ? 60 : 248, 255, nullptr,
                         [key, k, pc, po, pi] {
                             if (CustChoice(pi, *pc) == k) return;
                             if (po->price) { PayWhat what("property upgrade"); g_payFromBank = true; bool paid = Pay(po->price); g_payFromBank = false; if (!paid) return; }
                             g_cust[key] = k; Save();
                             WebToast(std::string(po->label) + " installed. You'll see it next time you go inside.");
                         }, is ? 255 : 20, is ? 255 : 20, is ? 255 : 30, 0.36f, 0});
        }
        break;
    }
    }
}
static bool g_realWebDefault = true;   // the game's own browser (Web.inc); false: the drawn eyefind below
static bool RealWebOpen(const std::string &site);
static void RealWebTick();
static bool RealWebIsOpen();
static void InternetTick() {
    // the phone's browser opens ours
    static uint32_t app = Joaat("appinternet");
    if (N<int>(N_GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH, app) > 0) {
        N(N_TERMINATE_ALL_SCRIPTS_WITH_THIS_NAME, "appinternet");
        N(N_DESTROY_MOBILE_PHONE);
        if (!g_realWebDefault || !RealWebOpen("WWW_EYEFIND_INFO")) OpenInternet();
    }
    if (RealWebIsOpen()) { RealWebTick(); return; }
    if (!g_webOpen) return;
    N(N_DISABLE_ALL_CONTROL_ACTIONS, 0);
    for (int c : {239, 240, 237, 238, 241, 242, 1, 2, 187, 188, 189, 190, 201, 202, 199, 200, 194}) N(N_ENABLE_CONTROL_ACTION, 0, c, 1);
    N(N_SET_MOUSE_CURSOR_THIS_FRAME);
    N(N_HIDE_HUD_AND_RADAR_THIS_FRAME);
    std::vector<Widget> w; std::string url, title; int tr, tg, tb;
    BuildWeb(w, url, title, tr, tg, tb);
    if (!g_webOpen) return;
    // frame: dim, window, address bar, banner
    Rect(0.5f, 0.5f, 1.f, 1.f, 0, 0, 0, 160);
    Rect(0.5f, 0.5f, 0.84f, 0.88f, 236, 238, 242, 252);
    Rect(0.5f, 0.085f, 0.84f, 0.05f, 45, 48, 58, 255);
    Text("<", 0.095f, 0.066f, 0.45f, 0, 255, 255, 255, 255, 0);
    Rect(0.53f, 0.085f, 0.62f, 0.032f, 250, 250, 252, 255);
    Text(url, 0.225f, 0.072f, 0.32f, 0, 40, 40, 50, 255, 1);
    Text("X", 0.905f, 0.066f, 0.45f, 0, 255, 255, 255, 255, 0);
    Rect(0.5f, 0.155f, 0.84f, 0.09f, tr, tg, tb, 255);
    Text(title, 0.11f, 0.125f, 0.8f, 1, 255, 255, 255, 255, 1);
    long long bank = BankBalance();
    Text("MAZE BANK: " + Money(bank >= 0 ? (int)std::min(bank, 2147483000LL) : GetCash()), 0.89f, 0.14f, 0.38f, 0, 255, 255, 255, 255, 2, 0.89f);
    // widgets
    float mx = N<float>(N_GET_DISABLED_CONTROL_NORMAL, 0, 239), my = N<float>(N_GET_DISABLED_CONTROL_NORMAL, 0, 240);
    static float lmx = -1, lmy = -1;
    bool mouseMoved = std::fabs(mx - lmx) > 0.002f || std::fabs(my - lmy) > 0.002f; lmx = mx; lmy = my;
    int hover = -1;
    for (int k = 0; k < (int)w.size(); ++k) if (w[k].act && mx >= w[k].x && mx <= w[k].x + w[k].w && my >= w[k].y && my <= w[k].y + w[k].h) hover = k;
    if (mouseMoved && hover >= 0) g_webFocus = hover;
    // keyboard / pad focus moves to the nearest clickable widget in that direction
    int dir = NB(N<int>(N_IS_CONTROL_JUST_PRESSED, 0, 188)) ? 0 : NB(N<int>(N_IS_CONTROL_JUST_PRESSED, 0, 187)) ? 1 : NB(N<int>(N_IS_CONTROL_JUST_PRESSED, 0, 189)) ? 2 : NB(N<int>(N_IS_CONTROL_JUST_PRESSED, 0, 190)) ? 3 : -1;
    if (dir >= 0) {
        if (g_webFocus < 0 || g_webFocus >= (int)w.size() || !w[g_webFocus].act) { for (int k = 0; k < (int)w.size(); ++k) if (w[k].act) { g_webFocus = k; break; } }
        else {
            const Widget &f = w[g_webFocus]; float fx = f.x + f.w / 2, fy = f.y + f.h / 2, best = 1e9f; int bi = -1;
            for (int k = 0; k < (int)w.size(); ++k) {
                if (k == g_webFocus || !w[k].act) continue;
                float dx = w[k].x + w[k].w / 2 - fx, dy = w[k].y + w[k].h / 2 - fy;
                bool ok = dir == 0 ? dy < -0.01f : dir == 1 ? dy > 0.01f : dir == 2 ? dx < -0.01f : dx > 0.01f;
                if (!ok) continue;
                float d = (dir < 2) ? std::fabs(dy) + std::fabs(dx) * 2.5f : std::fabs(dx) + std::fabs(dy) * 2.5f;
                if (d < best) { best = d; bi = k; }
            }
            if (bi >= 0) g_webFocus = bi;
        }
        Sound("NAV_UP_DOWN", "HUD_FRONTEND_DEFAULT_SOUNDSET");
    }
    for (int k = 0; k < (int)w.size(); ++k) {
        const Widget &x = w[k];
        bool f = k == g_webFocus && x.act;
        if (x.a) Rect(x.x + x.w / 2, x.y + x.h / 2, x.w, x.h, x.r, x.g, x.b, x.a);
        if (f) { Rect(x.x + x.w / 2, x.y, x.w, 0.004f, 255, 200, 40, 255); Rect(x.x + x.w / 2, x.y + x.h, x.w, 0.004f, 255, 200, 40, 255);
                 Rect(x.x, x.y + x.h / 2, 0.0025f, x.h, 255, 200, 40, 255); Rect(x.x + x.w, x.y + x.h / 2, 0.0025f, x.h, 255, 200, 40, 255); }
        float textY = x.y + 0.008f;
        if (x.img) {
            N(N_REQUEST_STREAMED_TEXTURE_DICT, x.img->txd, 0);
            float ih = x.text.empty() ? x.h : x.h * 0.68f;
            if (NB(N<int>(N_HAS_STREAMED_TEXTURE_DICT_LOADED, x.img->txd))) N(N_DRAW_SPRITE, x.img->txd, x.img->tex, x.x + x.w / 2, x.y + ih / 2, x.w * 0.96f, ih * 0.96f, 0.f, 255, 255, 255, 255, 0, 0);
            else Rect(x.x + x.w / 2, x.y + ih / 2, x.w * 0.96f, ih * 0.96f, 200, 202, 210, 255);
            textY = x.y + ih + 0.004f;
        } else if (!x.text.empty() && x.align == 1 && x.a && x.h > 0.2f) {   // vehicle card without a picture
            Rect(x.x + x.w / 2, x.y + x.h * 0.34f, x.w * 0.96f, x.h * 0.64f, 205, 208, 216, 255);
            Text("NO IMAGE", x.x + x.w / 2, x.y + x.h * 0.3f, 0.3f, 0, 120, 120, 130, 255, 0);
            textY = x.y + x.h * 0.68f + 0.004f;
        }
        if (!x.text.empty()) {
            if (x.align == 0) {
                float ty = x.sub.empty() ? x.y + x.h / 2 - x.ts * 0.036f : x.y + x.h / 2 - x.ts * 0.055f;
                Text(x.text, x.x + x.w / 2, ty, x.ts, 4, x.tr, x.tg, x.tb, 255, 0);
                if (!x.sub.empty()) Text(x.sub, x.x + x.w / 2, ty + x.ts * 0.07f, x.ts * 0.7f, 0, x.tr, x.tg, x.tb, 220, 0);
            } else {
                N(N_SET_TEXT_WRAP, x.x + 0.006f, x.x + x.w - 0.004f);
                Text(x.text, x.x + 0.006f, textY, x.ts, 4, x.tr, x.tg, x.tb, 255, 1);
                if (!x.sub.empty()) Text(x.sub, x.x + 0.006f, textY + x.ts * 0.075f, x.ts * 0.85f, 0, 40, 130, 60, 255, 1);
            }
        }
    }
    if (!g_webToast.empty() && Now() < g_webToastUntil) { Rect(0.5f, 0.925f, 0.4f, 0.035f, 30, 30, 40, 235); Text(g_webToast, 0.5f, 0.912f, 0.34f, 0, 255, 255, 255, 255, 0); }
    // input
    bool click = NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 0, 237)) || NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 0, 24));
    bool accept = NB(N<int>(N_IS_CONTROL_JUST_PRESSED, 0, 201)) || NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 0, 201));
    bool back = NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 0, 202)) || NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 0, 238)) || NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 0, 199)) || NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 0, 200));
    if (click) {
        if (mx < 0.11f && my < 0.11f) { WebBack(); return; }
        if (mx > 0.89f && my < 0.11f) { g_webOpen = false; return; }
        if (hover >= 0) { Sound("SELECT", "HUD_FRONTEND_DEFAULT_SOUNDSET"); std::function<void()> a = w[hover].act; a(); return; }
    }
    if (accept && g_webFocus >= 0 && g_webFocus < (int)w.size() && w[g_webFocus].act) { Sound("SELECT", "HUD_FRONTEND_DEFAULT_SOUNDSET"); std::function<void()> a = w[g_webFocus].act; a(); return; }
    if (back) { WebBack(); return; }
    WebPage &pg = g_webStack.back();
    if (NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 0, 241)) && pg.scroll > 0) pg.scroll--;
    if (NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 0, 242))) pg.scroll++;
}
