#include "terminal.h"

namespace {

// Key repeat is time-based now (the old version counted frames, so it depended on FPS).
constexpr ULONGLONG kRepeatDelayMs = 400;
constexpr ULONGLONG kRepeatRateMs  = 40;

int       g_heldVk     = 0;
char      g_heldCh     = 0;
ULONGLONG g_nextRepeat = 0;

struct TypeKey { int vk; char ch; };
constexpr TypeKey kExtraKeys[] = {
    { VK_SPACE, ' ' }, { VK_OEM_PERIOD, '.' }, { VK_OEM_COMMA, ',' },
    { VK_OEM_MINUS, '-' }, { VK_OEM_PLUS, '+' },
};

bool GameHasFocus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

void Type(int vk, char ch) {
    if (vk == VK_BACK) { if (!gInput.empty()) gInput.pop_back(); }
    else if (gInput.size() < kMaxInput) gInput.push_back(ch);
}

bool TryKey(int vk, char ch) {
    if (!KeyCheck::CheckJustDown(vk)) return false;
    Type(vk, ch);
    g_heldVk = vk; g_heldCh = ch;
    g_nextRepeat = GetTickCount64() + kRepeatDelayMs;
    return true;
}

} // namespace

void ResetInputState() { g_heldVk = 0; g_heldCh = 0; }

void ProcessInput() {
    if (!gOpen) return;

    if (KeyCheck::CheckJustDown(VK_UP)) {
        if (gHistoryIndex < (int)gCmdHistory.size() - 1) {
            ++gHistoryIndex;
            gInput = gCmdHistory[gCmdHistory.size() - 1 - gHistoryIndex];
        }
        return;
    }
    if (KeyCheck::CheckJustDown(VK_DOWN)) {
        if (gHistoryIndex > 0) {
            --gHistoryIndex;
            gInput = gCmdHistory[gCmdHistory.size() - 1 - gHistoryIndex];
        } else if (gHistoryIndex == 0) {
            gHistoryIndex = -1; gInput.clear();
        }
        return;
    }
    if (KeyCheck::CheckJustDown(VK_TAB)) { CompleteCommand(gInput); return; }

    // held-key repeat
    if (g_heldVk) {
        if (!(GetAsyncKeyState(g_heldVk) & 0x8000) || !GameHasFocus()) {
            g_heldVk = 0;
        } else {
            const ULONGLONG now = GetTickCount64();
            if (now >= g_nextRepeat) { Type(g_heldVk, g_heldCh); g_nextRepeat = now + kRepeatRateMs; }
        }
    }

    if (KeyCheck::CheckJustDown(VK_RETURN)) {
        const bool keepOpen = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        ExecuteCommand();
        if (!keepOpen) CloseTerminal(false);
        return;
    }
    if (KeyCheck::CheckJustDown(VK_ESCAPE)) { CloseTerminal(false); return; }

    if (TryKey(VK_BACK, 0)) return;
    for (int c = 'A'; c <= 'Z'; ++c) if (TryKey(c, (char)std::tolower(c))) return;
    for (int c = '0'; c <= '9'; ++c) if (TryKey(c, (char)c)) return;
    for (const auto& k : kExtraKeys) if (TryKey(k.vk, k.ch)) return;
}
