// Standalone offline diagnostic: runs the exact same jamo-composition
// decoder as KoreanSubtitleOverlay's dllmain.cpp against every
// null-terminated byte run in american.gxt, so remaining unmapped final
// bytes / broken triple-vowels can be found across the WHOLE game script
// instead of one played mission at a time.
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <windows.h>

namespace {

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

constexpr wchar_t kInitialJamo[19] = {
    0x3131,0x3132,0x3134,0x3137,0x3138,0x3139,0x3141,0x3142,0x3143,
    0x3145,0x3146,0x3147,0x3148,0x3149,0x314A,0x314B,0x314C,0x314D,0x314E
};

constexpr wchar_t kMedialJamo[10] = {
    0x314F,0x3151,0x3153,0x3155,0x3157,0x315B,0x315C,0x3160,0x3161,0x3163
};

wchar_t finalJamoForByte(unsigned char b)
{
    switch (b)
    {
        case 0x3C: return 0x314C; // E
        case 0x9D: return 0x3131; // G
        case 0x9E: return 0x3132; // GG - confirmed via "밖에" x3, "낚이던데"
        case 0x9F: return 0x3134;
        case 0xA1: return 0x3135; // NH - confirmed via "않아", "않습니다"
        case 0xA2: return 0x3137; // D - confirmed via "받고","싣는","얻어","믿는"
        case 0xA3: return 0x3139;
        case 0xA4: return 0x313A; // LG - confirmed via "닭대가리", "닭살"
        case 0xA5: return 0x313E; // LM - confirmed via "옮기나"
        case 0xA6: return 0x313B; // LB - confirmed via "밟기"
        case 0xA7: return 0x3140; // LH - confirmed via "잃기","잃었습니다","싫으니"
        case 0xA8: return 0x3141;
        case 0xA9: return 0x3142;
        case 0xAA: return 0x3144; // BS - confirmed via "없다","값"
        case 0xAB: return 0x3145; // S - confirmed via "그것도","것을","거짓말쟁이","멋쟁이"
        case 0xAC: return 0x3146;
        case 0xAD: return 0x3147;
        case 0xAE: return 0x3148; // confirmed via "좆같이도" reading + FontExtract glyph comparison vs 0xAB
        case 0xAF: return 0x314A; // CH - confirmed via "몇" x9+, "쫓는"
        default: return 0;
    }
}

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
int combineMedial(unsigned char firstByte, unsigned char secondByte)
{
    struct Pair { unsigned char a, b; int result; };
    static const Pair table[] = {
        {0x93, 0x5C, 1}, {0x95, 0x5C, 5}, {0x9B, 0x5C, 19}, {0x97, 0x5C, 11},
        {0x99, 0x5C, 16}, {0x97, 0x93, 9}, {0x99, 0x95, 14},
        // 0x9C (the plain medial "I" byte) doubles as an alternate "+i"
        // trigger, same role as 0x5C above - confirmed via "두려움의"
        // (EU+0x9C -> YI) and "뒤에서" (U+0x9C -> WI).
        {0x93, 0x9C, 1}, {0x95, 0x9C, 5}, {0x9B, 0x9C, 19}, {0x97, 0x9C, 11},
        {0x99, 0x9C, 16},
        // YEO + i -> YE, confirmed via "미셸" (Michelle)
        {0x96, 0x5C, 7},
    };
    for (const Pair& p : table)
        if (p.a == firstByte && p.b == secondByte) return p.result;
    return -1;
}
int upgradeMedialWithI(int mi)
{
    switch (mi)
    {
        case 9:  return 10;
        case 14: return 15;
        default: return -1;
    }
}
int finalToUnicodeIndex(wchar_t jamo)
{
    if (jamo == 0) return 0;
    static const wchar_t order[27] = {
        0x3131,0x3132,0x3133,0x3134,0x3135,0x3136,0x3137,0x3139,0x313A,
        0x313B,0x313C,0x313D,0x313E,0x313F,0x3140,0x3141,0x3142,0x3144,
        0x3145,0x3146,0x3147,0x3148,0x314A,0x314B,0x314C,0x314D,0x314E
    };
    for (int i = 0; i < 27; ++i) if (order[i] == jamo) return i + 1;
    return 0;
}

// Same as production DecodeJamoBytes, but also records every high byte
// that fell through unconsumed - the exact signature of a still-unmapped
// final consonant or leftover stray-ASCII bug.
std::wstring DecodeJamoBytes(const unsigned char* text, std::map<unsigned char, int>& unmapped)
{
    std::wstring out;
    size_t i = 0;
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

                if (i + 2 < len)
                {
                    int combined = combineMedial(b1, text[i + 2]);
                    if (combined >= 0)
                    {
                        mi = combined;
                        ++consumed;
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
                        ++consumed;
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
                                ++consumed;
                        }
                        else if (bf >= 0x80)
                        {
                            // Would-be final byte in the "final consonant"
                            // value range but not yet mapped.
                            unmapped[bf]++;
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
        if (b0 >= 0x80) unmapped[b0]++;
        ++i;
    }
    return out;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: gxt_scan <in.gxt> <out_report.txt>\n");
        return 1;
    }

    FILE* f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> buf(size);
    fread(buf.data(), 1, size, f);
    fclose(f);

    FILE* out = fopen(argv[2], "wb");
    if (!out) { fprintf(stderr, "cannot open %s for write\n", argv[2]); return 1; }

    std::map<unsigned char, int> unmapped;
    size_t n = buf.size();

    auto readU32 = [&](size_t off) -> uint32_t {
        return buf[off] | (buf[off+1]<<8) | (buf[off+2]<<16) | (buf[off+3]<<24);
    };
    auto magicIs = [&](size_t off, const char* m) -> bool {
        return off + 4 <= n && memcmp(&buf[off], m, 4) == 0;
    };

    int totalStrings = 0;
    int totalTables = 0;

    // Locate TABL anywhere near the start (some GXT variants prefix a
    // small header before it) instead of assuming a fixed offset.
    size_t tablOff = std::string::npos;
    for (size_t k = 0; k + 4 <= n && k < 64; ++k)
        if (magicIs(k, "TABL")) { tablOff = k; break; }
    if (tablOff == std::string::npos)
    {
        fprintf(stderr, "TABL magic not found near start of file\n");
        return 1;
    }

    uint32_t tablSize = readU32(tablOff + 4);
    size_t entriesStart = tablOff + 8;
    size_t numTables = tablSize / 12;

    for (size_t t = 0; t < numTables; ++t)
    {
        size_t entryOff = entriesStart + t * 12;
        if (entryOff + 12 > n) break;
        uint32_t tableOffset = readU32(entryOff + 8);
        ++totalTables;

        if (!magicIs(tableOffset, "TKEY")) continue; // not a real chunk, skip defensively
        uint32_t tkeySize = readU32(tableOffset + 4);
        size_t tdatOff = tableOffset + 8 + tkeySize;
        if (!magicIs(tdatOff, "TDAT")) continue;
        uint32_t tdatSize = readU32(tdatOff + 4);
        size_t dataStart = tdatOff + 8;
        size_t dataEnd = dataStart + tdatSize;
        if (dataEnd > n) dataEnd = n;

        size_t i = dataStart;
        while (i < dataEnd)
        {
            size_t start = i;
            while (i < dataEnd && buf[i] != 0) ++i;
            size_t runLen = i - start;
            if (i < dataEnd) ++i; // skip the NUL

            if (runLen >= 4)
            {
                bool hasHigh = false;
                for (size_t k = start; k < start + runLen; ++k)
                    if (buf[k] >= 0x80) { hasHigh = true; break; }

                if (hasHigh)
                {
                    ++totalStrings;
                    std::string raw(reinterpret_cast<char*>(buf.data() + start), runLen);
                    std::string stripped = StripGxtTags(raw.c_str());

                    std::map<unsigned char, int> localUnmapped;
                    std::wstring wide = DecodeJamoBytes(
                        reinterpret_cast<const unsigned char*>(stripped.c_str()), localUnmapped);

                    for (auto& kv : localUnmapped) unmapped[kv.first] += kv.second;

                    if (!localUnmapped.empty() && !wide.empty())
                    {
                        int ulen = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
                        if (ulen > 0)
                        {
                            std::string utf8(ulen, '\0');
                            WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &utf8[0], ulen, nullptr, nullptr);
                            if (!utf8.empty() && utf8.back() == '\0') utf8.pop_back();

                            std::string hex;
                            char b[4];
                            for (unsigned char c : raw) { wsprintfA(b, "%02x ", (int)c); hex += b; }

                            std::string flags;
                            for (auto& kv : localUnmapped)
                            {
                                wsprintfA(b, "%02x ", (int)kv.first);
                                flags += b;
                            }

                            std::string line = "[unmapped=" + flags + "] hex=" + hex + " utf8=" + utf8 + "\r\n";
                            fwrite(line.data(), 1, line.size(), out);
                        }
                    }
                }
            }
        }
    }

    std::string summary = "\r\n=== summary ===\r\ntables parsed: " + std::to_string(totalTables) +
        "\r\ntotal high-byte strings scanned: " + std::to_string(totalStrings) + "\r\n";
    fwrite(summary.data(), 1, summary.size(), out);
    for (auto& kv : unmapped)
    {
        char line[128];
        wsprintfA(line, "byte 0x%02x seen %d times (unmapped)\r\n", (int)kv.first, kv.second);
        fwrite(line, 1, lstrlenA(line), out);
    }

    fclose(out);
    printf("done, %d strings with high bytes scanned, %zu distinct unmapped byte values\n",
        totalStrings, unmapped.size());
    return 0;
}
