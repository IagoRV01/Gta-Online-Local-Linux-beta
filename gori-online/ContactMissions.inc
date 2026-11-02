// ---------------------------------------------------------------- GTA Online's contact missions
// The contacts' jobs (Gerald, Simeon, Lamar, Martin, Ron, Trevor, Lester, Agent 14, SecuroServ, Ms. Baker): GTA Online's names, ranks and player counts, recreated
// with Gori's mission steps from a few templates (eliminate, steal a vehicle, grab a package, destroy, rescue, hold up).
// Like Online they unlock by rank and pay more the higher the rank needed; every one is a co-op job.
// Interaction Menu > Contact Missions, or call the contact.
enum CmKind { CM_ELIM, CM_STEAL, CM_PACKAGE, CM_DESTROY, CM_RESCUE, CM_ROB };
struct ContactMission {
    const char *name; int kind, rank, pay, players;
    float x, y, z;                 // the target
    const char *enemy, *weapon;    // the opposition
    const char *vehicle;           // CM_STEAL: what to steal
    const char *text;              // the briefing
};
struct ContactDef { const char *name; int phoneId; const char *greeting; const ContactMission *missions; int count; };
static const ContactMission CM_GERALD[] = {
    {"Learning the Ropes", CM_PACKAGE, 1, 5000, 4, -50.f, -1450.f, 32.f, "g_m_y_mexgoon_01", "weapon_pistol", nullptr, "Gerald: \"The Vagos are sitting on a bag of mine in Strawberry. Prove you can fetch.\""},
    {"Pier Pressure", CM_PACKAGE, 6, 6000, 4, -1820.f, -1210.f, 13.f, "g_m_y_mexgoon_02", "weapon_pistol", nullptr, "Gerald: \"My stash is under the pier and the Vagos found it. Go get it.\""},
    {"Death Metal", CM_ELIM, 6, 6000, 4, -450.f, -1690.f, 19.f, "g_m_y_ballaeast_01", "weapon_microsmg", nullptr, "Gerald: \"Ballas are doing business in the La Puerta scrapyard. Correct them.\""},
    {"Deal Breaker", CM_ELIM, 6, 6000, 4, 1290.f, -1700.f, 61.f, "g_m_y_mexgoon_01", "weapon_microsmg", nullptr, "Gerald: \"A deal's going down in El Burro without me. Break it up.\""},
    {"Flood in the LS River", CM_PACKAGE, 6, 6000, 4, 660.f, -1400.f, 10.f, "g_m_y_mexgoon_03", "weapon_microsmg", nullptr, "Gerald: \"Product went in the river and the wrong people are fishing for it.\""},
    {"Meth'd Up", CM_DESTROY, 7, 6500, 4, 2430.f, 4970.f, 46.f, "a_m_m_hillbilly_01", "weapon_pumpshotgun", nullptr, "Gerald: \"Cooks on a Grapeseed farm are cutting into my trade. Burn it down.\""},
    {"No Hay Bronca", CM_ELIM, 8, 6500, 4, 470.f, -1310.f, 29.f, "g_m_y_mexgoon_02", "weapon_microsmg", nullptr, "Gerald: \"The Vagos want to talk. They don't get to talk.\""},
    {"Gassed Up", CM_DESTROY, 12, 7500, 4, -70.f, -1760.f, 29.f, "g_m_y_ballaorig_01", "weapon_microsmg", nullptr, "Gerald: \"Ballas run their dealing out of a Davis gas station. Light it up.\""},
    {"Hit 'Em Up", CM_ELIM, 14, 8000, 4, 100.f, -1940.f, 20.f, "g_m_y_ballaorig_01", "weapon_microsmg", nullptr, "Gerald: \"Time to hit Grove Street's new tenants. Hard.\""},
    {"Violent Duct", CM_DESTROY, 15, 8500, 4, 1400.f, -2200.f, 60.f, "g_m_y_mexgoon_03", "weapon_assaultrifle", nullptr, "Gerald: \"They hide the shipment at the Murrieta oil fields. Blow the pipes.\""},
    {"Hard Labor", CM_ELIM, 19, 9500, 4, -150.f, -950.f, 29.f, "s_m_y_construct_01", "weapon_smg", nullptr, "Gerald: \"A crew on that construction site owes me. Collect in blood.\""},
    {"War and Pieces", CM_DESTROY, 30, 12500, 4, 840.f, -2200.f, 30.f, "g_m_y_mexgoon_01", "weapon_assaultrifle", nullptr, "Gerald: \"Guns stacked up in Cypress Flats. Make them scrap.\""},
    {"Chumash and Grab", CM_PACKAGE, 35, 13500, 4, -3170.f, 1100.f, 21.f, "g_m_y_lost_01", "weapon_assaultrifle", nullptr, "Gerald: \"Bikers have my merch at Chumash. Grab it and go.\""},
    {"Dish the Dirt", CM_PACKAGE, 45, 16000, 4, 2000.f, 4900.f, 42.5f, "a_m_m_hillbilly_02", "weapon_assaultrifle", nullptr, "Gerald: \"A Grapeseed farmer's got dirt on me. Bring it here.\""},
    {"Bad Companies", CM_ELIM, 1, 20000, 4, 1705.f, 4720.f, 42.f, "a_m_m_hillbilly_01", "weapon_pumpshotgun", nullptr, "Gerald: \"Some company's muscling into Grapeseed. This is my last play. Help me.\""},
    {"Deal With It", CM_ELIM, 1, 20000, 4, -700.f, -200.f, 37.f, "s_m_m_highsec_01", "weapon_carbinerifle", nullptr, "Gerald: \"Rich boys in Rockford Hills cut me out of a deal. Deal with it.\""},
    {"End Product", CM_DESTROY, 1, 20000, 4, 1150.f, -1300.f, 35.f, "g_m_y_mexgoon_01", "weapon_assaultrifle", nullptr, "Gerald: \"The Vagos' end product is stored in Murrieta Heights. End it.\""},
    {"Fast Peddling", CM_STEAL, 1, 20000, 4, -1150.f, -1060.f, 2.f, "g_m_y_korean_01", "weapon_microsmg", "bf400", "Gerald: \"A fast bike in the Canals with my stuff on it. Go get it.\""},
    {"Go Figure", CM_PACKAGE, 1, 20000, 4, 870.f, -1000.f, 30.f, "g_m_y_mexgoon_02", "weapon_microsmg", nullptr, "Gerald: \"Somebody's books don't add up. Bring me the ledger. Go figure.\""},
    {"Make Ends Meat", CM_DESTROY, 1, 20000, 4, 960.f, -2170.f, 30.f, "s_m_y_dockwork_01", "weapon_assaultrifle", nullptr, "Gerald: \"They're cutting product at the meat plant. Close it down.\""},
};
static const ContactMission CM_SIMEON[] = {
    {"Where Credit's Due", CM_STEAL, 5, 6000, 2, 640.f, -440.f, 24.f, "g_m_y_lost_01", "weapon_sawnoffshotgun", "baller2", "Simeon: \"A customer stopped paying for his Baller. Repossess it, my friend.\""},
    {"Rockford Roll", CM_STEAL, 5, 6000, 2, -630.f, -230.f, 38.f, "s_m_m_security_01", "weapon_pistol", "cogcabrio", "Simeon: \"A very nice Cognoscenti in Rockford Hills. Overdue. Very sad.\""},
    {"Rich Men in Richman", CM_PACKAGE, 5, 6000, 4, -1525.f, 80.f, 56.f, "s_m_m_highsec_01", "weapon_carbinerifle", nullptr, "Simeon: \"A rich boy keeps the title deeds in his mansion. Bring them to me.\""},
    {"Chasers", CM_STEAL, 5, 6000, 2, 300.f, 200.f, 104.f, "g_m_y_lost_02", "weapon_pumpshotgun", "sultan", "Simeon: \"A Sultan in Vinewood. They will chase you. Do not scratch it.\""},
    {"It Takes a Thief", CM_STEAL, 5, 6000, 4, -1150.f, -1520.f, 4.f, "g_m_y_mexgoon_02", "weapon_microsmg", "penumbra", "Simeon: \"Thieves stole my stolen car in Vespucci. Unbelievable.\""},
    {"Gentry Does It", CM_STEAL, 10, 7000, 4, -820.f, 180.f, 72.f, "s_m_m_security_01", "weapon_carbinerifle", "schafter2", "Simeon: \"A Schafter in Rockford Hills. Be gentle. It's very expensive.\""},
    {"All in the Game", CM_STEAL, 10, 7000, 2, 1090.f, -480.f, 65.f, "g_m_y_mexgoon_01", "weapon_microsmg", "buffalo2", "Simeon: \"A Buffalo in Mirror Park. This is business, my friend. All of it.\""},
    {"El Burro Heists", CM_STEAL, 10, 7000, 4, 1290.f, -1700.f, 61.f, "g_m_y_mexgoon_03", "weapon_assaultrifle", "sabregt", "Simeon: \"A Sabre at El Burro Heights. They think it's theirs. It's not.\""},
    {"Blow Up", CM_DESTROY, 12, 7500, 2, -200.f, -1300.f, 31.f, "s_m_m_security_01", "weapon_pistol", nullptr, "Simeon: \"Some customers don't pay. Make their cars... disappear.\""},
    {"GTA Today", CM_STEAL, 12, 7500, 4, -1600.f, -400.f, 40.f, "s_m_m_highsec_01", "weapon_carbinerifle", "stinger", "Simeon: \"A collector's Stinger. Today, not tomorrow!\""},
    {"Chasers II", CM_STEAL, 16, 8500, 2, 980.f, -110.f, 74.f, "g_m_y_lost_01", "weapon_assaultrifle", "dominator", "Simeon: \"Another one. The Lost have a Dominator of mine. They will be upset.\""},
    {"Blow Up II", CM_DESTROY, 20, 9500, 2, -1150.f, -1520.f, 4.f, "g_m_y_mexgoon_02", "weapon_microsmg", nullptr, "Simeon: \"Another bad customer. Vespucci this time.\""},
    {"Blow Up III", CM_DESTROY, 31, 12500, 2, 1220.f, -3000.f, 5.9f, "s_m_y_dockwork_01", "weapon_assaultrifle", nullptr, "Simeon: \"A whole shipment of lemons at the docks. Blow them up.\""},
    {"Blow Up IV", CM_DESTROY, 1, 20000, 4, -530.f, -1220.f, 18.f, "g_m_y_lost_01", "weapon_assaultrifle", nullptr, "Simeon: \"Yet another customer! La Puerta. You know what to do.\""},
    {"Sasquashed", CM_STEAL, 1, 20000, 4, 2700.f, 4280.f, 45.f, "a_m_m_hillbilly_01", "weapon_pumpshotgun", "sandking", "Simeon: \"A Sandking, up in the hills. It's very big. They are very angry.\""},
    {"Under the Hammer", CM_STEAL, 1, 20000, 4, -800.f, -200.f, 37.f, "s_m_m_highsec_01", "weapon_carbinerifle", "ztype", "Simeon: \"A Z-Type going to auction. It goes to me instead.\""},
    {"Do You Even Lift?", CM_STEAL, 1, 20000, 4, -145.f, -1400.f, 30.f, "g_m_y_mexgoon_01", "weapon_microsmg", "flatbed", "Simeon: \"A flatbed. Bring it, and don't drop what's on it.\""},
    {"GTA Today II", CM_STEAL, 1, 20000, 4, -1390.f, -420.f, 36.f, "s_m_m_security_01", "weapon_carbinerifle", "feltzer2", "Simeon: \"A Feltzer in Del Perro. Today, again. I have a buyer.\""},
    {"RV Nearly There?", CM_STEAL, 1, 20000, 4, 1390.f, 3600.f, 35.f, "g_m_y_lost_02", "weapon_sawnoffshotgun", "camper", "Simeon: \"A camper in Sandy Shores. Don't look inside. Are we there yet?\""},
    {"Burn Rate", CM_DESTROY, 1, 20000, 4, 1690.f, 3580.f, 35.f, "g_m_y_lost_01", "weapon_assaultrifle", nullptr, "Simeon: \"Unpaid cars in Sandy Shores. My insurance agent says burn them.\""},
    {"Simeonomics", CM_ELIM, 1, 20000, 4, -45.f, -1100.f, 26.f, "g_m_y_mexgoon_02", "weapon_assaultrifle", nullptr, "Simeon: \"Men came to my showroom about money. This is economics, my friend.\""},
};
static const ContactMission CM_LAMAR[] = {
    {"Mall or Nothing", CM_PACKAGE, 1, 5000, 4, -50.f, -1750.f, 29.f, "g_m_y_ballaorig_01", "weapon_pistol", nullptr, "Lamar: \"Some fool's sitting on our money at the Davis strip mall, homie.\""},
    {"Hold Up", CM_ROB, 1, 5000, 2, 25.f, -1345.f, 29.5f, nullptr, nullptr, nullptr, "Lamar: \"Quick money, dog. Stick up the store in Strawberry and bounce.\""},
    {"Ballas to the Wall", CM_ELIM, 5, 6000, 2, 100.f, -1940.f, 20.f, "g_m_y_ballaorig_01", "weapon_pistol", nullptr, "Lamar: \"Ballas on Grove Street. Not on my watch.\""},
    {"San Andreas Seoul", CM_ELIM, 16, 8500, 4, -620.f, -900.f, 25.f, "g_m_y_korean_01", "weapon_microsmg", nullptr, "Lamar: \"Koreans in Little Seoul disrespected the set. Pay 'em a visit.\""},
    {"Ticket to Elysium", CM_PACKAGE, 18, 9000, 4, 200.f, -3000.f, 6.f, "s_m_y_dockwork_01", "weapon_assaultrifle", nullptr, "Lamar: \"The merch is on Elysian Island, homie. Get in, get out.\""},
    {"Going Down the GOH", CM_ELIM, 20, 9500, 2, -2550.f, 2320.f, 33.f, "g_m_y_lost_02", "weapon_assaultrifle", nullptr, "Lamar: \"Lost boys are camped off the Great Ocean Highway. Go down there.\""},
    {"Caught Napping", CM_RESCUE, 20, 9500, 2, -1100.f, -1500.f, 4.f, "g_m_y_ballaeast_01", "weapon_microsmg", nullptr, "Lamar: \"They snatched my boy while he was sleeping. Go get him out.\""},
    {"Lost MC RIP", CM_ELIM, 25, 11000, 2, 975.f, -120.f, 74.f, "g_m_y_lost_01", "weapon_sawnoffshotgun", nullptr, "Lamar: \"The Lost at their clubhouse. Time to put 'em in the ground.\""},
    {"No Smoking", CM_DESTROY, 30, 12500, 4, -70.f, 6260.f, 31.f, "g_m_y_lost_02", "weapon_assaultrifle", nullptr, "Lamar: \"A factory up in Paleto's making product for our rivals. Smoke it.\""},
    {"Community Outreach", CM_ELIM, 1, 25000, 4, -130.f, -1600.f, 34.f, "g_m_y_ballaeast_01", "weapon_microsmg", nullptr, "Lamar: \"Time to reach out to the community in Chamberlain, homie.\""},
    {"Slow and Low", CM_STEAL, 1, 25000, 2, 140.f, -1700.f, 29.f, "g_m_y_ballaorig_01", "weapon_pistol", "voodoo", "Lamar: \"A Voodoo in Davis with hydraulics, dog. Roll it to Benny's.\""},
    {"It's a G Thing", CM_STEAL, 1, 25000, 2, -190.f, -1620.f, 34.f, "g_m_y_ballaeast_01", "weapon_microsmg", "primo2", "Lamar: \"Those fools got a Primo Custom. You wouldn't understand. Take it.\""},
    {"Funeral Party", CM_ELIM, 1, 25000, 4, -1680.f, -290.f, 52.f, "g_m_y_ballaorig_01", "weapon_assaultrifle", nullptr, "Lamar: \"Ballas are coming to the funeral at Hill Valley, homie. Be ready.\""},
    {"Lowrider Envy", CM_STEAL, 1, 25000, 2, 840.f, -2200.f, 30.f, "g_m_y_mexgoon_01", "weapon_microsmg", "chino2", "Lamar: \"The Vagos got a Chino that's better than mine. Not for long.\""},
    {"Point and Shoot", CM_ELIM, 1, 25000, 2, 250.f, -1800.f, 27.f, "g_m_y_ballaeast_01", "weapon_microsmg", nullptr, "Lamar: \"Roll through Davis. Point. Shoot. Simple, dog.\""},
    {"Desperate Times Call For...", CM_RESCUE, 1, 25000, 4, 470.f, -1790.f, 28.f, "g_m_y_mexgoon_02", "weapon_assaultrifle", nullptr, "Lamar: \"They got Tonya's cousin in Rancho. Desperate times, homie.\""},
    {"Peace Offerings", CM_PACKAGE, 1, 25000, 4, -50.f, -1450.f, 32.f, "g_m_y_mexgoon_01", "weapon_assaultrifle", nullptr, "Lamar: \"We need a peace offering for the Vagos. Steal it from them.\""},
    {"Seed Capital", CM_PACKAGE, 1, 20000, 2, 2000.f, 4850.f, 43.f, "a_m_m_hillbilly_01", "weapon_pumpshotgun", nullptr, "Lamar: \"Me and Franklin need seeds for the farm. Get to Grapeseed, homie.\""},
    {"Fire It Up", CM_DESTROY, 1, 20000, 2, -600.f, -1650.f, 20.f, "g_m_y_korean_01", "weapon_microsmg", nullptr, "Lamar: \"Those guys' grow op is on our turf. Fire it up.\""},
    {"OG Kush", CM_PACKAGE, 1, 20000, 2, -1800.f, 4400.f, 48.f, "a_m_m_hillbilly_01", "weapon_pumpshotgun", nullptr, "Lamar: \"The original strain, dog. Some old hippie's got it in the woods.\""},
};
static const ContactMission CM_MARTIN[] = {
    {"On the List", CM_ELIM, 18, 9000, 4, -1388.f, -586.f, 30.f, "s_m_m_highsec_02", "weapon_pistol", nullptr, "Martin: \"You are on the list at Bahama Mamas. So are the men I want dead.\""},
    {"Artificial Scarcity", CM_DESTROY, 19, 9500, 4, 1200.f, 2650.f, 37.f, "g_m_y_lost_01", "weapon_assaultrifle", nullptr, "Martin: \"A rival's stock on Route 68. My product should be the only product.\""},
    {"Handle with Care", CM_STEAL, 20, 9500, 4, 300.f, -1250.f, 29.f, "g_m_y_mexgoon_01", "weapon_microsmg", "boxville2", "Martin: \"A van with something fragile inside. Be very careful with it.\""},
    {"Time To Get Away", CM_RESCUE, 21, 10000, 2, 300.f, -590.f, 43.f, "s_m_y_cop_01", "weapon_pistol", nullptr, "Martin: \"My associate is at Pillbox Hill Medical, under guard. Bring him out.\""},
    {"Out of Court Settlement", CM_ELIM, 22, 10000, 2, 240.f, -400.f, 47.f, "s_m_m_security_01", "weapon_pistol", nullptr, "Martin: \"A witness is at City Hall. There will be no trial.\""},
    {"Death From Above", CM_ELIM, 24, 10500, 4, -1100.f, 4900.f, 215.f, "a_m_m_hillbilly_01", "weapon_assaultrifle", nullptr, "Martin: \"Some crazies in the Chiliad woods stole from me. Finish them.\""},
    {"Check Out Time", CM_ELIM, 35, 13500, 2, -1270.f, 315.f, 65.f, "s_m_m_highsec_01", "weapon_carbinerifle", nullptr, "Martin: \"A guest of the Richman Hotel is leaving today. Permanently.\""},
    {"Water the Vineyard", CM_DESTROY, 40, 15000, 2, -1880.f, 2060.f, 140.f, "s_m_m_security_01", "weapon_carbinerifle", nullptr, "Martin: \"The Tongva vineyard belongs to a man who said no. Ruin the harvest.\""},
    {"Grab Your Ballas", CM_ELIM, 40, 15000, 4, -130.f, -1600.f, 34.f, "g_m_y_ballaeast_01", "weapon_microsmg", nullptr, "Martin: \"Ballas in Chamberlain took what is mine. Answer them.\""},
    {"Stick Up the Stickup Crew", CM_ELIM, 40, 15000, 4, 1150.f, -650.f, 57.f, "g_m_y_mexgoon_02", "weapon_assaultrifle", nullptr, "Martin: \"A crew robbed my courier in Mirror Park. Rob them right back.\""},
    {"The Los Santos Connection", CM_PACKAGE, 40, 15000, 6, -1200.f, -1400.f, 4.f, "g_m_y_korean_01", "weapon_assaultrifle", nullptr, "Martin: \"A shipment landed in Vespucci. It was supposed to be mine.\""},
    {"Effin' Lazers", CM_STEAL, 44, 16000, 2, -2140.f, 3240.f, 32.8f, "s_m_y_marine_01", "weapon_carbinerifle", "lazer", "Martin: \"I want a jet from Zancudo. Yes, that jet.\""},
    {"Editor and Thief", CM_STEAL, 45, 16000, 2, -1050.f, -510.f, 36.f, "s_m_m_security_01", "weapon_pistol", "exemplar", "Martin: \"A film editor at Richards Majestic has my money in his car.\""},
    {"Mixed Up With Coke", CM_PACKAGE, 45, 16000, 6, 1390.f, 1140.f, 114.f, "s_m_m_highsec_01", "weapon_assaultrifle", nullptr, "Martin: \"A package at the ranch went missing. Find it before I do.\""},
    {"Dry Docking", CM_ELIM, 45, 16000, 4, 1010.f, -3100.f, 5.9f, "s_m_y_dockwork_01", "weapon_microsmg", nullptr, "Martin: \"Rival dealers at the docks. Make them fish food.\""},
    {"Cleaning the Cat House", CM_ELIM, 45, 16000, 4, 110.f, -1290.f, 29.f, "g_m_y_mexgoon_01", "weapon_pumpshotgun", nullptr, "Martin: \"Some men are stepping on my business at the Unicorn. Clean it.\""},
    {"Extradition", CM_RESCUE, 50, 17500, 6, -440.f, 6020.f, 31.f, "s_m_y_sheriff_01", "weapon_carbinerifle", nullptr, "Martin: \"The sheriffs in Paleto Bay have my man. Bring him back to me.\""},
    {"Holed Up - Burton", CM_ELIM, 50, 17500, 4, -430.f, -50.f, 46.f, "g_m_y_korean_01", "weapon_assaultrifle", nullptr, "Martin: \"A debtor is holed up in Burton with his friends. End this.\""},
    {"Show Me the Monet", CM_PACKAGE, 55, 19000, 4, -2300.f, 320.f, 174.f, "s_m_m_security_01", "weapon_carbinerifle", nullptr, "Martin: \"A painting at the Kortz Center. My wife wants it. Go.\""},
    {"Judging the Jury", CM_ELIM, 65, 21500, 8, 350.f, -1640.f, 29.f, "s_m_y_cop_01", "weapon_carbinerifle", nullptr, "Martin: \"The jury at Davis Court House is about to reach the wrong verdict.\""},
    {"Defender", CM_DESTROY, 70, 22500, 2, 2750.f, 3470.f, 55.f, "s_m_y_marine_01", "weapon_carbinerifle", nullptr, "Martin: \"Military hardware behind the YouTool. I don't want it there.\""},
    {"Rooftop Rumble", CM_PACKAGE, 75, 24000, 4, -150.f, -590.f, 211.f, "s_m_m_highsec_02", "weapon_carbinerifle", nullptr, "Martin: \"A deal on a rooftop downtown. Take the merchandise.\""},
    {"Trash Talk", CM_DESTROY, 81, 25500, 6, -560.f, 5280.f, 70.f, "a_m_m_hillbilly_02", "weapon_pumpshotgun", nullptr, "Martin: \"Rednecks at the Paleto sawmill talk too much. Shut them up.\""},
    {"Dispatch I", CM_ELIM, 1, 20000, 4, 1150.f, -650.f, 57.f, "g_m_y_mexgoon_01", "weapon_assaultrifle", nullptr, "Martin: \"A debtor in Mirror Park. Send them my regards.\""},
    {"Dispatch II", CM_PACKAGE, 1, 20000, 4, 568.f, 2735.f, 42.f, "g_m_y_lost_01", "weapon_assaultrifle", nullptr, "Martin: \"A package near Harmony is mine. Dispatch someone. You.\""},
    {"Dispatch III", CM_DESTROY, 1, 20000, 4, 200.f, -3000.f, 6.f, "s_m_y_dockwork_01", "weapon_assaultrifle", nullptr, "Martin: \"A rival's cargo on Elysian Island. Make it go away.\""},
    {"Dispatch IV", CM_RESCUE, 1, 20000, 4, -1450.f, -380.f, 38.f, "s_m_y_cop_01", "weapon_pistol", nullptr, "Martin: \"My man is under arrest in Del Perro. Unarrest him.\""},
    {"Dispatch V", CM_ELIM, 1, 20000, 4, 1961.f, 3740.f, 32.f, "g_m_y_lost_02", "weapon_assaultrifle", nullptr, "Martin: \"Bikers in Sandy Shores skipped their payment. Collect.\""},
    {"Dispatch VI", CM_STEAL, 1, 20000, 4, -147.f, 6304.f, 31.f, "s_m_y_blackops_01", "weapon_carbinerifle", "baller2", "Martin: \"A Baller in Paleto Bay belongs to me now. Bring it.\""},
};
static const ContactMission CM_RON[] = {
    {"Romance Isn't Dead", CM_RESCUE, 13, 8000, 4, 1990.f, 3050.f, 47.f, "g_m_y_lost_01", "weapon_pumpshotgun", nullptr, "Ron: \"They took a friend's sweetheart at the Yellow Jack. Love prevails!\""},
    {"Fuelling the Flames", CM_DESTROY, 20, 9500, 4, 2700.f, 1600.f, 24.f, "g_m_y_lost_02", "weapon_assaultrifle", nullptr, "Ron: \"The Lost's fuel at the Palmer-Taylor plant. It's all a conspiracy!\""},
    {"Turbine Carbine", CM_ELIM, 25, 11000, 4, 2300.f, 1950.f, 100.f, "s_m_y_blackops_01", "weapon_carbinerifle", nullptr, "Ron: \"Men with rifles at the wind farm. Why are they guarding a windmill?!\""},
    {"Daemon Run", CM_STEAL, 25, 11000, 4, 980.f, -110.f, 74.f, "g_m_y_lost_01", "weapon_sawnoffshotgun", "daemon", "Ron: \"A Daemon at the Lost's clubhouse. Trevor wants it. Don't ask.\""},
    {"Base Invaders", CM_STEAL, 27, 11500, 4, -2140.f, 3240.f, 32.8f, "s_m_y_marine_01", "weapon_carbinerifle", "crusader", "Ron: \"There's a Crusader at Zancudo. Ask no questions. They're listening.\""},
    {"Crank Up the Volume", CM_DESTROY, 30, 12500, 4, 1700.f, 3290.f, 41.f, "g_m_y_lost_01", "weapon_sawnoffshotgun", nullptr, "Ron: \"The radio mast is mind-controlling Sandy Shores. Destroy it!\""},
    {"Landing Gear", CM_STEAL, 55, 19000, 6, -1336.f, -3044.f, 13.9f, "s_m_m_security_01", "weapon_carbinerifle", "luxor", "Ron: \"A jet at the airport has what Trevor needs. Fly it out.\""},
    {"Wet Workers", CM_PACKAGE, 55, 19000, 4, 1300.f, 4310.f, 37.f, "a_m_m_hillbilly_01", "weapon_microsmg", nullptr, "Ron: \"Something's come ashore at the Alamo Sea. I saw them bury it!\""},
};
static const ContactMission CM_TREVOR[] = {
    {"TP Industries", CM_ELIM, 13, 8000, 1, 1960.f, 3820.f, 32.f, "g_m_y_lost_02", "weapon_sawnoffshotgun", nullptr, "Trevor: \"Someone's on MY land. Get out to Sandy Shores and show them the door.\""},
    {"Lost My Mind", CM_ELIM, 20, 9500, 4, 70.f, 3730.f, 39.7f, "g_m_y_lost_01", "weapon_sawnoffshotgun", nullptr, "Trevor: \"Stab City. The Lost. Everyone. Go on.\""},
    {"Crystal Clear Out", CM_DESTROY, 28, 12000, 4, 1390.f, 3600.f, 35.f, "g_m_y_mexgoon_01", "weapon_assaultrifle", nullptr, "Trevor: \"The Aztecas are cooking near MY lab. Burn theirs.\""},
    {"Chop Chop", CM_STEAL, 43, 15500, 4, 1770.f, 3239.f, 42.f, "g_m_y_lost_01", "weapon_assaultrifle", "buzzard2", "Trevor: \"A Buzzard at the airfield. Mine now. Yours for about five minutes.\""},
    {"Out of Harmony", CM_ELIM, 50, 17500, 4, 590.f, 2700.f, 42.f, "a_m_m_hillbilly_01", "weapon_pumpshotgun", nullptr, "Trevor: \"Harmony is out of tune. Fix it with bullets.\""},
    {"Satellite Communications", CM_DESTROY, 60, 20000, 4, 2050.f, 2950.f, 47.f, "s_m_y_blackops_01", "weapon_carbinerifle", nullptr, "Trevor: \"Those dishes in the desert are watching me. Blow them up.\""},
    {"Method in the Madness", CM_DESTROY, 65, 21500, 4, 1300.f, 4310.f, 37.f, "g_m_y_mexgoon_02", "weapon_assaultrifle", nullptr, "Trevor: \"Rival cooks at the Alamo Sea. There's a method. Trust me.\""},
    {"Chopper Tail", CM_STEAL, 70, 22500, 4, 1708.f, 3254.f, 41.f, "g_m_y_lost_02", "weapon_assaultrifle", "frogger", "Trevor: \"Someone left a Frogger at my airfield. Bring it. Now go!\""},
    {"Diamonds are for Trevor", CM_PACKAGE, 70, 22500, 4, -630.f, -230.f, 38.f, "s_m_m_security_01", "weapon_carbinerifle", nullptr, "Trevor: \"Diamonds in Rockford Hills. I earned them, in spirit. Fetch.\""},
};
static const ContactMission CM_LESTER[] = {
    {"Denial of Service", CM_DESTROY, 14, 8000, 4, -1080.f, -250.f, 37.f, "s_m_m_security_01", "weapon_pistol", nullptr, "Lester: \"Lifeinvader's servers. I need them offline. Very offline.\""},
    {"Master Data", CM_PACKAGE, 16, 8500, 4, -50.f, -790.f, 44.f, "s_m_m_highsec_01", "weapon_carbinerifle", nullptr, "Lester: \"There's a drive downtown I'd like to read. Bring it to me.\""},
    {"Cops Capacity", CM_RESCUE, 19, 9500, 2, 440.f, -980.f, 30.f, "s_m_y_cop_01", "weapon_pistol", nullptr, "Lester: \"My informant is at Mission Row and he's about to talk. Get him out.\""},
    {"Crime Scenester", CM_PACKAGE, 20, 9500, 4, 850.f, -2350.f, 30.f, "s_m_y_cop_01", "weapon_pistol", nullptr, "Lester: \"Evidence in a Cypress Flats yard. It would be better lost.\""},
    {"Landing Strip", CM_DESTROY, 22, 10000, 4, 2133.f, 4783.f, 40.9f, "g_m_y_lost_01", "weapon_assaultrifle", nullptr, "Lester: \"Someone's flying product through the Grapeseed strip. Close it.\""},
    {"A Titan of a Job", CM_STEAL, 24, 10500, 6, -1336.f, -3044.f, 13.9f, "s_m_y_marine_01", "weapon_carbinerifle", "titan", "Lester: \"I need a Titan from the airport. No, I'm not joking.\""},
    {"Last Chopper Outta LS", CM_STEAL, 25, 11000, 2, -745.f, -1468.f, 5.f, "s_m_m_security_01", "weapon_carbinerifle", "maverick", "Lester: \"A Maverick at the helipad is your way out. Don't miss it.\""},
    {"High Priority Case", CM_PACKAGE, 25, 11000, 4, -1040.f, -2730.f, 20.f, "s_m_m_highsec_01", "weapon_carbinerifle", nullptr, "Lester: \"A case comes through the airport today. I want it first.\""},
    {"Quarry Quarry", CM_ELIM, 30, 12500, 4, 2950.f, 2790.f, 41.f, "g_m_y_mexgoon_03", "weapon_assaultrifle", nullptr, "Lester: \"A buyer I don't like is hiding at the Davis Quartz quarry.\""},
    {"By Land, Sea and Air", CM_PACKAGE, 30, 12500, 4, -275.f, 6638.f, 7.5f, "s_m_y_blackops_01", "weapon_carbinerifle", nullptr, "Lester: \"Merryweather's cargo stops at the Paleto Bay pier. Take it.\""},
    {"Teaser Trailer", CM_STEAL, 40, 15000, 4, 850.f, -2350.f, 30.f, "s_m_y_dockwork_01", "weapon_assaultrifle", "phantom", "Lester: \"A Phantom in Cypress Flats. What it hauls matters to me.\""},
    {"Four Trailers", CM_STEAL, 45, 16000, 6, 200.f, -3000.f, 6.f, "s_m_y_dockwork_01", "weapon_assaultrifle", "hauler", "Lester: \"A Hauler on Elysian Island. Four trailers depend on it.\""},
    {"Bust Out", CM_RESCUE, 55, 19000, 4, 400.f, -1630.f, 29.f, "s_m_y_cop_01", "weapon_pistol", nullptr, "Lester: \"They've got my contact at the impound. Bust him out.\""},
    {"Sinking Feeling", CM_DESTROY, 55, 19000, 4, -300.f, -2750.f, 5.f, "s_m_y_dockwork_01", "weapon_assaultrifle", nullptr, "Lester: \"A boat's cargo at the port. I have a sinking feeling. Make it true.\""},
    {"The Parking Garage", CM_ELIM, 55, 19000, 4, -283.f, -888.f, 31.f, "s_m_m_highsec_02", "weapon_carbinerifle", nullptr, "Lester: \"A meeting in a Pillbox parking garage. Interrupt it.\""},
    {"Hack and Dash", CM_ROB, 60, 20000, 4, -1581.f, -558.f, 34.f, "s_m_m_highsec_01", "weapon_carbinerifle", nullptr, "Lester: \"Get to the Lombank terminal, stay on it, then run.\""},
    {"On Maneuvers", CM_ELIM, 65, 21500, 3, -2140.f, 3240.f, 32.8f, "s_m_y_marine_01", "weapon_carbinerifle", nullptr, "Lester: \"A squad at Zancudo found out about me. They don't get to report it.\""},
    {"Docks to Stock", CM_STEAL, 70, 22500, 4, 1180.f, -3110.f, 5.9f, "s_m_y_dockwork_01", "weapon_microsmg", "mule", "Lester: \"A truck at the docks has what we need. Don't ask.\""},
    {"Stocks and Scares", CM_ROB, 70, 22500, 4, 150.f, -1040.f, 29.4f, "s_m_m_security_01", "weapon_carbinerifle", nullptr, "Lester: \"A Fleeca in Legion Square. A scare, for the stock price. Be quick.\""},
};
static const ContactMission CM_AGENT14[] = {
    {"Severe Weather Patterns", CM_DESTROY, 1, 25000, 4, 1660.f, -30.f, 170.f, "s_m_y_blackops_01", "weapon_carbinerifle", nullptr, "Agent 14: \"Merryweather has a toy at the Land Act Dam. It needs to break.\""},
    {"Half-track Bully", CM_STEAL, 1, 25000, 4, 2950.f, 2790.f, 41.f, "s_m_y_blackops_01", "weapon_carbinerifle", "halftrack", "Agent 14: \"A Half-track at the quarry. Bring it in one piece.\""},
    {"Exit Strategy", CM_ELIM, 1, 25000, 4, 490.f, -3220.f, 6.f, "s_m_y_blackops_01", "weapon_carbinerifle", nullptr, "Agent 14: \"Merryweather's people at the Naval Port are leaving. Stop them.\""},
    {"Offshore Assets", CM_PACKAGE, 1, 25000, 4, -275.f, 6638.f, 7.5f, "s_m_y_blackops_01", "weapon_carbinerifle", nullptr, "Agent 14: \"A shipment comes ashore at the Paleto Bay pier. Intercept it.\""},
    {"Cover Blown", CM_RESCUE, 1, 25000, 4, -430.f, -50.f, 46.f, "s_m_m_security_01", "weapon_smg", nullptr, "Agent 14: \"One of ours has been made in Burton. Get them out, quietly if you can.\""},
    {"Mole Hunt", CM_ELIM, 1, 25000, 4, 100.f, -730.f, 45.f, "s_m_m_highsec_01", "weapon_carbinerifle", nullptr, "Agent 14: \"The mole meets his handlers downtown. It ends there.\""},
    {"Data Breach", CM_PACKAGE, 1, 25000, 4, 2050.f, 2950.f, 47.f, "s_m_y_blackops_01", "weapon_carbinerifle", nullptr, "Agent 14: \"Their data's on a drive in the desert array. I want it.\""},
    {"Work Dispute", CM_DESTROY, 1, 25000, 4, -2140.f, 3240.f, 32.8f, "s_m_y_marine_01", "weapon_carbinerifle", nullptr, "Agent 14: \"Zancudo has a contract dispute with us. Settle it.\""},
};
static const ContactMission CM_SECURO[] = {
    {"Escape Escort", CM_RESCUE, 1, 25000, 4, 300.f, -590.f, 43.f, "s_m_m_security_01", "weapon_smg", nullptr, "SecuroServ: \"A client needs an escort out of Pillbox Hill. Now.\""},
    {"Breakdown Recovery", CM_STEAL, 1, 25000, 4, 1200.f, 2650.f, 37.f, "g_m_y_lost_01", "weapon_assaultrifle", "flatbed", "SecuroServ: \"A client's flatbed has been taken on Route 68. Recover it.\""},
    {"Cleanup Op", CM_ELIM, 1, 25000, 4, -1050.f, -510.f, 36.f, "g_m_y_korean_01", "weapon_microsmg", nullptr, "SecuroServ: \"Someone left a mess at Richards Majestic. Clean it up.\""},
    {"Asset Seizure", CM_STEAL, 1, 25000, 4, -1600.f, -400.f, 40.f, "s_m_m_highsec_01", "weapon_carbinerifle", "schafter3", "SecuroServ: \"A debtor's armored Schafter in Del Perro. Seize it.\""},
    {"Firewall Protection", CM_DESTROY, 1, 25000, 4, -1080.f, -250.f, 37.f, "s_m_m_security_01", "weapon_carbinerifle", nullptr, "SecuroServ: \"A competitor's servers at Lifeinvader. They go offline tonight.\""},
    {"Coast Guard Duty", CM_ELIM, 1, 25000, 4, -1820.f, -1210.f, 13.f, "s_m_y_dockwork_01", "weapon_assaultrifle", nullptr, "SecuroServ: \"Smugglers on the Del Perro waterfront. Take them off the board.\""},
    {"End of Transmission", CM_DESTROY, 1, 25000, 4, 750.f, 1300.f, 360.f, "s_m_y_blackops_01", "weapon_carbinerifle", nullptr, "SecuroServ: \"A rival's relay in the Vinewood Hills. Silence it.\""},
    {"Arms Embargo", CM_PACKAGE, 1, 25000, 4, 840.f, -2200.f, 30.f, "g_m_y_mexgoon_01", "weapon_assaultrifle", nullptr, "SecuroServ: \"An arms shipment in Cypress Flats. It isn't going anywhere.\""},
};
static const ContactMission CM_AGATHA[] = {
    {"Loose Cheng", CM_RESCUE, 1, 25000, 4, -620.f, -900.f, 25.f, "g_m_y_korean_01", "weapon_microsmg", nullptr, "Ms. Baker: \"Mr. Cheng is being held in Little Seoul. The casino wants him back.\""},
    {"House Keeping", CM_ELIM, 1, 25000, 4, 925.f, 47.f, 81.f, "s_m_m_highsec_01", "weapon_smg", nullptr, "Ms. Baker: \"Some guests at the casino need to be shown out. Permanently.\""},
    {"Strong Arm Tactics", CM_STEAL, 1, 25000, 4, 1090.f, -480.f, 65.f, "g_m_y_korean_01", "weapon_microsmg", "cog55", "Ms. Baker: \"The Duggans' lawyer drives a Cognoscenti in Mirror Park. We want it.\""},
    {"Play to Win", CM_PACKAGE, 1, 25000, 4, 1150.f, -650.f, 57.f, "g_m_y_korean_01", "weapon_assaultrifle", nullptr, "Ms. Baker: \"Stolen chips in Mirror Park. They belong to the house.\""},
    {"Bad Beat", CM_DESTROY, 1, 25000, 4, -1200.f, -1400.f, 4.f, "g_m_y_korean_01", "weapon_assaultrifle", nullptr, "Ms. Baker: \"A rival's cash room in Vespucci. Shut it down.\""},
    {"Cashing Out", CM_ELIM, 1, 25000, 4, 925.f, 47.f, 81.f, "g_m_y_korean_01", "weapon_assaultrifle", nullptr, "Ms. Baker: \"The Cheng family is at our door. Defend the Diamond.\""},
};
static const ContactDef CONTACTS[] = {
    {"Gerald", 86, "Gerald: \"Ain't nobody got time for small talk.\"", CM_GERALD, (int)(sizeof CM_GERALD / sizeof *CM_GERALD)},
    {"Simeon", 18, "Simeon: \"My friend! I have work for you.\"", CM_SIMEON, (int)(sizeof CM_SIMEON / sizeof *CM_SIMEON)},
    {"Lamar", 19, "Lamar: \"Wassup, homie?\"", CM_LAMAR, (int)(sizeof CM_LAMAR / sizeof *CM_LAMAR)},
    {"Martin", 31, "Martin: \"Talk.\"", CM_MARTIN, (int)(sizeof CM_MARTIN / sizeof *CM_MARTIN)},
    {"Ron", 20, "Ron: \"They're watching us...\"", CM_RON, (int)(sizeof CM_RON / sizeof *CM_RON)},
    {"Trevor", -1, "Trevor: \"What.\"", CM_TREVOR, (int)(sizeof CM_TREVOR / sizeof *CM_TREVOR)},
    {"Lester", 12, "Lester: \"What do you need?\"", CM_LESTER, (int)(sizeof CM_LESTER / sizeof *CM_LESTER)},
    {"Agent 14", 76, "Agent 14: \"Go ahead.\"", CM_AGENT14, (int)(sizeof CM_AGENT14 / sizeof *CM_AGENT14)},
    {"SecuroServ", -1, "SecuroServ: \"SecuroServ. How can we help?\"", CM_SECURO, (int)(sizeof CM_SECURO / sizeof *CM_SECURO)},
    {"Ms. Baker", 157, "Ms. Baker: \"The Diamond Casino & Resort.\"", CM_AGATHA, (int)(sizeof CM_AGATHA / sizeof *CM_AGATHA)},
};
static const int NCONTACTS = (int)(sizeof CONTACTS / sizeof *CONTACTS);
enum { CMM_LIST = 270, CMM_CONTACT = 271 };   // CMM_CONTACT + contact index

