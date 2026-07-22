// Korean Subtitle Overlay for GTA San Andreas
//
// The game's native font renderer (fonts.txd) has no Hangul glyphs, so
// mission/dialogue subtitles never render correctly for Korean text.
// This plugin reads the already-resolved subtitle strings directly out of
// CMessages::BIGMessages (see sa_messages.h) every frame and draws them
// itself via Dear ImGui + a system Korean TrueType font, layered on top of
// the game's own (broken) subtitle draw.
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cfloat>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "MinHook.h"
#include "sa_messages.h"

#pragma comment(lib, "d3d9.lib")

namespace {

using EndScene_t = HRESULT(__stdcall*)(IDirect3DDevice9*);
using Reset_t = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using Display_t = void(__cdecl*)(bool);

EndScene_t oEndScene = nullptr;
Reset_t oReset = nullptr;
Display_t oDisplay = nullptr;
bool g_ImGuiReady = false;
ImFont* g_KoreanFont = nullptr;

// Raw WinAPI logger (no CRT iostream). A standalone probe confirmed this
// exact pattern reliably writes from DllMain; an std::ofstream opened from
// a spawned thread produced no file at all in testing, so we avoid
// iostream here entirely and stick to plain CreateFileA/WriteFile.
void LogLine(const char* msg)
{
    HANDLE f = CreateFileA("KoreanSubtitleOverlay.log", FILE_APPEND_DATA, FILE_SHARE_READ,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    SetFilePointer(f, 0, nullptr, FILE_END);
    DWORD written;
    WriteFile(f, msg, static_cast<DWORD>(lstrlenA(msg)), &written, nullptr);
    WriteFile(f, "\r\n", 2, &written, nullptr);
    FlushFileBuffers(f);
    CloseHandle(f);
}

void LogLinef(const char* fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    wvsprintfA(buf, fmt, args);
    va_end(args);
    LogLine(buf);
}

// User-facing tuning knobs, read once at startup from an INI file sitting
// next to the .asi (same folder, found via this module's own path rather
// than the process's current directory, since modloader may launch the
// game with a different cwd). Defaults reproduce this plugin's original
// hardcoded behavior exactly, so an install with no ini file at all (or
// missing individual keys) behaves the same as before this option existed.
struct Config
{
    float fontSize = 39.0f;  // 26 * 1.5, see the historical comment this replaced
    int offsetX = 0;         // pixels added to the centered X position
    int offsetY = 0;         // pixels added to the Y position (82% of screen height)
    bool debug = false;      // gates the verbose per-frame/per-line diagnostic log
    std::string fontPath;    // empty = fall back to the system Malgun Gothic chain
};
Config g_config;
HMODULE g_hModule = nullptr;
std::string g_iniPath;

// Debug-gated counterparts to LogLine/LogLinef above: init/error/warning
// lines always go through the plain versions so a report of "it doesn't
// work" always has setup info to look at, but the noisy per-frame dumps
// (raw byte hex, slot-selection changes, native-suppress transitions) are
// only useful while actively chasing a decode/timing bug and otherwise
// just bloat the log file - those are switched to these instead, and only
// write anything once Debug=yes is set in the ini.
void LogDebugLine(const char* msg)
{
    if (!g_config.debug) return;
    LogLine(msg);
}

void LogDebugLinef(const char* fmt, ...)
{
    if (!g_config.debug) return;
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    wvsprintfA(buf, fmt, args);
    va_end(args);
    LogLine(buf);
}

// This module's own directory (with trailing slash), computed once from
// GetModuleFileNameA. Both the ini file and any relative FontPath setting
// are resolved against this, not the process's current directory - the
// same reasoning as the original BuildIniPath this replaced.
const std::string& GetModuleDir()
{
    static std::string dir;
    static bool computed = false;
    if (!computed)
    {
        char path[MAX_PATH];
        DWORD len = GetModuleFileNameA(g_hModule, path, MAX_PATH);
        if (len > 0 && len < MAX_PATH)
        {
            std::string s(path, len);
            size_t pos = s.find_last_of("\\/");
            if (pos != std::string::npos) dir = s.substr(0, pos + 1);
        }
        computed = true;
    }
    return dir;
}

std::string BuildIniPath()
{
    return GetModuleDir() + "KoreanSubtitleOverlay.ini";
}

bool IsAbsolutePath(const std::string& p)
{
    if (p.size() >= 2 && p[1] == ':') return true;                      // "C:\..."
    if (p.size() >= 2 && (p[0] == '\\' || p[0] == '/') &&
        (p[1] == '\\' || p[1] == '/')) return true;                     // UNC "\\server\share"
    return false;
}

// Relative FontPath values (e.g. "fonts/MyFont.ttf") resolve against this
// plugin's own folder, same as the ini file itself, so a bundled font in
// the mod's fonts/ subfolder Just Works regardless of the game's cwd.
std::string ResolveFontPath(const std::string& configured)
{
    if (configured.empty() || IsAbsolutePath(configured)) return configured;
    return GetModuleDir() + configured;
}

int ReadIntSetting(const char* key, int def)
{
    char defStr[16];
    wsprintfA(defStr, "%d", def);
    char buf[32];
    GetPrivateProfileStringA("Subtitle", key, defStr, buf, sizeof(buf), g_iniPath.c_str());
    return atoi(buf);
}

float ReadFloatSetting(const char* key, float def)
{
    char defStr[32];
    wsprintfA(defStr, "%d", (int)def);
    char buf[32];
    GetPrivateProfileStringA("Subtitle", key, defStr, buf, sizeof(buf), g_iniPath.c_str());
    float v = static_cast<float>(atof(buf));
    return v > 0.0f ? v : def;
}

bool ReadBoolSetting(const char* key, bool def)
{
    char buf[8];
    GetPrivateProfileStringA("Subtitle", key, def ? "yes" : "no", buf, sizeof(buf), g_iniPath.c_str());
    return (buf[0] == 'y' || buf[0] == 'Y' || buf[0] == '1');
}

std::string ReadStringSetting(const char* key, const char* def)
{
    char buf[MAX_PATH];
    GetPrivateProfileStringA("Subtitle", key, def, buf, sizeof(buf), g_iniPath.c_str());
    return std::string(buf);
}

void LoadConfig()
{
    g_iniPath = BuildIniPath();
    g_config.fontSize = ReadFloatSetting("FontSize", g_config.fontSize);
    g_config.offsetX = ReadIntSetting("OffsetX", g_config.offsetX);
    g_config.offsetY = ReadIntSetting("OffsetY", g_config.offsetY);
    g_config.debug = ReadBoolSetting("Debug", g_config.debug);
    g_config.fontPath = ResolveFontPath(ReadStringSetting("FontPath", ""));
    LogLinef("[config] path=%s FontSize=%d OffsetX=%d OffsetY=%d Debug=%d FontPath=%s",
        g_iniPath.c_str(), (int)g_config.fontSize, g_config.offsetX, g_config.offsetY, (int)g_config.debug,
        g_config.fontPath.empty() ? "(default: system Malgun Gothic)" : g_config.fontPath.c_str());
}

// GXT strings embed inline formatting tags like "~z~", "~1~", "~s~" that
// the native CFont renderer interprets specially and removes from the
// visible text before drawing. CP949 is a variable-width (double-byte)
// encoding, so leaving one of these single-byte ASCII tags in place shifts
// the byte-pairing for every Korean character that follows it, corrupting
// the rest of the string, so every tag must come out of the byte stream
// before jamo-decoding - but not all tags mean the same thing:
//
//  - Pure style/color tags (~z~, ~r~, ~SPHERE~, ...) carry no displayable
//    content - delete them outright.
//  - Numbered placeholder tags (~1~) are how the native engine splices a
//    runtime value into HUD/stat text (e.g. "거리: ~1~.~1~m 높이: ~1~.~1~m
//    회전: ~1~ 선회: ~1~" for a stunt-bike HUD) - GTA SA always writes the
//    literal digit "1" here regardless of position; it's a generic "next
//    number" marker, not an index. The actual values live in
//    tMessage::m_dwNumber[6] (sa_messages.h) and get consumed in the same
//    left-to-right order the tags appear in. Deleting these outright
//    instead of substituting was the original bug here: "~1~.~1~m"
//    silently collapsed to just ".m", with every number gone.
//
// CP949 trail bytes can legitimately equal '~' (0x7E), so only treat a
// ~...~ span as a tag when the enclosed run is short and pure ASCII -
// real GXT tags (~z~, ~1~, ~SPHERE~, etc.) always look like that, while a
// stray 0x7E landing inside real Hangul bytes will not.
bool LooksLikeGxtTag(const char* begin, const char* end)
{
    ptrdiff_t len = end - begin;
    if (len <= 0 || len > 24) return false;
    for (const char* p = begin; p < end; ++p)
        if (static_cast<unsigned char>(*p) >= 0x80) return false;
    return true;
}

bool IsNumberedGxtTag(const char* begin, const char* end)
{
    return (end - begin) == 1 && *begin >= '0' && *begin <= '9';
}

std::string ResolveGxtTags(const char* text, const int* numbers, int numberCount)
{
    std::string out;
    int nextNumber = 0;
    for (const char* p = text; *p; )
    {
        if (*p == '~')
        {
            const char* end = p + 1;
            while (*end && *end != '~') ++end;
            if (*end == '~' && LooksLikeGxtTag(p + 1, end))
            {
                if (IsNumberedGxtTag(p + 1, end) && nextNumber < numberCount)
                {
                    char buf[16];
                    wsprintfA(buf, "%d", numbers[nextNumber++]);
                    out += buf;
                }
                p = end + 1;
                continue;
            }
        }
        out += *p++;
    }
    return out;
}

// This Korean patch's fonts.txd is NOT a text encoding at all - it's a
// "jamo composition" font (like old console Korean fonts): each byte
// indexes either a Latin/ASCII glyph (matching standard ASCII layout) or,
// for the 0x80+ range, an individual Hangul consonant/vowel piece. The
// game draws 2-3 consecutive jamo bytes overlapping to fuse into one
// Hangul syllable box. Reverse-engineered by extracting and visually
// inspecting the actual fonts.txd glyph grid (see FontExtract tool) -
// confirmed against real dialogue: bytes 85 93 60 8b 9c 60 83 95 60 decode
// to "라이더" (Ryder), the NPC name that immediately precedes this line
// in-game.
constexpr wchar_t kInitialJamo[19] = {
    0x3131,0x3132,0x3134,0x3137,0x3138,0x3139,0x3141,0x3142,0x3143,
    0x3145,0x3146,0x3147,0x3148,0x3149,0x314A,0x314B,0x314C,0x314D,0x314E
}; // GAEGGEUL ... HIEUH, base byte 0x80

constexpr wchar_t kMedialJamo[10] = {
    0x314F,0x3151,0x3153,0x3155,0x3157,0x315B,0x315C,0x3160,0x3161,0x3163
}; // A YA EO YEO O YO U YU EU I, base byte 0x93

// Final (batchim) consonant bytes, confirmed by cross-referencing this
// plugin's raw-byte log against real in-game dialogue lines. The position
// isn't a simple sequential offset from 0x9D like the initials/medials
// are, so only emit a final for bytes actually verified here - anything
// else falls back to "no final" (readable, if imperfect) rather than
// guessing and rendering a different, wrong syllable.
//
// 0xA8 was previously mapped to ㅇ ("visually a circle glyph, unverified")
// - that was wrong. Log analysis across three independent words ("이놈아",
// "임마", "좀") all require a ㅁ final at this byte, so it's been corrected.
// The real ㅇ final turned out to be the separate byte 0xAD (see below).
//
// The full table below was cross-checked against every dialogue string in
// american.gxt (not just played missions) via a standalone offline tool
// that runs this exact decode logic over the whole TDAT block of every
// GXT table - after adding these entries, zero unmapped high bytes remain
// across all 1775 Korean-encoded strings in the game.
wchar_t finalJamoForByte(unsigned char b)
{
    // 0x3E (ㅍ, confirmed via "무릎" = 86 99 5D 85 9B 3E 5D) reliably came back
    // as unmapped from inside the switch below despite a matching case being
    // present - reproduced via in-game logging, never root-caused (suspected
    // MSVC codegen quirk for this case set), fixed by handling it here
    // instead, before the switch ever runs.
    if (b == 0x3E) return 0x314D;
    switch (b)
    {
        case 0x3C: return 0x314C; // ㅌ - confirmed via "같이" in two independent lines (80 93 3C 60 8B 9C 60);
                                   // reuses the ASCII '<' slot, but only ever read here, inside an
                                   // already-detected initial+medial pair, so plain '<' text is unaffected
        case 0x9D: return 0x3131; // ㄱ - confirmed via "가속하려면", "죽기엔" (most common final, by far the biggest source of stray '<'/']' before this was added)
        case 0x9E: return 0x3132; // ㄲ - confirmed via "밖에" (x3), "낚이던데"
        case 0x9F: return 0x3134; // ㄴ - confirmed via "만"
        case 0xA1: return 0x3136; // ㄶ - confirmed via "않아", "않습니다"; was 0x3135 (which is
                                   // actually ㄵ, not ㄶ - a one-character typo in the compat jamo
                                   // constant) causing every ㄶ-final syllable (잖아, 귀찮아, ...)
                                   // to silently render as the wrong-but-similar ㄵ-final syllable
        case 0xA2: return 0x3137; // ㄷ - confirmed via "받고", "싣는", "얻어", "믿는"
        case 0xA3: return 0x3139; // ㄹ - confirmed via "를", "살"
        case 0xA4: return 0x313A; // ㄺ - confirmed via "닭대가리", "닭살"
        case 0xA5: return 0x313E; // ㄻ - confirmed via "옮기나"
        case 0xA6: return 0x313B; // ㄼ - confirmed via "밟기"
        case 0xA7: return 0x3140; // ㅀ - confirmed via "잃기", "잃었습니다", "싫으니"
        case 0xA8: return 0x3141; // ㅁ - confirmed via "이놈아", "임마", "좀" (corrected from ㅇ)
        case 0xA9: return 0x3142; // ㅂ - confirmed via "십", "습"
        case 0xAA: return 0x3144; // ㅄ - confirmed via "없다", "값"
        case 0xAB: return 0x3145; // ㅅ - confirmed via "그것도", "것을", "거짓말쟁이", "멋쟁이"
        case 0xAC: return 0x3146; // ㅆ - confirmed via "있"
        case 0xAD: return 0x3147; // ㅇ - confirmed via "상황" (both syllables share this final)
        case 0xAE: return 0x3148; // ㅈ - confirmed both from "[조][가같이도]" slang reading and by visually
                                   // comparing FontExtract's cell_ae.png against the confirmed ㅅ glyph
                                   // (cell_ab.png): same checkmark shape plus one extra top stroke, exactly
                                   // the real ㅅ->ㅈ relationship
        case 0xAF: return 0x314A; // ㅊ - confirmed via "몇" (9+ occurrences), "쫓는"
        case 0x5B: return 0x314E; // ㅎ - confirmed via "그렇게" (10+ occurrences: 80 9B 5D 85 95 5B 60 80 95 5C 60);
                                   // reuses the ASCII '[' slot, same pattern as 0x3C reusing '<' for ㅌ
        // 0x3E (ㅍ) is handled above, before this switch - see the comment there.
        default: return 0;
    }
}

// Standard Unicode Hangul compatibility jamo -> modern jamo index, needed
// for the U+AC00 composition formula (initial*21 + medial)*28 + final.
int initialToUnicodeIndex(wchar_t jamo)
{
    static const wchar_t order[19] = {
        0x3131,0x3132,0x3134,0x3137,0x3138,0x3139,0x3141,0x3142,0x3143,
        0x3145,0x3146,0x3147,0x3148,0x3149,0x314A,0x314B,0x314C,0x314D,0x314E
    };
    for (int i = 0; i < 19; ++i) if (order[i] == jamo) return i;
    return -1;
}
int medialToUnicodeIndex(wchar_t jamo)
{
    static const wchar_t order[21] = {
        0x314F,0x3150,0x3151,0x3152,0x3153,0x3154,0x3155,0x3156,0x3157,
        0x3158,0x3159,0x315A,0x315B,0x315C,0x315D,0x315E,0x315F,0x3160,
        0x3161,0x3162,0x3163
    };
    for (int i = 0; i < 21; ++i) if (order[i] == jamo) return i;
    return -1;
}
// Compound vowels (ㅘㅝㅐㅔㅚㅟㅢ etc.) are drawn as two consecutive
// medial-range bytes overlapping, same trick as initial+medial+final for
// a whole syllable. 0x5C acts as a dedicated "add ㅣ" second component
// (confirmed via "에" = EO(0x95) + 0x5C); other pairs use two ordinary
// vowel bytes (confirmed via "와" = O(0x97) + A(0x93)). Returns the
// combined Unicode medial index, or -1 if this isn't a known pair.
int combineMedial(unsigned char firstByte, unsigned char secondByte)
{
    struct Pair { unsigned char a, b; int result; };
    static const Pair table[] = {
        {0x93, 0x5C, 1},  // A + i -> AE
        {0x95, 0x5C, 5},  // EO + i -> E
        {0x9B, 0x5C, 19}, // EU + i -> YI
        {0x97, 0x5C, 11}, // O + i -> OE
        {0x99, 0x5C, 16}, // U + i -> WI
        {0x96, 0x5C, 7},  // YEO + i -> YE, confirmed via "미셸" (Michelle)
        {0x94, 0x5C, 3},  // YA + i -> YAE, confirmed via "얘기" (80 9B 5D 86 93 9F 60 20 8B 94 5C 60 80 9C 60)
        {0x97, 0x93, 9},  // O + A -> WA
        {0x99, 0x95, 14}, // U + EO -> WEO
        // 0x9C (the plain medial "I" byte) doubles as an alternate "+i"
        // trigger, same role as 0x5C above - confirmed via "두려움의"
        // (EU + 0x9C -> YI) and "뒤에서" (U + 0x9C -> WI). By far the
        // single biggest source of stray '`' before this was found, since
        // -의 is one of the most common particles in Korean.
        {0x93, 0x9C, 1},
        {0x95, 0x9C, 5},
        {0x9B, 0x9C, 19},
        {0x97, 0x9C, 11},
        {0x99, 0x9C, 16},
        {0x94, 0x9C, 3},
    };
    for (const Pair& p : table)
        if (p.a == firstByte && p.b == secondByte) return p.result;
    return -1;
}

// A few compound vowels stack a THIRD overlapping byte on top of an
// already-combined pair - confirmed via "됐" (dwae-ss, as in "됐어"):
// O + A combines to WA via combineMedial() above, then a further 0x5c
// "add i" byte upgrades WA -> WAE, the same way 0x5c independently
// upgrades plain O -> OE elsewhere in the table. WEO -> WE follows the
// same stacking pattern. Returns the upgraded Unicode medial index, or
// -1 if this medial has no such upgrade.
int upgradeMedialWithI(int mi)
{
    switch (mi)
    {
        case 9:  return 10; // WA  -> WAE, confirmed via "됐"
        case 14: return 15; // WEO -> WE, same pattern as WA -> WAE, unverified against real text
        default: return -1;
    }
}

int finalToUnicodeIndex(wchar_t jamo)
{
    if (jamo == 0) return 0; // no final
    static const wchar_t order[27] = {
        0x3131,0x3132,0x3133,0x3134,0x3135,0x3136,0x3137,0x3139,0x313A,
        0x313B,0x313C,0x313D,0x313E,0x313F,0x3140,0x3141,0x3142,0x3144,
        0x3145,0x3146,0x3147,0x3148,0x314A,0x314B,0x314C,0x314D,0x314E
    };
    for (int i = 0; i < 27; ++i) if (order[i] == jamo) return i + 1;
    return 0;
}

// Decodes this font's custom jamo-composition bytes into a real UTF-8
// Korean string. Falls back to passing ASCII bytes (< 0x80) through
// verbatim, since that range matches standard ASCII in this font.
std::wstring DecodeJamoBytes(const unsigned char* text)
{
    std::wstring out;
    size_t i = 0;
    // crude length via null terminator, since input is a C string
    size_t len = 0;
    while (text[len]) ++len;

    while (i < len)
    {
        unsigned char b0 = text[i];

        // This font redraws a handful of low-ASCII cells as different
        // glyphs than standard ASCII (same idea as 0x3C->ㅌ and 0x5B->ㅎ
        // below, which are jamo *finals* instead of standalone bytes):
        // 0x7C's cell is a degree sign, not '|' - confirmed both via
        // FontExtract (cell_7c.png shows "°") and a real in-game stunt
        // HUD string ("회전: ~1~°") that rendered as a bare "|" before
        // this was added.
        if (b0 == 0x7C)
        {
            out += static_cast<wchar_t>(0x00B0); // °
            ++i;
            continue;
        }
        if (b0 < 0x80)
        {
            out += static_cast<wchar_t>(b0);
            ++i;
            continue;
        }
        if (b0 >= 0x80 && b0 <= 0x92 && i + 1 < len)
        {
            unsigned char b1 = text[i + 1];
            if (b1 >= 0x93 && b1 <= 0x9C)
            {
                wchar_t initial = kInitialJamo[b0 - 0x80];
                int mi = medialToUnicodeIndex(kMedialJamo[b1 - 0x93]);
                size_t consumed = 2;

                // Compound vowels (와/에/외/...) are two consecutive
                // medial-range bytes (or a medial + the 0x5C "add i"
                // byte) overlapping into one glyph - see combineMedial.
                if (i + 2 < len)
                {
                    int combined = combineMedial(b1, text[i + 2]);
                    if (combined >= 0)
                    {
                        mi = combined;
                        ++consumed;

                        // Check for a stacked third byte upgrading this
                        // pair further (WA -> WAE, WEO -> WE) - see
                        // upgradeMedialWithI.
                        if (i + consumed < len && text[i + consumed] == 0x5c)
                        {
                            int upgraded = upgradeMedialWithI(mi);
                            if (upgraded >= 0)
                            {
                                mi = upgraded;
                                ++consumed;
                            }
                        }
                    }
                }

                wchar_t finalJamo = 0;
                if (i + consumed < len)
                {
                    unsigned char bf = text[i + consumed];
                    if (bf == 0x60 || bf == 0x5D)
                    {
                        ++consumed; // explicit "no final" terminator
                    }
                    else
                    {
                        wchar_t f = finalJamoForByte(bf);
                        if (f != 0)
                        {
                            finalJamo = f;
                            ++consumed;
                            if (i + consumed < len &&
                                (text[i + consumed] == 0x60 || text[i + consumed] == 0x5D))
                                ++consumed; // trailing terminator after a final
                        }
                    }
                }

                int ii = initialToUnicodeIndex(initial);
                int fi = finalToUnicodeIndex(finalJamo);
                if (ii >= 0 && mi >= 0 && mi < 21)
                {
                    out += static_cast<wchar_t>(0xAC00 + (ii * 21 + mi) * 28 + fi);
                    i += consumed;
                    continue;
                }
            }
        }
        // Unrecognized byte in the high range: skip it rather than
        // emitting garbage.
        ++i;
    }
    return out;
}

void LogRawBytesOnce(const char* text, const int* numbers, int numberCount)
{
    if (!g_config.debug) return;
    static std::string lastLogged;
    if (!text || text == lastLogged) return;
    lastLogged = text;

    std::string hex;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p)
    {
        char b[4];
        wsprintfA(b, "%02x ", (int)*p);
        hex += b;
    }

    // Logged alongside the raw text so a ~1~-bearing message (HUD/stat
    // text with runtime-substituted numbers, see ResolveGxtTags above) can
    // be checked against what actually got spliced in, instead of having
    // to guess after the fact.
    std::string nums;
    for (int i = 0; i < numberCount; ++i)
    {
        char b[16];
        wsprintfA(b, "%d ", numbers[i]);
        nums += b;
    }
    LogLinef("[subtitle] raw bytes: %s text=\"%s\" numbers=[%s]", hex.c_str(), text, nums.c_str());
}

void LoadKoreanFont()
{
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;

    // FontPath in the ini (see LoadConfig/ResolveFontPath) lets a user drop
    // their own .ttf/.otf in the mod's fonts/ folder instead of relying on
    // the system's Malgun Gothic - tried first, and only falls through to
    // the original system-font chain below if it's unset or fails to load.
    if (!g_config.fontPath.empty())
    {
        g_KoreanFont = io.Fonts->AddFontFromFileTTF(
            g_config.fontPath.c_str(), g_config.fontSize, &cfg, io.Fonts->GetGlyphRangesKorean());
        if (g_KoreanFont)
        {
            LogLinef("[init] loaded custom FontPath: %s", g_config.fontPath.c_str());
            return;
        }
        LogLinef("[warn] FontPath \"%s\" failed to load, falling back to system Malgun Gothic",
            g_config.fontPath.c_str());
    }

    g_KoreanFont = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\malgunbd.ttf", g_config.fontSize, &cfg, io.Fonts->GetGlyphRangesKorean());
    if (!g_KoreanFont)
    {
        LogLine("[warn] malgunbd.ttf (bold) not found, falling back to regular malgun.ttf");
        g_KoreanFont = io.Fonts->AddFontFromFileTTF(
            "C:\\Windows\\Fonts\\malgun.ttf", g_config.fontSize, &cfg, io.Fonts->GetGlyphRangesKorean());
    }
    if (!g_KoreanFont)
    {
        LogLine("[warn] malgun.ttf not found either, falling back to default ImGui font (no Hangul)");
        g_KoreanFont = io.Fonts->AddFontDefault();
    }
}

// The game only calls CMessages::AddMessage/AddBigMessage for some mission
// dialogue (e.g. in-car banter) when the native "Subtitles" option is on -
// with it off, the message queue our overlay reads is simply never
// populated, so there's nothing for us to intercept no matter how we draw.
// Force the preference byte on every frame so the game always queues this
// dialogue; our own hkDisplay hook already no-ops the native draw
// unconditionally, so this can't cause the broken native subtitle box to
// reappear - only our overlay ever renders.
void ForceSubtitlesPref()
{
    static bool loggedOnce = false;
    if (!loggedOnce)
    {
        LogDebugLinef("[subtitles-pref] current value at 0xBA678C: %d", (int)*PrefsShowSubtitles);
        loggedOnce = true;
    }
    *PrefsShowSubtitles = true;
}

std::string DecodeToUtf8(const char* text, const int* numbers, int numberCount)
{
    LogRawBytesOnce(text, numbers, numberCount);
    std::string resolved = ResolveGxtTags(text, numbers, numberCount);
    std::wstring wide = DecodeJamoBytes(reinterpret_cast<const unsigned char*>(resolved.c_str()));
    if (wide.empty()) return {};
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (ulen <= 0) return {};
    std::string utf8(ulen, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, utf8.data(), ulen, nullptr, nullptr);
    if (!utf8.empty() && utf8.back() == '\0') utf8.pop_back();
    return utf8;
}

// Only one subtitle line is ever shown at a time. This used to track every
// BIGMessages/BriefMessages slot independently and append to a history log
// as each slot's text changed, but the game can keep an OLD style slot
// "active" (non-expired, m_pText still set) for a frame or two after a NEW
// line has already started in a different slot - since slot iteration order
// has nothing to do with recency, that let the old line flash back on
// screen right after being replaced. Instead, every frame we scan all
// active slots and pick the one with the latest m_dwStartTime - that is
// always the most recently queued line, regardless of what other slots
// still happen to be holding stale-but-technically-active text.
std::string g_currentLine;

// Banners like "MISSION PASSED!", BUSTED, and WASTED go through the same
// CMessages queue as dialogue but are plain ASCII (never translated), so
// the game's own native font renders them correctly - only Korean text
// needs our overlay. Skip capturing anything with no high byte at all so
// hkDisplay's native suppression (see below) can stay off for that frame
// and let the original banner draw normally instead of being redrawn in
// our own font.
bool ContainsHighByte(const char* text)
{
    for (const char* p = text; *p; ++p)
        if (static_cast<unsigned char>(*p) >= 0x80) return true;
    return false;
}

const tMessage* PickCurrentMessage()
{
    const tMessage* best = nullptr;
    for (eMessageStyle style : kDialogueStyles)
    {
        const tMessage& m = BIGMessages[style].m_Current;
        if (m.m_pText && m.m_pText[0] && ContainsHighByte(m.m_pText) &&
            (!best || m.m_dwStartTime >= best->m_dwStartTime))
            best = &m;
    }
    for (int i = 0; i < kBriefMessageCount; ++i)
    {
        const tMessage& m = BriefMessages[i];
        if (m.m_pText && m.m_pText[0] && ContainsHighByte(m.m_pText) &&
            (!best || m.m_dwStartTime >= best->m_dwStartTime))
            best = &m;
    }
    return best;
}

// Diagnostic dump for the A->B flicker: prints every currently-active
// slot's raw pointer, start time, and text whenever the *chosen* slot (by
// identity, not content) or that slot's start time changes. A one-frame
// flicker is too fast to screenshot, but it always shows up here as an
// extra selection change between two log lines a frame or two apart -
// this lets us see whether the old slot's m_dwStartTime is genuinely
// still ahead of the new one for a frame (buffer/timing race) rather than
// guessing.
void LogSubtitleSelectionIfChanged(const tMessage* msg)
{
    if (!g_config.debug) return;
    static const tMessage* lastMsg = nullptr;
    static unsigned int lastStart = 0xFFFFFFFF;
    unsigned int curStart = msg ? msg->m_dwStartTime : 0;
    if (msg == lastMsg && curStart == lastStart) return;
    lastMsg = msg;
    lastStart = curStart;

    LogLine("[subtitle-debug] selection changed, active slots:");
    for (eMessageStyle style : kDialogueStyles)
    {
        const tMessage& m = BIGMessages[style].m_Current;
        if (m.m_pText && m.m_pText[0])
            LogLinef("  [big style=%d]%s ptr=0x%x start=%u text=%s",
                (int)style, (&m == msg) ? " <-- chosen" : "",
                (unsigned int)(uintptr_t)m.m_pText, m.m_dwStartTime, m.m_pText);
    }
    for (int i = 0; i < kBriefMessageCount; ++i)
    {
        const tMessage& m = BriefMessages[i];
        if (m.m_pText && m.m_pText[0])
            LogLinef("  [brief idx=%d]%s ptr=0x%x start=%u text=%s",
                i, (&m == msg) ? " <-- chosen" : "",
                (unsigned int)(uintptr_t)m.m_pText, m.m_dwStartTime, m.m_pText);
    }
}

// Dear ImGui has no built-in text outline, so fake one by drawing the same
// string several times at small pixel offsets in outline color underneath,
// then the real text on top - standard "poor man's outline" technique.
void AddOutlinedText(ImDrawList* drawList, ImFont* font, float fontSize, ImVec2 pos,
    ImU32 textColor, ImU32 outlineColor, const char* text)
{
    static const ImVec2 kOffsets[] = {
        {-1.5f,-1.5f}, {0.0f,-1.5f}, {1.5f,-1.5f},
        {-1.5f, 0.0f},               {1.5f, 0.0f},
        {-1.5f, 1.5f}, {0.0f, 1.5f}, {1.5f, 1.5f},
    };
    for (const ImVec2& o : kOffsets)
        drawList->AddText(font, fontSize, ImVec2(pos.x + o.x, pos.y + o.y), outlineColor, text);
    drawList->AddText(font, fontSize, pos, textColor, text);
}

void DrawDialogueSubtitles()
{
    // ESC/pause menu is up - hide the overlay entirely while it's open, and
    // resume showing whatever is current once the player goes back in-game
    // (we don't touch g_currentLine here, so PickCurrentMessage() just
    // re-evaluates live game state on the next unpaused frame).
    if (*MenuManager_bMenuActive) return;

    const tMessage* msg = PickCurrentMessage();
    LogSubtitleSelectionIfChanged(msg);
    if (!msg)
    {
        g_currentLine.clear();
        return;
    }

    std::string utf8 = DecodeToUtf8(msg->m_pText, msg->m_dwNumber, 6);
    if (utf8.empty())
    {
        g_currentLine.clear();
        return;
    }
    if (utf8 != g_currentLine)
    {
        LogDebugLinef("[subtitle] now showing: %s", utf8.c_str());
        g_currentLine = utf8;
    }

    // ImGuiWindowFlags_AlwaysAutoResize grows the window to fit content, but
    // that resize is only reflected a frame after the content that caused it
    // was submitted - so the very first frame a longer line replaces a
    // shorter one, it was still being clipped to the previous (narrower)
    // window rect, cutting the new line short for one frame before it
    // "caught up" on the next. Sidestepping the lag entirely by measuring
    // the text and setting the window size explicitly every frame, instead
    // of letting ImGui auto-size it a frame late.
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 textSize = g_KoreanFont->CalcTextSizeA(g_config.fontSize, FLT_MAX, 0.0f, g_currentLine.c_str());
    constexpr float kOutlinePad = 4.0f; // room for AddOutlinedText's outline offset
    ImVec2 windowSize(textSize.x + kOutlinePad * 2.0f, textSize.y + kOutlinePad * 2.0f);

    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f + g_config.offsetX, io.DisplaySize.y * 0.82f + g_config.offsetY),
        ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kOutlinePad, kOutlinePad));
    ImGui::PushFont(g_KoreanFont);
    ImGui::Begin("##korean_sub_log", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    AddOutlinedText(drawList, g_KoreanFont, g_config.fontSize, pos,
        IM_COL32(255, 255, 255, 255), IM_COL32(0, 0, 0, 255), g_currentLine.c_str());
    ImGui::End();
    ImGui::PopFont();
    ImGui::PopStyleVar();
}

