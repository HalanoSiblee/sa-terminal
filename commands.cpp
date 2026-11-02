#include "terminal.h"
#include <string_view>

namespace {

// ---------------------------------------------------------------- constants
constexpr int   kMaxModels     = 20000;
constexpr int   kFirstSpecial  = 290, kLastSpecial = 299;
constexpr int   kFirstVehicle  = 400, kLastVehicle = 611;
constexpr int   kMaxWeaponId   = 46;
constexpr int   kWeatherCount  = 23;
constexpr int   kMaxCarColour  = 126;
constexpr float kWorldLimit    = 3000.0f;

// ---------------------------------------------------------------- args
struct Args {
    std::string_view v[6];
    size_t n = 0;
    std::string_view operator[](size_t i) const { return i < n ? v[i] : std::string_view{}; }
    bool Has(size_t i) const { return i < n; }
};

Args Tokenize(std::string_view s) {
    Args a;
    size_t i = 0;
    while (i < s.size() && a.n < 6) {
        while (i < s.size() && s[i] == ' ') ++i;
        const size_t start = i;
        while (i < s.size() && s[i] != ' ') ++i;
        if (i > start) a.v[a.n++] = s.substr(start, i - start);
    }
    return a;
}

void Err(const std::string& m)  { AddToHistory(m, kColRed); }
void Ok(const std::string& m)   { AddToHistory(m, kColWhite); }
void Usage(const char* u)       { AddToHistory(std::string("usage: ") + u, kColRed); }

// ---------------------------------------------------------------- safety helpers
bool GameBusy() {
    return FrontEndMenuManager.m_bMenuActive || CTimer::m_UserPause || CTimer::m_CodePause
        || CCutsceneMgr::ms_running;
}

enum : unsigned { kAllowVehicle = 1, kAllowDead = 2 };

CPlayerPed* RequirePlayer(unsigned flags = 0) {
    if (GameBusy()) { Err("error: not available while paused/menu/cutscene"); return nullptr; }
    CPlayerPed* p = FindPlayerPed(0);
    if (!p) { Err("error: player not found"); return nullptr; }
    if (!(flags & kAllowDead) && p->m_fHealth <= 0.0f) { Err("error: player is dead"); return nullptr; }
    if (!(flags & kAllowVehicle) && p->m_pVehicle) { Err("error: exit the vehicle first"); return nullptr; }
    return p;
}

CVehicle* RequireVehicle() {
    CVehicle* v = GetPlayerVehicle();
    if (!v) Err("error: not in a vehicle");
    return v;
}

bool IsSpecialSlot(int id) { return id >= kFirstSpecial && id <= kLastSpecial; }

bool ModelLoaded(int id) {
    return id >= 0 && id < kMaxModels && CModelInfo::ms_modelInfoPtrs[id]
        && CStreaming::ms_aInfoForModel[id].m_nLoadState == LOADSTATE_LOADED;
}

int PickSpecialSlot(const CPlayerPed& p) {
    for (int slot = 1; slot <= 10; ++slot) {
        int id = 289 + slot;
        if (id == (int)p.m_nModelIndex) continue;
        CBaseModelInfo* mi = CModelInfo::ms_modelInfoPtrs[id];
        if (!mi || mi->m_nRefCount > 0) continue;
        const int st = CStreaming::ms_aInfoForModel[id].m_nLoadState;
        if (st != 0 && st != LOADSTATE_LOADED) continue;
        return slot;
    }
    return -1;
}

template <class T> int GetActiveWeaponSlot(const T& p) {
    if constexpr (requires { p.m_nActiveWeaponSlot; }) return (int)p.m_nActiveWeaponSlot;
    else if constexpr (requires { p.m_nCurrentWeapon; }) return (int)p.m_nCurrentWeapon;
    else return -1;
}

template <class T> void ReapplyMoveAnims(T& p) {
    if constexpr (requires { p.ReApplyMoveAnims(); }) p.ReApplyMoveAnims();
    else if constexpr (requires { p.ReapplyMoveAnims(); }) p.ReapplyMoveAnims();
    else if constexpr (requires { p.SetMoveAnim(); }) p.SetMoveAnim();
}
void SwapPlayerModel(CPlayerPed* p, int newId) {
    const int oldId = (int)p->m_nModelIndex;
    if (oldId == newId) return;

    eWeaponType cur = WEAPONTYPE_UNARMED;
    const int slot = GetActiveWeaponSlot(*p);
    if (slot >= 0 && slot < 13) cur = p->m_aWeapons[slot].m_eWeaponType;

    // 1. Remove weapon BEFORE changing model
    p->RemoveWeaponModel(-1);
    
    // 2. Change the model
    p->SetModelIndex(newId);
    
    // 3. FIX: Pass 'false' because the group already exists
    p->SetInitialState(false);
    
    // 4. Reapply move animations
    if constexpr (requires { p->ReApplyMoveAnims(); }) {
        p->ReApplyMoveAnims();
    }
    
    // 5. Rebuild CJ's clothes
    if (newId == 0) {
        CClothes::RebuildPlayer(p, false);
    }
    
    // 6. Re-equip weapon
    if (cur != WEAPONTYPE_UNARMED) {
        p->SetCurrentWeapon(cur);
    }

    if (IsSpecialSlot(oldId)) CStreaming::SetModelIsDeletable(oldId);
}

// ---------------------------------------------------------------- commands
void CmdChangeMe(const Args& a) {
    int id;
    if (!ParseInt(a[1], id)) { Usage("changeme <ped_id>"); return; }
    if (id < 0 || id >= kMaxModels) { Err("error: id out of bounds"); return; }
    if (IsSpecialSlot(id)) { Err("error: use: skin <name>"); return; }
    CBaseModelInfo* mi = CModelInfo::ms_modelInfoPtrs[id];
    if (!mi || mi->GetModelType() != eModelInfoType::MODEL_INFO_PED) { Err("error: not a ped model"); return; }

    CPlayerPed* p = RequirePlayer();
    if (!p) return;

    if (id != 0) {
        CStreaming::RequestModel(id, GAME_REQUIRED);
        CStreaming::LoadAllRequestedModels(false);
        if (!ModelLoaded(id)) {
            CStreaming::SetModelIsDeletable(id);
            Err("error: model " + std::to_string(id) + " failed to load");
            return;
        }
    }
    if (FindPlayerPed(0) != p) { Err("error: player changed while loading"); return; }

    SwapPlayerModel(p, id);
    if (id != 0) CStreaming::SetModelIsDeletable(id);
    Ok("model -> " + std::to_string(id));
}
//    -------------------------------------------    ---------------------------------

void CmdSkin(const Args& a) {
    if (!a.Has(1)) { Usage("skin <name>"); return; }
    const std::string name = ToUpper(a[1]);

    if (name.empty() || name.size() > 8 ||
        !std::all_of(name.begin(), name.end(), [](unsigned char c) {
            return std::isalnum(c) != 0;
        })) {
        Err("error: name must be 1-8 letters/digits");
        return;
    }

    CPlayerPed* p = RequirePlayer();
    if (!p) return;

    const int slot = PickSpecialSlot(*p);
    if (slot < 0) {
        Err("error: no free special slot");
        return;
    }

    // Request and block until loaded. GAME_REQUIRED prevents the engine from deleting it immediately.
    CStreaming::RequestSpecialModel(slot, name.c_str(), GAME_REQUIRED);
    CStreaming::LoadAllRequestedModels(false);

    // Verify it actually loaded
    if (!CStreaming::HasSpecialCharLoaded(slot)) {
        CStreaming::SetSpecialCharIsDeletable(slot);
        Err("error: model '" + name + "' failed to load");
        return;
    }

    // Apply it.
    const int modelId = 289 + slot;
    SwapPlayerModel(p, modelId);
    CStreaming::SetSpecialCharIsDeletable(slot);
    
    AddToHistory("skin applied -> " + name, kColGreen);
}

//    --------------------------------------------    ---------------------------
bool IsValidWeaponId(int id) { return id >= 0 && id <= kMaxWeaponId && !(id >= 19 && id <= 21); }

void CmdWeapon(const Args& a) {
    if (!a.Has(1)) { Usage("weapon <id|name> [ammo]"); return; }

    int id = -1;
    if (IsNumber(a[1])) { if (!ParseInt(a[1], id)) id = -1; }
    else id = GetWeaponIdByName(a[1]);
    if (!IsValidWeaponId(id)) { Err("error: invalid weapon id"); return; }

    int ammo = 500;
    if (a.Has(2) && !ParseInt(a[2], ammo)) { Err("error: invalid ammo"); return; }
    ammo = std::clamp(ammo, 1, 99999);

    CPlayerPed* p = RequirePlayer(kAllowVehicle);
    if (!p) return;

    CWeaponInfo* wi = CWeaponInfo::GetWeaponInfo((eWeaponType)id);
    if (!wi) { Err("error: weapon info missing"); return; }

    const int models[2] = { wi->m_nModelId, wi->m_nModelId2 };
    for (int m : models) if (m > 0 && m < kMaxModels) CStreaming::RequestModel(m, GAME_REQUIRED);
    CStreaming::LoadAllRequestedModels(false);
    for (int m : models) {
        if (m > 0 && m < kMaxModels && !ModelLoaded(m)) {
            for (int r : models) if (r > 0 && r < kMaxModels) CStreaming::SetModelIsDeletable(r);
            Err("error: weapon model failed to load");
            return;
        }
    }
    if (FindPlayerPed(0) != p) { Err("error: player changed while loading"); return; }

    p->GiveWeapon((eWeaponType)id, ammo, true);
    p->SetCurrentWeapon((eWeaponType)id);
    for (int m : models) if (m > 0 && m < kMaxModels) CStreaming::SetModelIsDeletable(m);

    Ok("weapon -> " + std::to_string(id) + " (ammo " + std::to_string(ammo) + ")");
}

bool IsTrainModel(int id) { return id == 537 || id == 538 || id == 569 || id == 570 || id == 590; }

void CmdVeh(const Args& a) {
    if (!a.Has(1)) { Usage("veh <id|name> [nitro]"); return; }

    int id = -1;
    if (IsNumber(a[1])) { if (!ParseInt(a[1], id)) id = -1; }
    else id = GetVehicleIdByName(a[1]);
    if (id < kFirstVehicle || id > kLastVehicle) { Err("error: invalid vehicle id"); return; }
    if (IsTrainModel(id)) { Err("error: trains can't be spawned"); return; }

    CBaseModelInfo* mi = CModelInfo::ms_modelInfoPtrs[id];
    if (!mi || mi->GetModelType() != eModelInfoType::MODEL_INFO_VEHICLE) { Err("error: not a vehicle model"); return; }

    if (!RequirePlayer(kAllowVehicle)) return;
    
    // Spawn the vehicle
    CCheat::VehicleCheat(id);
    
    // Check for nitro in any of the remaining arguments
    bool wantNitro = false;
    for (size_t i = 2; i <= 4; ++i) {
        if (a.Has(i) && ToLower(a[i]) == "nitro") { wantNitro = true; break; }
    }
    if (wantNitro) {
        if (CVehicle* v = GetPlayerVehicle()) v->AddVehicleUpgrade(1010); // 1010 = NITRO
    }
    
    Ok("spawned " + std::to_string(id));
}

void CmdTp(const Args& a) {
    float x, y, z;
    if (!ParseFloat(a[1], x) || !ParseFloat(a[2], y)) { Usage("tp <x> <y> [z]"); return; }
    if (std::fabs(x) > kWorldLimit || std::fabs(y) > kWorldLimit) { Err("error: coords outside the map"); return; }
    if (a.Has(3)) {
        if (!ParseFloat(a[3], z) || z < -100.0f || z > 1000.0f) { Err("error: invalid z"); return; }
    } else {
        z = CWorld::FindGroundZForCoord(x, y) + 1.0f;
    }

    CPlayerPed* p = RequirePlayer(kAllowVehicle);
    if (!p) return;

    // Clear the area to prevent physics collisions on teleport
    CWorld::ClearExcitingStuffFromArea(CVector(x, y, z), 10.0f, false);
    
    // Teleport player
    p->Teleport(CVector(x, y, z), false);
    
    // Teleport vehicle if driving
    if (CVehicle* v = GetPlayerVehicle()) v->Teleport(CVector(x, y, z), false);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "teleported -> %.1f, %.1f, %.1f", x, y, z);
    Ok(buf);
}