static void StartContactMission(int c, int i, int diff = 1) {   // diff: 0 easy, 1 normal, 2 hard (the lobby's setting)
    const ContactMission &cm = CONTACTS[c].missions[i];
    Vec3 t = V3(cm.x, cm.y, cm.z);
    Vec3 back = PickSpot(DROPS);
    Mission m;
    m.name = cm.name;
    for (auto &ch : m.name) ch = (char)toupper((unsigned char)ch);
    m.timeLimitMs = 25 * 60000;
    int n = std::max(2, (5 + cm.rank / 4) * (diff == 0 ? 6 : diff == 2 ? 14 : 10) / 10);
    switch (cm.kind) {
    case CM_ELIM:
        m.steps.push_back(Goto("Go to the ~y~target area~s~.", t, 40.f));
        m.steps.back().snapFoot = true;
        m.steps.push_back(KillAll("Eliminate the ~r~targets~s~.", t, n + 2, cm.enemy, cm.weapon));
        { Step e = Escape("Lose the ~r~cops~s~."); e.wanted = 2; m.steps.push_back(e); }
        break;
    case CM_STEAL: {
        static const char *const AIR[] = {"lazer", "luxor", "titan", "buzzard2", "frogger", "maverick"};
        bool air = false;
        for (const char *a : AIR) if (!strcmp(cm.vehicle, a)) air = true;
        Step s = Steal(std::string("Steal the ~g~vehicle~s~."), t, 0.f, cm.vehicle);
        s.indoors = air;
        m.steps.push_back(Enemies(s, n, cm.enemy, cm.weapon));
        Step d = air ? Deliver("Land it at the ~y~Grapeseed airstrip~s~.", V3(2133.f, 4783.f, 40.9f)) : Deliver("Deliver it to the ~y~drop-off~s~.", back);
        if (air) { d.radius = 25.f; d.snapRoad = false; }
        else d.chasers = 1 + cm.rank / 10;
        m.steps.push_back(d);
        break;
    }
    case CM_PACKAGE: {
        Step p = Pickup("Grab the ~g~package~s~.", t, 1, "prop_box_wood02a");
        p.snapFoot = true;
        m.steps.push_back(Enemies(p, n, cm.enemy, cm.weapon));
        Step g = Goto("Deliver it to the ~y~drop-off~s~.", back, 6.f);
        g.wanted = cm.rank >= 8 ? 2 : 0;
        m.steps.push_back(g);
        break;
    }
    case CM_DESTROY: {
        m.steps.push_back(Enemies(Goto("Go to the ~y~target~s~.", t, 15.f), n, cm.enemy, cm.weapon));
        Step h = Hold("Plant the ~g~explosives~s~.", t, 12);
        h.snapFoot = true;
        h.onDone = [t] { N(N_ADD_EXPLOSION, t.x, t.y, t.z, 2, 5.f, 1, 0, 1.f, 0); };
        m.steps.push_back(h);
        Step e = Escape("Get away and lose the ~r~cops~s~."); e.wanted = 3;
        m.steps.push_back(e);
        break;
    }
    case CM_RESCUE: {
        m.steps.push_back(Enemies(Goto("Find the ~b~hostage~s~.", t, 10.f), n, cm.enemy, cm.weapon));
        m.steps.back().snapFoot = true;
        Step e = Escape("Get the hostage to safety.", t, 300.f);
        e.ally = "a_m_y_downtown_01"; e.wanted = 2;
        m.steps.push_back(e);
        m.steps.push_back(Goto("Drop the hostage at the ~y~safehouse~s~.", back, 6.f));
        break;
    }
    case CM_ROB: {
        Step g = Goto("Go to the ~y~target~s~.", t, 8.f);
        g.indoors = true;   // inside the shop or the bank
        m.steps.push_back(cm.enemy ? Enemies(g, n, cm.enemy, cm.weapon) : g);
        Step h = Hold("Stay on the ~g~job~s~.", t, 15);
        h.indoors = true; h.holdStill = true;
        m.steps.push_back(h);
        Step e = Escape("Get away and lose the ~r~cops~s~."); e.wanted = 2 + cm.rank / 30;
        m.steps.push_back(e);
        break;
    }
    }
    int pay = cm.pay * (diff == 0 ? 8 : diff == 2 ? 13 : 10) / 10, rp = 500 + pay / 20;
    std::string name = cm.name, who = CONTACTS[c].name;
    m.coopPay = pay / 2;
    m.corona = true; m.resultCash = pay; m.resultRp = rp;
    m.onSuccess = [pay, rp] { AddCash(pay); GiveRP(rp, "contact mission"); };
    Notify(cm.text);
    StartMission(m);
}
// the lobby for a contact mission, like Online's: difficulty, then the invite page
static CoronaDef ContactCorona(int c, int i) {
    const ContactMission &cm = CONTACTS[c].missions[i];
    CoronaDef d;
    d.title = cm.name;
    std::string brief = cm.text;
    size_t q = brief.find('"');
    if (q != std::string::npos) brief = brief.substr(q + 1, brief.size() - q - 2);   // the contact's line, without "Name: "
    d.desc = std::string(CONTACTS[c].name) + ": " + brief + "  Rank " + std::to_string(cm.rank) + ", " + Money(cm.pay) + ". 1-" + std::to_string(cm.players) + (cm.players == 1 ? " player." : " players.");
    d.theme = 0; d.maxPlayers = cm.players; d.payout = cm.pay;
    d.at = V3(cm.x, cm.y, cm.z);
    d.opts.push_back({"Difficulty", {"Easy", "Normal", "Hard"}, 1, "Easy: fewer enemies, less pay. Hard: more of them, more pay."});
    d.onPlay = [c, i](const std::vector<int> &sel) { StartContactMission(c, i, sel.empty() ? 1 : sel[0]); };
    return d;
}
// the contact's "Request Job": a call, then an invite to one of their jobs at your rank (accept with E)
static int g_cmInviteC = -1, g_cmInviteI = -1, g_cmInviteUntil = 0;
static void ContactRequestJob(int c) {
    int rank = LevelOf(g_rp);
    std::vector<int> ok;
    for (int i = 0; i < CONTACTS[c].count; ++i) if (rank >= CONTACTS[c].missions[i].rank) ok.push_back(i);
    if (ok.empty()) { Notify(std::string(CONTACTS[c].name) + ": \"Come back when you've got some rank.\""); return; }
    g_cmInviteC = c; g_cmInviteI = ok[Rand((int)ok.size())]; g_cmInviteUntil = Now() + 45000;
    const char *pics[] = {"CHAR_MP_GERALD", "CHAR_SIMEON", "CHAR_LAMAR", "CHAR_MARTIN", "CHAR_RON", "CHAR_TREVOR", "CHAR_LESTER", "CHAR_AGENT14", "CHAR_DEFAULT", "CHAR_CASINO_MANAGER"};
    const char *pic = c < 10 ? pics[c] : "CHAR_DEFAULT";
    N(N_REQUEST_STREAMED_TEXTURE_DICT, pic, 0);
    for (int k = 0; k < 20 && !NB(N<int>(N_HAS_STREAMED_TEXTURE_DICT_LOADED, pic)); ++k) Wait(25);
    N(N_BEGIN_TEXT_COMMAND_THEFEED_POST, "STRING");
    N(N_ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME, (std::string("Invite to ~y~") + CONTACTS[c].missions[g_cmInviteI].name + "~s~.").c_str());
    N(N_END_TEXT_COMMAND_THEFEED_POST_MESSAGETEXT, pic, pic, 1, 1, CONTACTS[c].name, "Job Invite");
    N(N_END_TEXT_COMMAND_THEFEED_POST_TICKER, 0, 1);
    Sound("Text_Arrive_Tone", "Phone_SoundSet_Default");
}
static void ContactInviteTick() {
    if (g_cmInviteC < 0) return;
    if (Now() > g_cmInviteUntil || g_mActive || CoronaActive()) { g_cmInviteC = -1; return; }
    if (!g_menuOpen) Help("Press ~INPUT_CONTEXT~ to accept the job invite: ~y~" + std::string(CONTACTS[g_cmInviteC].missions[g_cmInviteI].name));
    if (!g_menuOpen && NB(N<int>(N_IS_CONTROL_JUST_PRESSED, 0, 51))) {
        int c = g_cmInviteC, i = g_cmInviteI;
        g_cmInviteC = -1;
        OpenCorona(ContactCorona(c, i));
    }
}
// the results board for a mission started from a lobby
static void ContactResults(bool passed, const std::string &why, const Mission &m) {
    std::vector<CoronaResultRow> rows;
    CoronaResultRow me; me.name = g_myName.empty() ? "You" : g_myName; me.rank = LevelOf(g_rp); me.kills = g_mKills; me.deaths = passed ? 0 : 1;
    me.rp = passed ? m.resultRp : 0; me.cash = passed ? (g_lastHeistPay >= 0 ? g_lastHeistPay : m.resultCash) : 0; me.me = true;
    g_lastHeistPay = -1;
    rows.push_back(me);
    for (auto &n : Split(g_coop.members, ',')) if (!n.empty()) { CoronaResultRow r; r.name = n; rows.push_back(r); }
    std::string title = m.name;
    CoronaResults(title, passed, why, rows);
    bool shared = rows.size() > 1;
    g_cor.after = [title, shared] { NextJobContact(title, shared, true); };   // then Online's vote on the next job, with the crew
}
static bool ContactMenu(const Frame &f, std::string &title, std::string &sub, std::vector<Item> &items) {
    int id = (int)f.id;
    if (id == CMM_LIST) {
        title = "Contact Missions"; sub = "CONTACTS";
        for (int c = 0; c < NCONTACTS; ++c) items.push_back({CONTACTS[c].name, std::to_string(CONTACTS[c].count) + " >", CONTACTS[c].greeting, [c] { Open((MenuId)(CMM_CONTACT + c)); }});
        return true;
    }
    if (id >= CMM_CONTACT && id < CMM_CONTACT + NCONTACTS) {
        int c = id - CMM_CONTACT;
        title = CONTACTS[c].name; sub = "CONTACT MISSIONS";
        int rank = LevelOf(g_rp);
        items.push_back({"Request Job", "", std::string("Ask ") + CONTACTS[c].name + " for a job at your rank: an invite arrives.", [c] { CloseMenu(); ContactRequestJob(c); }});
        for (int i = 0; i < CONTACTS[c].count; ++i) {
            const ContactMission &cm = CONTACTS[c].missions[i];
            bool ok = rank >= cm.rank;
            items.push_back({cm.name, ok ? Money(cm.pay) : "Rank " + std::to_string(cm.rank), cm.text,
                             ok ? std::function<void()>([c, i] { CloseMenu(); OpenCorona(ContactCorona(c, i)); }) : nullptr});
        }
        return true;
    }
    return false;
}
// the phone: calling one of these contacts opens his missions (Phone.inc's contact table points here)
static MenuId ContactMenuFor(int phoneId) {
    for (int c = 0; c < NCONTACTS; ++c) if (CONTACTS[c].phoneId == phoneId) return (MenuId)(CMM_CONTACT + c);
    return M_MAIN;
}
// wiring: the phone's job contacts, the results boards (host and members)
static void ContactsInit() {
    g_contactCall = [](int c) { ContactRequestJob(c); };
    g_missionEnd = [](bool passed, const std::string &why, const Mission &m) { ContactResults(passed, why, m); };
    g_coopMemberEnd = [](bool passed, const CoopState &s) {
        std::vector<CoronaResultRow> rows;
        CoronaResultRow h; h.name = s.host; rows.push_back(h);
        for (auto &n : Split(s.members, ',')) {
            if (n.empty()) continue;
            CoronaResultRow r; r.name = n == "you" ? (g_myName.empty() ? "You" : g_myName) : n;
            if (n == "you") { r.me = true; r.rank = LevelOf(g_rp); r.cash = passed ? s.payout : 0; }
            rows.push_back(r);
        }
        CoronaResults(g_coopTitle.empty() ? "Job" : g_coopTitle, passed, passed ? "" : s.host + "'s job failed.", rows);
        std::string t = g_coopTitle;
        g_cor.after = [t] { NextJobContact(t, true, false); };   // the host's vote on the next job (a contact mission's)
    };
}