void EnsureImGuiInit(IDirect3DDevice9* device)
{
    if (g_ImGuiReady) return;

    LogLine("[init] first EndScene call, creating ImGui context");
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    LoadKoreanFont();

    ImGui_ImplDX9_Init(device);
    g_ImGuiReady = true;
    LogLine("[init] ImGui + DX9 backend ready");
}

void UpdateDisplaySize(IDirect3DDevice9* device)
{
    IDirect3DSurface9* backBuffer = nullptr;
    if (SUCCEEDED(device->GetRenderTarget(0, &backBuffer)) && backBuffer)
    {
        D3DSURFACE_DESC desc;
        backBuffer->GetDesc(&desc);
        ImGui::GetIO().DisplaySize = ImVec2((float)desc.Width, (float)desc.Height);
        backBuffer->Release();
    }
}

HRESULT __stdcall hkEndScene(IDirect3DDevice9* device)
{
    EnsureImGuiInit(device);
    UpdateDisplaySize(device);

    ForceSubtitlesPref();

    ImGui_ImplDX9_NewFrame();
    ImGui::NewFrame();
    DrawDialogueSubtitles();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

    return oEndScene(device);
}

// The [display-debug] log proved the earlier fixed ~166ms (10-frame)
// cooldown was far too short: native gets ALLOWED well before the next
// Korean line's m_pText appears, meaning the native renderer keeps
// showing the OLD line for its own declared duration, completely
// independent of whether m_pText currently reads non-null - clearing
// m_pText early (to let the next line's raw bytes be written) does not
// mean the native display system is done showing it. That's the actual
// source of the reported overlap: for a real stretch of time, native is
// still legitimately drawing the old (broken) line by its own clock while
// our overlay has already moved on.
//
// Fix: base the suppression window on that message's OWN declared
// display duration (tMessage::m_dwTime, milliseconds) measured against
// real wall-clock time (GetTickCount), instead of a guessed frame count -
// so we keep blocking native for exactly as long as native itself intends
// to keep the line up, then release it once that time has genuinely
// passed.
DWORD g_nativeSuppressUntilTick = 0;
constexpr DWORD kFallbackSuppressMs = 3000; // used if m_dwTime looks bogus (0)