void CmdLock(const Args&)   { if (CVehicle* v = RequireVehicle()) { v->bIsLocked = !v->bIsLocked; Ok(std::string("vehicle locked: ") + (v->bIsLocked ? "ON" : "OFF")); } }
void CmdEngine(const Args&) { if (CVehicle* v = RequireVehicle()) { v->bEngineOn = !v->bEngineOn;  Ok(std::string("engine: ") + (v->bEngineOn ? "ON" : "OFF")); } }
void CmdLight(const Args&)  { if (CVehicle* v = RequireVehicle()) { v->bLightsOn = !v->bLightsOn;  Ok(std::string("lights: ") + (v->bLightsOn ? "ON" : "OFF")); } }

void CmdVcol(const Args& a) {
    int c1, c2;
    if (!ParseInt(a[1], c1) || !ParseInt(a[2], c2)) { Usage("vcol <p> <s>"); return; }
    if (c1 < 0 || c1 > kMaxCarColour || c2 < 0 || c2 > kMaxCarColour) { Err("error: colour id must be 0-126"); return; }
    CVehicle* v = RequireVehicle();
    if (!v) return;
    v->m_nPrimaryColor   = (unsigned char)c1;
    v->m_nSecondaryColor = (unsigned char)c2;
    Ok("colors -> " + std::to_string(c1) + ", " + std::to_string(c2));
}

