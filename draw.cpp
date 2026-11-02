#include "terminal.h"

static ID3DXFont* g_pFont = nullptr;

void InitFont() {
    if (g_pFont) return;                                     // was leaking when called twice
    IDirect3DDevice9* device = reinterpret_cast<IDirect3DDevice9*>(RwD3D9GetCurrentD3DDevice());
    if (!device) return;
    if (FAILED(D3DXCreateFontA(device, 14, 0, FW_NORMAL, 1, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, "Arial", &g_pFont))) {
        g_pFont = nullptr;
    }
}

void DestroyFont() { if (g_pFont) { g_pFont->Release(); g_pFont = nullptr; } }

void DrawOverlay() {
    if (!gOpen) return;
    if (!g_pFont) { InitFont(); if (!g_pFont) return; }

    const float width  = (float)RsGlobal.maximumWidth;
    const float height = (float)RsGlobal.maximumHeight * 0.25f;
    CSprite2d::DrawRect(CRect(0.0f, 0.0f, width, height), CRGBA(0, 0, 0, 255));

    RECT rect;
    rect.left = 15; rect.top = 15; rect.right = (LONG)width - 15; rect.bottom = (LONG)height - 15;

    for (const auto& [color, text] : gHistory) {
        g_pFont->DrawTextA(nullptr, text.c_str(), (INT)text.size(), &rect, DT_LEFT | DT_TOP | DT_NOCLIP, color);
        rect.top += 18;
    }

    static std::string prompt;                               // reused buffer, no per-frame allocation
    prompt.assign("> ");
    prompt += gInput;
    if ((GetTickCount64() / 500) & 1) prompt += '_';         // blinking cursor
    g_pFont->DrawTextA(nullptr, prompt.c_str(), (INT)prompt.size(), &rect, DT_LEFT | DT_TOP | DT_NOCLIP, kColWhite);
}