// The [display-debug] log caught native being ALLOWED exactly 6ms after
// `until` on one real transition ("아 제발, 좀 봐줘." -> "가서 우리 구역을
// 좀 붐비게 해야지.") - m_dwTime cutting it that close means any scripted
// (deterministic) dialogue sequence can reliably hit the same knife-edge
// timing every time it's replayed, which is exactly why it looked
// consistent/repeatable to the user despite only affecting specific
// transitions. Pad the window well past the message's own declared
// duration so real clock jitter can't win that race.
constexpr DWORD kSuppressSafetyMarginMs = 500;

// Suppresses the native subtitle draw while a Korean message is active,
// and for the remainder of that message's own declared display duration
// afterward - plain-ASCII banners like MISSION PASSED/BUSTED/WASTED are
// otherwise left to the native call so they keep their normal game
// styling instead of being redrawn in ours. CMessages::Process (unhooked,
// still runs normally) keeps advancing/expiring messages regardless of
// which path draws them.
void __cdecl hkDisplay(bool arg)
{
    const tMessage* msg = PickCurrentMessage();
    DWORD now = GetTickCount();

    if (msg)
    {
        DWORD duration = (msg->m_dwTime > 0) ? msg->m_dwTime : kFallbackSuppressMs;
        g_nativeSuppressUntilTick = now + duration + kSuppressSafetyMarginMs;
    }

    bool suppress = (msg != nullptr) || (now < g_nativeSuppressUntilTick);

    // Diagnostic for the reported overlap between the old (native, broken)
    // line and the new (our overlay) line: only logs when the suppress/
    // allow decision actually flips, so we can see directly when the
    // native draw call is let through relative to a subtitle transition.
    static bool lastSuppress = true;
    if (suppress != lastSuppress)
    {
        LogDebugLinef("[display-debug] native draw %s (korean=%d until=%u now=%u)",
            suppress ? "SUPPRESSED" : "ALLOWED", (int)(msg != nullptr), g_nativeSuppressUntilTick, now);
        lastSuppress = suppress;
    }

    if (!suppress)
        oDisplay(arg);
}