void CmdGameSpeed(const Args& a) {
    float s;
    if (!ParseFloat(a[1], s)) { Usage("gamespeed <0.0 - 1.0>"); return; }
    s = std::clamp(s, 0.0f, 1.0f);
    CTimer::ms_fTimeScale = s;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "game speed -> %.2f", s);
    Ok(buf);
}

void CmdTime(const Args& a) {
    int h;
    if (!ParseInt(a[1], h)) { Usage("time <0-23>"); return; }
    h = std::clamp(h, 0, 23);
    CClock::SetGameClock((unsigned char)h, 0, CClock::ms_nGameClockDays);
    Ok("time -> " + std::to_string(h) + ":00");
}

void CmdWeather(const Args& a) {
    int id;
    if (!ParseInt(a[1], id)) { Usage("weather <0-22>"); return; }
    if (id < 0 || id >= kWeatherCount) { Err("error: weather id must be 0-22"); return; }
    CWeather::ForceWeatherNow((short)id);
    Ok("weather -> " + std::to_string(id));
}
constexpr int kDoorNode[6] = { 9, 18, 16, 12, 17, 13 };

bool DoorAvailable(CVehicle* v, int door) {
    return v->IsComponentPresent(kDoorNode[door]) && !v->IsDoorMissing((eDoors)door);
}
void CmdDoor(const Args& a) {
    int d;
    if (!ParseInt(a[1], d) || d < -1 || d > 5) { Usage("door <0-5 or -1 for all>"); return; }
    CVehicle* v = RequireVehicle();
    if (!v) return;

    CPlayerPed* p = FindPlayerPed(0);

    if (d == -1) {
        // Toggle all doors
        static bool allOpen = false;
        allOpen = !allOpen;
        for (int i = 0; i <= 5; ++i) {
            if (DoorAvailable(v, i)) {
                // Passing the player and true for playSound fixes the visual rotation
                v->OpenDoor(p, kDoorNode[i], (eDoors)i, allOpen ? 1.0f : 0.0f, true);
            }
        }
        Ok(allOpen ? "opened all doors" : "closed all doors");
    } else {
        // Toggle specific door
        if (!DoorAvailable(v, d)) { Err("error: this vehicle has no such door"); return; }
        
        bool isClosed = v->IsDoorClosed((eDoors)d);
        v->OpenDoor(p, kDoorNode[d], (eDoors)d, isClosed ? 1.0f : 0.0f, true);
        
        Ok(isClosed ? "opened door " + std::to_string(d) : "closed door " + std::to_string(d));
    }
}
void CmdHp(const Args& a) {
    int val;
    if (!ParseInt(a[1], val)) { Usage("hp <value>"); return; }
    if (CPlayerPed* p = RequirePlayer(kAllowVehicle | kAllowDead)) {
        p->m_fHealth = (float)std::max(val, 0);
        Ok("hp -> " + std::to_string(std::max(val, 0)));
    }
}

