#include "terminal.h"

// Two raw code patches. Only applied on the two exes the SDK targets, and written with
// VirtualProtect (patch::Set*) instead of poking code memory directly.
static void InitPatches() {
    const auto ver = plugin::GetGameVersion();
    if (ver != GAME_10US_COMPACT && ver != GAME_10US_HOODLUM) return;
    patch::SetUChar(0x0047EDB9, 15);
    patch::SetUInt(0x0047EDD2, 15);
}

class SaTerminal {
public:
    SaTerminal() {
        Events::initRwEvent.Add(InitFont);
        Events::shutdownRwEvent.Add(DestroyFont);
        Events::d3dLostEvent.Add(DestroyFont);
        Events::d3dResetEvent.Add(InitFont);
        Events::initRwEvent.Add(InitPatches);

        Events::gameProcessEvent.Add([] {
            KeyCheck::Update();
            if (KeyCheck::CheckJustDown(VK_OEM_3)) {
                if (gOpen) CloseTerminal(true); else OpenTerminal();
            } else if (gOpen && (FrontEndMenuManager.m_bMenuActive || !FindPlayerPed(0))) {
                CloseTerminal(false);                         // never leave player controls disabled
            }
            ProcessInput();
        });

        Events::drawingEvent.Add([] { DrawOverlay(); });
    }
} SaTerminalInstance;
