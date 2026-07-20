# Korean Subtitle Overlay for GTA San Andreas (WIP)

A modloader ASI plugin that fixes broken in-game subtitles when playing
GTA San Andreas (1.0 US) with the community Korean text patch.

## The problem

The Korean patch's `fonts.txd` isn't a real text encoding - it's a
"jamo composition" font (like old console Korean fonts): each byte in
the GXT dialogue strings indexes either a Latin/ASCII glyph or, for the
`0x80+` range, an individual Hangul consonant/vowel piece (jamo). The
game's native renderer just draws 2-4 of these glyph pieces overlapping
on screen to fuse them into one Hangul syllable box. Rendering it
"as text" (e.g. copying it out, or any tool that isn't the exact
original glyph-overlay renderer) produces garbage.

## What this plugin does

Every frame, it reads the already-resolved subtitle strings directly out
of `CMessages::BIGMessages` / `BriefMessages`, decodes the jamo-composition
byte sequences into real Unicode Hangul syllables itself, and draws them
with Dear ImGui using a system Korean TrueType font (Malgun Gothic),
layered on top of (and effectively replacing) the game's own broken
subtitle draw call.

The decode table (`src/dllmain.cpp`) was reverse-engineered by visually
inspecting the patch's `fonts.txd` glyph grid and cross-referencing
against real dialogue, then verified exhaustively by running the exact
decoder offline against every dialogue string in `american.gxt`
(1775 Korean-encoded strings across all 127 GXT tables) until zero
bytes were left unmapped.

## Dependencies

- Windows, MSVC v143 toolset (Visual Studio 2022)
- [Dear ImGui](https://github.com/ocornut/imgui) (tested against 1.92.9 WIP) with the DX9 backend (`imgui_impl_dx9`)
- [MinHook](https://github.com/TsudaKageyu/minhook) 1.3 (built from source, static)
- DirectX 9 (`d3d9.lib`, part of the Windows SDK)

MinHook and Dear ImGui are **not vendored** in this repo - the project
file compiles their sources directly from sibling directories:

```
GTA-SA-Dev/
├── KoreanSubtitleOverlay/   (this repo)
├── imgui/                   (github.com/ocornut/imgui)
└── minhook/                 (github.com/TsudaKageyu/minhook)
```

Clone both next to this repo before building.

## Building

1. Clone `imgui` and `minhook` as siblings of this repo (see layout above).
2. Open `KoreanSubtitleOverlay.vcxproj` in Visual Studio 2022.
3. Build `Release|Win32` (GTA SA is a 32-bit game).
4. Output: `bin/KoreanSubtitleOverlay.asi`.

## Installing

Requires:
- A [modloader](https://gtaforums.com/topic/577721-relmod-modloader/) install of GTA San Andreas 1.0 US.
- The Korean text patch already installed (needs its `fonts.dat`,
  `fonts.txd`, and `american.gxt` in place - this plugin only fixes the
  *rendering*, it doesn't add Korean text on its own).

Steps:
1. Build (or download) `KoreanSubtitleOverlay.asi`.
2. Copy it to `modloader/_BASIC/KoreanSubtitleOverlay/KoreanSubtitleOverlay.asi`
   in your GTA SA install directory.
3. Launch the game. A `KoreanSubtitleOverlay.log` will appear next to the
   `.asi` with init/debug output if something goes wrong.

See [DECODING_NOTES.md](DECODING_NOTES.md) for the full byte-mapping
evidence table and verification methodology.

## Known limitations

Every byte in the final-consonant and compound-vowel tables has now been
confirmed either against real dialogue text, direct glyph comparison via
FontExtract, or both - see the comments in `finalJamoForByte()` /
`combineMedial()` in `src/dllmain.cpp`.

## TODO

#### 인게임 실제 렌더링 스크린샷 검증