void CmdArmor(const Args& a) {
    int val;
    if (!ParseInt(a[1], val)) { Usage("armor <value>"); return; }
    if (CPlayerPed* p = RequirePlayer(kAllowVehicle | kAllowDead)) {
        p->m_fArmour = (float)std::max(val, 0);
        Ok("armor -> " + std::to_string(std::max(val, 0)));
    }
}

void CmdMoney(const Args& a) {
    std::string_view s = a[1];
    if (s.empty()) { Usage("money <amount | +amount | -amount>"); return; }
    int mode = 0;
    if (s[0] == '+') { mode = 1;  s.remove_prefix(1); }
    else if (s[0] == '-') { mode = -1; s.remove_prefix(1); }
    int amount;
    if (!ParseInt(s, amount) || amount < 0) { Err("error: invalid amount"); return; }

    CPlayerInfo& info = CWorld::Players[CWorld::PlayerInFocus];
    int64_t v = (mode == 1) ? (int64_t)info.m_nMoney + amount
              : (mode == -1) ? (int64_t)info.m_nMoney - amount
              : amount;
    v = std::clamp<int64_t>(v, 0, INT32_MAX);
    info.m_nMoney = (int)v;
    Ok("money -> " + std::to_string((int)v));
}

void CmdStar(const Args& a) {
    int level;
    if (!ParseInt(a[1], level)) { Usage("star <0-6> (or -1 for Aezakmi)"); return; }
    CWanted* wanted = FindPlayerWanted();
    if (!wanted) { Err("error: player not found"); return; }
    if (level == -1) { wanted->CheatWantedLevel(0); Ok("Aezakmi activated"); return; }
    level = std::clamp(level, 0, 6);
    wanted->SetWantedLevel(level);
    Ok("wanted level -> " + std::to_string(level));
}

