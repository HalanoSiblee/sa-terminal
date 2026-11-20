#include "terminal.h"
#include <unordered_map>

bool gOpen = false;
std::string gInput;
std::deque<std::pair<D3DCOLOR, std::string>> gHistory;
std::vector<std::string> gCmdHistory;
int gHistoryIndex = -1;

void AddToHistory(std::string line, D3DCOLOR color) {
    gHistory.emplace_back(color, std::move(line));
    while (gHistory.size() > kMaxHistoryLines) gHistory.pop_front();
}

CVehicle* GetPlayerVehicle() { return FindPlayerVehicle(-1, false); }

void SetControlsDisabled(bool disabled) { CPad::GetPad(0)->DisablePlayerControls = disabled ? 0xFF : 0; }

void OpenTerminal() {
    gOpen = true;
    ResetInputState();
    SetControlsDisabled(true);
}

void CloseTerminal(bool clearHistory) {
    gOpen = false;
    gInput.clear();
    gHistoryIndex = -1;
    if (clearHistory) gHistory.clear();
    ResetInputState();
    SetControlsDisabled(false);
}

bool ParseInt(std::string_view s, int& out) {
    if (s.empty()) return false;
    const char* end = s.data() + s.size();
    auto r = std::from_chars(s.data(), end, out);
    return r.ec == std::errc{} && r.ptr == end;
}

bool ParseFloat(std::string_view s, float& out) {
    if (s.empty()) return false;
    const char* end = s.data() + s.size();
    auto r = std::from_chars(s.data(), end, out);
    return r.ec == std::errc{} && r.ptr == end && std::isfinite(out);
}

bool IsNumber(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    return true;
}

std::string ToUpper(std::string_view s) {
    std::string r(s);
    for (char& c : r) c = (char)std::toupper((unsigned char)c);
    return r;
}

std::string ToLower(std::string_view s) {
    std::string r(s);
    for (char& c : r) c = (char)std::tolower((unsigned char)c);
    return r;
}

// Vehicle model ids are 400..611. The old code scanned 400..20000 on every lookup.
int GetVehicleIdByName(std::string_view name) {
    if (name.empty()) return -1;

    static std::unordered_map<std::string, int> cache;   // built once, on first use in-game
    static bool built = false;
    if (!built) {
        for (int i = 400; i <= 611; ++i) {
            CBaseModelInfo* mi = CModelInfo::ms_modelInfoPtrs[i];
            if (!mi || mi->GetModelType() != eModelInfoType::MODEL_INFO_VEHICLE) continue;
            auto* vmi = reinterpret_cast<CVehicleModelInfo*>(mi);
            const size_t len = strnlen(vmi->m_szGameName, 8);
            if (len) cache.emplace(ToUpper(std::string_view(vmi->m_szGameName, len)), i);
        }
        built = !cache.empty();
    }

    auto it = cache.find(ToUpper(name));
    return it == cache.end() ? -1 : it->second;
}
int GetWeaponIdByName(std::string_view name) {
    if (name.empty()) return -1;
    std::string tmp = ToUpper(name);
    // Replace spaces with underscores (e.g., "desert eagle" -> "DESERT_EAGLE")
    std::replace(tmp.begin(), tmp.end(), ' ', '_');
    
    const int id = (int)CWeaponInfo::FindWeaponType(tmp.data());
    if (id == 0 && tmp != "UNARMED") return -1;
    return id;
}
bool DoesModelExist(const std::string& name) {
    if (name.empty() || name.size() > 8) return false;
    CDirectory* dir = CStreaming::ms_pExtraObjectsDir;
    if (!dir) return false;
    char file[32];
    std::snprintf(file, sizeof(file), "%s.dff", name.c_str());
    unsigned int offset = 0, size = 0;
    return dir->FindItem(file, offset, size);
}
