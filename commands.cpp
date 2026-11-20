#include "terminal.h"
#include <string_view>
#include <vector>
#include <algorithm>

namespace {

// ---------------------------------------------------------------- constants
constexpr int   kMaxModels     = 20000;
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

bool ModelLoaded(int id) {
    return id >= 0 && id < kMaxModels && CModelInfo::ms_modelInfoPtrs[id]
        && CStreaming::ms_aInfoForModel[id].m_nLoadState == LOADSTATE_LOADED;
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
constexpr int kSpecialBaseId = 290;
constexpr int kSpecialSlot   = 9; // slot 9 -> model ID 299 (less likely to collide with mission slots 0..3)

void SwapPlayerModel(CPlayerPed* p, int newId) {
    const int oldId = (int)p->m_nModelIndex;
    if (oldId == newId) return;

    eWeaponType cur = WEAPONTYPE_UNARMED;
    const int slot = GetActiveWeaponSlot(*p);
    if (slot >= 0 && slot < 13) cur = p->m_aWeapons[slot].m_eWeaponType;

    p->RemoveWeaponModel(-1);
    p->SetModelIndex(newId);

    // If we switched away from our special skin slot, release it now
    if (oldId == kSpecialBaseId + kSpecialSlot && newId != oldId) {
        CStreaming::SetSpecialCharIsDeletable(kSpecialSlot);
    }

    if (newId == 0) {
        CClothes::RebuildPlayer(p, false);
    } else {
        ReapplyMoveAnims(*p);
    }

    if (cur != WEAPONTYPE_UNARMED) {
        p->SetCurrentWeapon(cur);
    }
}
// ---------------------------------------------------------------- commands
void CmdChangeMe(const Args& a) {
    int id;
    if (!ParseInt(a[1], id)) { Usage("changeme <ped_id>"); return; }
    if (id < 0 || id >= kMaxModels) { Err("error: id out of bounds"); return; }
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
    SwapPlayerModel(p, id);
    if (id != 0) CStreaming::SetModelIsDeletable(id);
    Ok("model -> " + std::to_string(id));
}
void CmdSkin(const Args& a) {
    if (!a.Has(1)) { Usage("skin <special_name>"); return; }

    // SA special actor names are max 7 chars (8-byte buffer) and lowercase in IMG
    if (a[1].size() > 7) {
        Err("error: special actor name must be <= 7 chars (e.g. tenpen, sweet, ryder)");
        return;
    }
    const std::string name = ToLower(a[1]);

    CPlayerPed* p = RequirePlayer();
    if (!p) return;

    const int modelId = kSpecialBaseId + kSpecialSlot;

    // If already using this special slot, switch to CJ temporarily so the slot can reload cleanly
    if (p->m_nModelIndex == modelId) {
        SwapPlayerModel(p, 0);
    }
    CStreaming::SetSpecialCharIsDeletable(kSpecialSlot);

    // 1. Request into kSpecialSlot (maps to modelId = 290 + kSpecialSlot)
    CStreaming::RequestSpecialChar(kSpecialSlot, name.c_str(), GAME_REQUIRED);
    CStreaming::LoadAllRequestedModels(false);

    // 2. Verify both special char flag and model load state
    if (!CStreaming::HasSpecialCharLoaded(kSpecialSlot) || !ModelLoaded(modelId)) {
        CStreaming::SetSpecialCharIsDeletable(kSpecialSlot);
        Err("error: special actor '" + name + "' not found");
        return;
    }

    // 3. Swap player to matching modelId (290 + kSpecialSlot) — do NOT mark deletable yet
    SwapPlayerModel(p, modelId);

    AddToHistory("skin applied -> " + name, kColGreen);
}
bool IsValidWeaponId(int id) { return id >= 0 && id <= kMaxWeaponId && !(id >= 19 && id <= 21); }

void CmdWeapon(const Args& a) {
    if (!a.Has(1)) { Usage("weapon <id|name> [ammo]"); return; }

    int id = -1;
    std::string nameStr;

    if (IsNumber(a[1])) {
        if (!ParseInt(a[1], id)) id = -1;
    } else {
        // Reconstruct name for weapons with spaces (e.g., "desert eagle")
        nameStr = std::string(a[1]);
        for (size_t i = 2; i < a.n; ++i) {
            if (IsNumber(a[i])) break; // Stop at ammo argument
            nameStr += " " + std::string(a[i]);
        }
        id = GetWeaponIdByName(nameStr);
    }

    if (!IsValidWeaponId(id)) { Err("error: invalid weapon id"); return; }

    int ammo = 500;
    for (size_t i = 2; i < a.n; ++i) {
        if (IsNumber(a[i])) {
            if (!ParseInt(a[i], ammo)) ammo = 500;
            break;
        }
    }
    ammo = std::clamp(ammo, 1, 99999);

    CPlayerPed* p = RequirePlayer(kAllowVehicle);
    if (!p) return;

    CWeaponInfo* wi = CWeaponInfo::GetWeaponInfo((eWeaponType)id);
    if (!wi) { Err("error: weapon info missing"); return; }

    // --- CRITICAL FIX: Load the weapon model BEFORE giving it to the player ---
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
    // -------------------------------------------------------------------------

    p->GiveWeapon((eWeaponType)id, ammo, true);
    p->SetCurrentWeapon((eWeaponType)id);
    
    // Mark models as deletable (the player's hand now holds a reference)
    for (int m : models) if (m > 0 && m < kMaxModels) CStreaming::SetModelIsDeletable(m);

    Ok("weapon -> " + std::to_string(id) + " (ammo " + std::to_string(ammo) + ")");
}

void CmdFightStyle(const Args& a) {
    int style;
    if (!ParseInt(a[1], style) || style < 4 || style > 16) {
        Usage("fightstyle <4-16>");
        Err("valid styles: 4-16");
        return;
    }
    CPlayerPed* p = RequirePlayer(kAllowVehicle);
    if (!p) return;
    
    p->m_nFightingStyle = (unsigned char)style;
    Ok("fight style -> " + std::to_string(style));
}

bool IsTrainModel(int id) { return id == 537 || id == 538 || id == 569 || id == 570 || id == 590; }

void CmdVeh(const Args& a) {
    if (!a.Has(1)) { Usage("veh <id|name> [nitro]"); return; }

    int id = -1;
    if (IsNumber(a[1])) { if (!ParseInt(a[1], id)) id = -1; }
    else id = GetVehicleIdByName(a[1]);
    if (id < kFirstVehicle || id > kLastVehicle) { Err("error: invalid vehicle id"); return; }
    if (IsTrainModel(id)) { Err("error: trains can't be spawned"); return; }

    if (!RequirePlayer(kAllowVehicle)) return;
    
    CCheat::VehicleCheat(id);
    
    bool wantNitro = false;
    for (size_t i = 2; i <= 4; ++i) {
        if (a.Has(i) && ToLower(a[i]) == "nitro") { wantNitro = true; break; }
    }
    if (wantNitro) {
        if (CVehicle* v = GetPlayerVehicle()) v->AddVehicleUpgrade(1010);
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

    CWorld::ClearExcitingStuffFromArea(CVector(x, y, z), 10.0f, false);
    p->Teleport(CVector(x, y, z), false);
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
        static bool allOpen = false;
        allOpen = !allOpen;
        for (int i = 0; i <= 5; ++i) {
            if (DoorAvailable(v, i)) {
                v->OpenDoor(p, kDoorNode[i], (eDoors)i, allOpen ? 1.0f : 0.0f, true);
            }
        }
        Ok(allOpen ? "opened all doors" : "closed all doors");
    } else {
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
    CAnimBlendAssociation* walkAnim = CAnimManager::BlendAnimation(p->m_pRwClump, grpId, 0, 4.0f);
    
    if (walkAnim) {
        walkAnim->m_bLooped = true; 
        Ok("walkstyle -> " + groupName);
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
    AddToHistory("gamespeed / time / star / fightstyle", kColGreen);
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
    { "fightstyle", CmdFightStyle }, { "clear", CmdClear }, { "help", CmdHelp }, { "exit", CmdExit },
};

} // namespace

// ---------------------------------------------------------------- tab completion
std::string GetLongestCommonPrefix(const std::vector<std::string>& strs) {
    if (strs.empty()) return "";
    std::string prefix = strs[0];
    for (size_t i = 1; i < strs.size(); ++i) {
        while (strs[i].find(prefix) != 0) {
            prefix.pop_back();
            if (prefix.empty()) return "";
        }
    }
    return prefix;
}

bool CompleteCommand(std::string& input) {
    if (input.empty()) return false;

    std::string cmdPart = input;
    std::string argPart = "";
    size_t spacePos = input.find(' ');
    if (spacePos != std::string::npos) {
        cmdPart = input.substr(0, spacePos);
        argPart = input.substr(spacePos + 1);
    }

    std::vector<std::string> matches;

    if (cmdPart == "veh" && !argPart.empty()) {
        for (int i = kFirstVehicle; i <= kLastVehicle; ++i) {
            CBaseModelInfo* mi = CModelInfo::ms_modelInfoPtrs[i];
            if (mi && mi->GetModelType() == eModelInfoType::MODEL_INFO_VEHICLE) {
                auto* vmi = reinterpret_cast<CVehicleModelInfo*>(mi);
                if (vmi->m_szGameName) {
                    std::string name = ToLower(vmi->m_szGameName);
                    if (name.starts_with(argPart)) matches.push_back(name);
                }
            }
        }
    } 
       else if ((cmdPart == "wep" || cmdPart == "weapon") && !argPart.empty()) {
        // These are the EXACT string names the game's CWeaponInfo::FindWeaponType expects
                static const char* weps[] = {
            "unarmed", "brassknuckle", "golfclub", "nightstick", "knife", "baseballbat",
            "shovel", "poolcue", "katana", "chainsaw", "dildo", "dildo2", "vibrator",
            "vibrator2", "flowers", "cane", "grenade", "teargas", "molotov", "rocket",
            "pistol", "pistol_silenced", "desert_eagle", "shotgun", "sawnoff",
            "spas12", "micro_uzi", "tec9", "mp5", "ak47", "m4", "countryrifle",
            "sniperrifle", "rpg", "flamethrower", "minigun", "satchel",
            "detonator", "spraycan", "extinguisher", "camera", "nvg", "infrared", "parachute"
        };
        for (const auto* w : weps) {
            std::string wname = ToLower(w);
            if (wname.starts_with(argPart)) matches.push_back(wname);
        }
    }
    else if (spacePos == std::string::npos) {
        for (const Cmd& c : kCmds) {
            if (c.name.starts_with(input)) matches.push_back(std::string(c.name));
        }
    }

    if (matches.empty()) return false;
    if (matches.size() == 1) {
        if (spacePos == std::string::npos) input = matches[0] + " ";
        else input = cmdPart + " " + matches[0] + " ";
        return true;
    }

    std::string prefix = GetLongestCommonPrefix(matches);
    if (spacePos == std::string::npos) {
        if (prefix.length() > input.length()) { input = prefix; return true; }
    } else {
        if (prefix.length() > argPart.length()) { input = cmdPart + " " + prefix; return true; }
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