bool ParseFlags5(const Args& a, bool out[5]) {
    for (int i = 0; i < 5; ++i) { int v; if (!ParseInt(a[i + 1], v)) return false; out[i] = v != 0; }
    return true;
}

void CmdPim(const Args& a) {
    bool f[5];
    if (!ParseFlags5(a, f)) { Usage("pim <b> <f> <e> <c> <m> (1 or 0)"); return; }
    if (CPlayerPed* p = RequirePlayer(kAllowVehicle)) {
        p->bBulletProof = f[0]; p->bFireProof = f[1]; p->bExplosionProof = f[2];
        p->bCollisionProof = f[3]; p->bMeleeProof = f[4];
        Ok("player proofs set");
    }
}

void CmdVim(const Args& a) {
    bool f[5];
    if (!ParseFlags5(a, f)) { Usage("vim <b> <f> <e> <c> <m> (1 or 0)"); return; }
    if (CVehicle* v = RequireVehicle()) {
        v->bBulletProof = f[0]; v->bFireProof = f[1]; v->bExplosionProof = f[2];
        v->bCollisionProof = f[3]; v->bMeleeProof = f[4];
        Ok("vehicle proofs set");
    }
}

void CmdDb(const Args& a) {
    CPlayerPed* p = RequirePlayer(kAllowVehicle);
    if (!p) return;
    CPlayerInfo& info = CWorld::Players[CWorld::PlayerInFocus];
    if (!a.Has(1)) {
        info.m_bCanDoDriveBy = !info.m_bCanDoDriveBy;
        Ok(std::string("drive-by: ") + (info.m_bCanDoDriveBy ? "ON" : "OFF"));
    } else if (a[1] == "n") {
        for (int i = 0; i < 13; ++i) {
            const eWeaponType t = p->m_aWeapons[i].m_eWeaponType;
            if (t >= 22 && t <= 32) {
                p->SetCurrentWeapon(t);
                Ok("db weapon -> " + std::to_string((int)t));
                return;
            }
        }
        Err("error: no drive-by weapon found");
    } else {
        Usage("db [n]");
    }
}

