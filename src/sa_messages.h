// Verified struct layout & addresses for GTA SA 1.0 US, sourced from
// https://github.com/DK22Pac/plugin-sdk (plugin_sa/game_sa/CMessages.h)
#pragma once
#include <cstdint>

enum eMessageStyle : unsigned short
{
    STYLE_MIDDLE,
    STYLE_BOTTOM_RIGHT,
    STYLE_WHITE_MIDDLE,
    STYLE_MIDDLE_SMALLER,
    STYLE_MIDDLE_SMALLER_HIGHER,
    STYLE_WHITE_MIDDLE_SMALLER,
    STYLE_LIGHTBLUE_TOP,
    STYLE_COUNT
};

struct tMessage
{
    char*          m_pText;         // 0x0
    unsigned short m_wFlag;         // 0x4
    unsigned int   m_dwTime;        // 0x8
    unsigned int   m_dwStartTime;   // 0xC
    int            m_dwNumber[6];   // 0x10
    char*          m_pString;       // 0x28
    unsigned char  m_bPreviousBrief;// 0x2C
}; // size 0x30

struct tBigMessage
{
    tMessage m_Current;
    tMessage m_Stack[3];
}; // size 0xC0

// All big-message styles, including character-to-character dialogue that
// uses BOTTOM_RIGHT/LIGHTBLUE_TOP positioning rather than centered text.
inline constexpr eMessageStyle kDialogueStyles[] = {
    STYLE_MIDDLE, STYLE_BOTTOM_RIGHT, STYLE_WHITE_MIDDLE, STYLE_MIDDLE_SMALLER,
    STYLE_MIDDLE_SMALLER_HIGHER, STYLE_WHITE_MIDDLE_SMALLER, STYLE_LIGHTBLUE_TOP
};

inline tBigMessage* const BIGMessages = reinterpret_cast<tBigMessage*>(0xC1A970);

// CMessages::BriefMessages[8] - regular mission/dialogue subtitles (the
// "character speaking" text) go through this array via AddMessage /
// AddMessageJumpQ, which is separate from the BIGMessages array above.
inline constexpr int kBriefMessageCount = 8;
inline tMessage* const BriefMessages = reinterpret_cast<tMessage*>(0xC1A7F0);

// CCutsceneMgr::ms_running - true while a cutscene is currently playing.
// Verified address from plugin-sdk (plugin_sa/game_sa/CCutsceneMgr.cpp).
inline bool* const CutsceneMgr_ms_running = reinterpret_cast<bool*>(0xB5F851);

// CMessages::AddBigMessage(text, time, style) - cdecl, address verified
// from plugin-sdk (plugin_sa/game_sa/CMessages.cpp).
using AddBigMessage_t = void(__cdecl*)(const char*, unsigned int, unsigned short);
inline AddBigMessage_t const AddBigMessage = reinterpret_cast<AddBigMessage_t>(0x69F2B0);

// CMessages::Display(bool) - draws the native (Hangul-broken) subtitle
// text every frame. State/expiry bookkeeping happens in CMessages::Process
// (called separately from CWorld::Process), so it's safe to suppress the
// native draw entirely without breaking the message queue our overlay
// reads from.
inline constexpr uintptr_t kCMessagesDisplayAddr = 0x69EFC0;

// "Subtitles" preference byte (Options > Display > Subtitles on/off).
// UNVERIFIED - sourced from the GTAMods Wiki memory address list, not
// plugin-sdk, so treat with caution until confirmed in-game (see the
// diagnostic log in ForceSubtitlesPref in dllmain.cpp). The game only
// calls CMessages::AddMessage/AddBigMessage for a subset of dialogue
// (in-car mission banter) when this preference is on - when it's off,
// the message queue our overlay reads never gets populated at all, so
// suppressing the native draw isn't enough; the pref itself must read
// as "on" for our overlay to have anything to show.
inline bool* const PrefsShowSubtitles = reinterpret_cast<bool*>(0xBA678C);

// CMenuManager::m_bMenuActive - true while the pause/frontend menu (ESC)
// is open. Verified from plugin-sdk (plugin_sa/game_sa/CMenuManager.h/.cpp):
// FrontEndMenuManager singleton at 0xBA6748, m_bMenuActive at offset 0x5C
// (confirmed by the VALIDATE_OFFSET(CMenuManager, m_bMenuActive, 0x5C)
// assertion in the header), so the absolute address is 0xBA6748 + 0x5C.
inline bool* const MenuManager_bMenuActive = reinterpret_cast<bool*>(0xBA6748 + 0x5C);