HRESULT __stdcall hkReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* pp)
{
    if (g_ImGuiReady)
        ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = oReset(device, pp);
    if (g_ImGuiReady)
        ImGui_ImplDX9_CreateDeviceObjects();
    return hr;
}

// Grabs the real IDirect3DDevice9 vtable by briefly creating a throwaway
// device, so we can hook EndScene/Reset for ANY device the game later
// creates (they all share the same vtable, since it's the same D3D9.dll).
bool HookD3D9(void** endSceneOut, void** resetOut)
{
    LogLine("[init] calling Direct3DCreate9");
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d)
    {
        LogLine("[error] Direct3DCreate9 returned null");
        return false;
    }

    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;

    LogLine("[init] creating dummy device to read vtable");
    IDirect3DDevice9* dummy = nullptr;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, GetDesktopWindow(),
        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dummy);
    if (FAILED(hr) || !dummy)
    {
        LogLinef("[error] CreateDevice failed, hr=0x%x", (unsigned int)hr);
        d3d->Release();
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(dummy);
    *endSceneOut = vtable[42]; // IDirect3DDevice9::EndScene
    *resetOut = vtable[16];    // IDirect3DDevice9::Reset
    LogLinef("[init] vtable read ok, EndScene=0x%x Reset=0x%x",
        (unsigned int)(uintptr_t)*endSceneOut, (unsigned int)(uintptr_t)*resetOut);

    dummy->Release();
    d3d->Release();
    return true;
}