#define PED_ANIM_GROUP(p) ((p)->m_nAnimGroup)

bool SetWalkStyle(CPed* p, std::string_view name) {
    const std::string n(name);
    const int grp = (int)CAnimManager::GetFirstAssocGroup(n.c_str());
    if (grp < 0 || grp >= (int)CAnimManager::ms_numAnimAssocDefinitions) return false;
    PED_ANIM_GROUP(p) = grp;
    ReapplyMoveAnims(*p);
    return true;
}

void CmdWalkStyle(const Args& a) {
    static constexpr std::string_view kGroups[] = {
        "man", "fatman", "oldman", "oldfatman", "gang1", "gang2",
        "sexywoman", "fatwoman", "oldwoman", "busywoman", "jogwoman",
    };
    static size_t idx = 0;

    CPlayerPed* p = RequirePlayer();
    if (!p) return;

    std::string groupName;
    if (a.Has(1)) {
        groupName = std::string(a[1]);
    } else {
        idx = (idx + 1) % std::size(kGroups);
        groupName = std::string(kGroups[idx]);
    }

    int grpId = (int)CAnimManager::GetFirstAssocGroup(groupName.c_str());
    if (grpId < 0 || grpId >= (int)CAnimManager::ms_numAnimAssocDefinitions) {
        Err("error: unknown anim group '" + groupName + "'");
        return;
    }

    p->m_nAnimGroup = grpId;

    // FIX: Declare 'walkAnim' here using CAnimManager::BlendAnimation
    CAnimBlendAssociation* walkAnim = CAnimManager::BlendAnimation(
        p->m_pRwClump, 
        grpId, 
        0,       // 0 = ANIM_WALK
        4.0f     // Blend delta
    );
    
    if (walkAnim) {
        walkAnim->m_bLooped = true; 
        Ok("walkstyle -> " + groupName + " (blended)");
    } else {
        Err("error: failed to blend walk animation");
    }
}

void CmdExit(const Args&) {
    Ok("Exiting game...");
    RsGlobal.quit = true;
}

void CmdClear(const Args&) { gHistory.clear(); }

void CmdHelp(const Args&) {
    AddToHistory("--- COMMANDS ---", kColGreen);
    AddToHistory("changeme / skin / weapon / veh / tp", kColGreen);
    AddToHistory("weather / door / hp / armor / money", kColGreen);
    AddToHistory("lock / engine / light / vcol", kColGreen);
    AddToHistory("gamespeed / time / star", kColGreen);
    AddToHistory("pim / vim / db / ws", kColGreen);
    AddToHistory("clear / exit", kColGreen);
}

// ---------------------------------------------------------------- dispatch table
using Handler = void (*)(const Args&);
struct Cmd { std::string_view name; Handler fn; };

