// Korean Subtitle Overlay for GTA SA 1.0 US
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

// GXT strings embed inline formatting tags like "~z~", "~1~", "~s~" that
// the native CFont renderer interprets as color/substitution codes and
// strips before drawing. CP949 is a variable-width (double-byte) encoding,
// so leaving one of these single-byte ASCII tags in place shifts the
// byte-pairing for every Korean character that follows it, corrupting the
// rest of the string. Strip anything matching ~...~ before decoding.
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

std::string StripGxtTags(const char* text)
{
    std::string out;
    for (const char* p = text; *p; )
    {
        if (*p == '~')
        {
            const char* end = p + 1;
            while (*end && *end != '~') ++end;
            if (*end == '~' && LooksLikeGxtTag(p + 1, end))
            {
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
    switch (b)
    {
        case 0x3C: return 0x314C; // ㅌ - confirmed via "같이" in two independent lines (80 93 3C 60 8B 9C 60);
                                   // reuses the ASCII '<' slot, but only ever read here, inside an
                                   // already-detected initial+medial pair, so plain '<' text is unaffected
        case 0x9D: return 0x3131; // ㄱ - confirmed via "가속하려면", "죽기엔" (most common final, by far the biggest source of stray '<'/']' before this was added)
        case 0x9E: return 0x3132; // ㄲ - confirmed via "밖에" (x3), "낚이던데"
        case 0x9F: return 0x3134; // ㄴ - confirmed via "만"
        case 0xA1: return 0x3135; // ㄶ - confirmed via "않아", "않습니다"
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

void LogRawBytesOnce(const char* text)
{
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
    LogLinef("[subtitle] raw bytes: %s text=\"%s\"", hex.c_str(), text);
}

void LoadKoreanFont()
{
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    g_KoreanFont = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\malgun.ttf", 26.0f, &cfg, io.Fonts->GetGlyphRangesKorean());
    if (!g_KoreanFont)
    {
        LogLine("[warn] malgun.ttf not found, falling back to default ImGui font (no Hangul)");
        g_KoreanFont = io.Fonts->AddFontDefault();
    }
}

// Fires CMessages::AddBigMessage() with a known-good test string the
// moment a cutscene starts, so the jamo-composition render pipeline can
// be verified without needing real GXT dialogue. The bytes below are this
// font's own custom jamo encoding (see DecodeJamoBytes) for "라이더"
// (Ryder) - chosen because it's the exact sequence that was reverse
// engineered from a real dialogue line, so a correct render here confirms
// the decoder against a known-true answer.
void CheckCutsceneEntry()
{
    static bool wasRunning = false;
    bool isRunning = *CutsceneMgr_ms_running;
    if (isRunning && !wasRunning)
    {
        LogLine("[cutscene] entry detected, firing test subtitle");
        static const char kHello[] =
            "\x85\x93\x60\x8b\x9c\x60\x83\x95\x60";
        AddBigMessage(kHello, 5000, STYLE_MIDDLE);
    }
    wasRunning = isRunning;
}

std::string DecodeToUtf8(const char* text)
{
    LogRawBytesOnce(text);
    std::string stripped = StripGxtTags(text);
    std::wstring wide = DecodeJamoBytes(reinterpret_cast<const unsigned char*>(stripped.c_str()));
    if (wide.empty()) return {};
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (ulen <= 0) return {};
    std::string utf8(ulen, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, utf8.data(), ulen, nullptr, nullptr);
    if (!utf8.empty() && utf8.back() == '\0') utf8.pop_back();
    return utf8;
}

// Scrolling subtitle log: newest line at the bottom, capped at 2 visible
// lines, similar to how the base game's own message stack works. Every
// message source (BIGMessages styles + BriefMessages slots) is tracked
// independently so a message only gets appended to history once, right
// when it first appears - not re-appended every single frame it stays
// on screen.
constexpr int kMaxSlots = 7 /*kDialogueStyles*/ + kBriefMessageCount;
constexpr size_t kMaxHistoryLines = 1;
std::string g_lastSeenPerSlot[kMaxSlots];
std::vector<std::string> g_history;

bool g_anySlotActive = false;

void ProcessSlot(const char* text, int slot)
{
    if (!text || !text[0])
    {
        g_lastSeenPerSlot[slot].clear();
        return;
    }
    g_anySlotActive = true;

    std::string utf8 = DecodeToUtf8(text);
    if (utf8.empty() || utf8 == g_lastSeenPerSlot[slot]) return;
    g_lastSeenPerSlot[slot] = utf8;

    if (!g_history.empty() && g_history.back() == utf8) return;
    g_history.push_back(utf8);
    if (g_history.size() > kMaxHistoryLines)
        g_history.erase(g_history.begin());
}

void DrawDialogueSubtitles()
{
    g_anySlotActive = false;
    int slot = 0;
    for (eMessageStyle style : kDialogueStyles)
        ProcessSlot(BIGMessages[style].m_Current.m_pText, slot++);
    for (int i = 0; i < kBriefMessageCount; ++i)
        ProcessSlot(BriefMessages[i].m_pText, slot++);

    // The native game cleared every message slot (its own subtitle would
    // have disappeared too) - drop our own history instead of leaving the
    // last line stuck on screen forever.
    if (!g_anySlotActive)
    {
        g_history.clear();
        return;
    }
    if (g_history.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.82f),
        ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.6f);
    ImGui::PushFont(g_KoreanFont);
    ImGui::Begin("##korean_sub_log", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
    for (const std::string& line : g_history)
        ImGui::TextUnformatted(line.c_str());
    ImGui::End();
    ImGui::PopFont();
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

    CheckCutsceneEntry();

    ImGui_ImplDX9_NewFrame();
    ImGui::NewFrame();
    DrawDialogueSubtitles();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

    return oEndScene(device);
}

// Swallows the native subtitle draw call entirely. CMessages::Process
// (unhooked, still runs normally) keeps advancing/expiring messages, so
// BIGMessages/BriefMessages stay valid for our own overlay to read - only
// the broken-glyph native rendering is suppressed.
void __cdecl hkDisplay(bool)
{
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