DWORD WINAPI InitThread(LPVOID)
{
    LogLine("[init] KoreanSubtitleOverlay InitThread started");

    LoadConfig();

    if (MH_Initialize() != MH_OK)
    {
        LogLine("[error] MH_Initialize failed");
        return 0;
    }
    LogLine("[init] MinHook initialized");

    void* endSceneAddr = nullptr;
    void* resetAddr = nullptr;
    if (!HookD3D9(&endSceneAddr, &resetAddr))
    {
        LogLine("[error] failed to resolve IDirect3DDevice9 vtable");
        return 0;
    }

    MH_STATUS s1 = MH_CreateHook(endSceneAddr, &hkEndScene, reinterpret_cast<void**>(&oEndScene));
    MH_STATUS s2 = MH_CreateHook(resetAddr, &hkReset, reinterpret_cast<void**>(&oReset));
    MH_STATUS s4 = MH_CreateHook(reinterpret_cast<void*>(kCMessagesDisplayAddr), &hkDisplay,
        reinterpret_cast<void**>(&oDisplay));
    LogLinef("[init] MH_CreateHook results: EndScene=%d Reset=%d Display=%d", (int)s1, (int)s2, (int)s4);

    MH_STATUS s3 = MH_EnableHook(MH_ALL_HOOKS);
    LogLinef("[init] MH_EnableHook result: %d", (int)s3);

    LogLine("[init] setup complete, waiting for first EndScene call");
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        LogLine("=== DllMain: DLL_PROCESS_ATTACH ===");
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        HANDLE th = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        LogLinef("[init] CreateThread returned handle=0x%x", (unsigned int)(uintptr_t)th);
        if (th) CloseHandle(th);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        if (g_ImGuiReady)
        {
            ImGui_ImplDX9_Shutdown();
            ImGui::DestroyContext();
        }
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
    }
    return TRUE;
}