constexpr Cmd kCmds[] = {
    { "changeme", CmdChangeMe }, { "skin", CmdSkin }, { "weapon", CmdWeapon }, { "wep", CmdWeapon },
    { "veh", CmdVeh }, { "tp", CmdTp }, { "lock", CmdLock }, { "engine", CmdEngine },
    { "light", CmdLight }, { "vcol", CmdVcol }, { "gamespeed", CmdGameSpeed }, { "time", CmdTime },
    { "weather", CmdWeather }, { "door", CmdDoor }, { "hp", CmdHp }, { "armor", CmdArmor },
    { "money", CmdMoney }, { "star", CmdStar }, { "pim", CmdPim }, { "vim", CmdVim },
    { "db", CmdDb }, { "ws", CmdWalkStyle }, { "walkstyle", CmdWalkStyle },
    { "clear", CmdClear }, { "help", CmdHelp }, { "exit", CmdExit },
};

} // namespace

// ---------------------------------------------------------------- tab completion
bool CompleteCommand(std::string& input) {
    if (input.empty()) return false;

    // Auto-complete vehicles: "veh <name>"
    if (input.starts_with("veh ")) {
        std::string prefix = input.substr(4);
        for (int i = kFirstVehicle; i <= kLastVehicle; ++i) {
            CBaseModelInfo* mi = CModelInfo::ms_modelInfoPtrs[i];
            if (mi && mi->GetModelType() == eModelInfoType::MODEL_INFO_VEHICLE) {
                auto* vmi = reinterpret_cast<CVehicleModelInfo*>(mi);
                if (vmi->m_szGameName) {
                    std::string name = ToLower(vmi->m_szGameName);
                    if (name.starts_with(prefix)) {
                        input = "veh " + name;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // Auto-complete weapons: "wep <name>" or "weapon <name>"
    if (input.starts_with("wep ") || input.starts_with("weapon ")) {
        std::string prefix = input.substr(input.find(' ') + 1);
        static const char* weps[] = {
            "unarmed", "brassknuckle", "golfclub", "nightstick", "knife", "baseballbat", "shovel",
            "poolcue", "katana", "chainsaw", "dildo", "flowers", "cane", "grenade", "teargas",
            "molotov", "rocket", "pistol", "silenced", "deagle", "shotgun", "sawnoff",
            "spas12", "uzi", "tec9", "mp5", "ak47", "m4", "rifle", "sniper",
            "rpg", "flamethrower", "minigun", "satchel", "detonator", "spraycan", "extinguisher",
            "camera", "nvg", "infrared", "parachute"
        };
        for (const auto* w : weps) {
            std::string wname = ToLower(w);
            if (wname.starts_with(prefix)) {
                input = (input.starts_with("wep ") ? "wep " : "weapon ") + wname;
                return true;
            }
        }
        return false;
    }

    // Auto-complete base commands
    if (input.find(' ') != std::string::npos) return false;
    for (const Cmd& c : kCmds) {
        if (c.name.starts_with(input)) { input.assign(c.name); input += ' '; return true; }
    }
    return false;
}

void ExecuteCommand() {
    if (gInput.empty()) return;

    const std::string line = std::move(gInput);
    gInput.clear();

    AddToHistory("> " + line, kColGrey);
    if (gCmdHistory.empty() || gCmdHistory.back() != line) {
        gCmdHistory.push_back(line);
        if (gCmdHistory.size() > kMaxCmdHistory) gCmdHistory.erase(gCmdHistory.begin());
    }
    gHistoryIndex = -1;

    const Args a = Tokenize(line);
    if (a.n == 0) return;
    const std::string cmd = ToLower(a[0]);

    try {
        for (const Cmd& c : kCmds) {
            if (c.name == cmd) { c.fn(a); return; }
        }
        AddToHistory("unknown command: " + cmd, kColRed);
    } catch (...) {
        AddToHistory("error: command failed", kColRed);
    }
}