#pragma once

#include "plugin.h"
#include "CHud.h"
#include "CSprite2d.h"
#include "CPad.h"
#include "CStreaming.h"
#include "CDirectory.h"
#include "CPed.h"
#include "CPlayerPed.h"
#include "CVehicle.h"
#include "CWorld.h"
#include "CModelInfo.h"
#include "CVehicleModelInfo.h"
#include "CCarCtrl.h"
#include "CTimer.h"
#include "CClock.h"
#include "CAnimManager.h"
#include "CWeaponInfo.h"
#include "CCheat.h"
#include "CWanted.h"
#include "CGame.h"
#include "CWeather.h"
#include "CPlayerInfo.h"
#include "CCutsceneMgr.h"
#include "CClothes.h"
#include "CMenuManager.h"
#include "enums/eModelInfoType.h"
#include "enums/eWeaponType.h"
#include "extensions/KeyCheck.h"
// NOTE: extensions/ScriptCommands.h is intentionally NOT included any more.
// Everything here uses the game's own functions, and that header is huge.

#include <string>
#include <string_view>
#include <vector>
#include <deque>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <windows.h>
#include <d3dx9.h>

using namespace plugin;

// --- Colours ---
inline constexpr D3DCOLOR kColWhite = D3DCOLOR_RGBA(255, 255, 255, 255);
inline constexpr D3DCOLOR kColGrey  = D3DCOLOR_RGBA(150, 150, 150, 255);
inline constexpr D3DCOLOR kColRed   = D3DCOLOR_RGBA(255, 0, 0, 255);
inline constexpr D3DCOLOR kColGreen = D3DCOLOR_RGBA(0, 255, 0, 255);

// --- Limits ---
inline constexpr size_t kMaxInput        = 96;
inline constexpr size_t kMaxHistoryLines = 8;
inline constexpr size_t kMaxCmdHistory   = 64;

// --- Global State ---
extern bool gOpen;
extern std::string gInput;
extern std::deque<std::pair<D3DCOLOR, std::string>> gHistory;
extern std::vector<std::string> gCmdHistory;
extern int gHistoryIndex;

// --- utils.cpp ---
void AddToHistory(std::string line, D3DCOLOR color = kColWhite);
bool ParseInt(std::string_view s, int& out);       // no exceptions, whole string must be a number
bool ParseFloat(std::string_view s, float& out);   // rejects inf/nan
bool IsNumber(std::string_view s);
std::string ToUpper(std::string_view s);
std::string ToLower(std::string_view s);
int GetVehicleIdByName(std::string_view name);
int GetWeaponIdByName(std::string_view name);
void SetControlsDisabled(bool disabled);
CVehicle* GetPlayerVehicle();
bool DoesModelExist(const std::string& name);       // special model present in player.img?
void OpenTerminal();
void CloseTerminal(bool clearHistory);

// --- draw.cpp ---
void InitFont();
void DestroyFont();
void DrawOverlay();

// --- input.cpp ---
void ProcessInput();
void ResetInputState();

// --- commands.cpp ---
void ExecuteCommand();
bool CompleteCommand(std::string& input);
