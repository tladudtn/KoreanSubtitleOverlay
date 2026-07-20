# 개발자용 디버깅 가이드

이 프로젝트를 개발/수정하면서 실제로 썼던 디버깅 수단을 한 곳에 모은
문서다. 각 도구가 정확히 뭘 확인해주는지, 언제 어떤 걸 써야 하는지
정리한다.

## 목차

1. [런타임 로그 — `KoreanSubtitleOverlay.log`](#1-런타임-로그--koreansubtitleoverlaylog)
2. [오프라인 전수 검증 — `tools/gxt_scan.cpp`](#2-오프라인-전수-검증--toolsgxt_scancpp)
3. [글리프 직접 확인 — `FontExtract`](#3-글리프-직접-확인--fontextract)
4. [타이밍 버그: 화면 녹화 + ffmpeg 프레임 대조](#4-타이밍-버그-화면-녹화--ffmpeg-프레임-대조)
5. [어떤 문제엔 뭘 써야 하는지](#5-어떤-문제엔-뭘-써야-하는지)

---

## 1. 런타임 로그 — `KoreanSubtitleOverlay.log`

**위치**: `.asi`와 같은 폴더 (`modloader/_BASIC/KoreanSubtitleOverlay/
KoreanSubtitleOverlay.log`). 게임을 실행하면 자동 생성/이어쓰기된다.
구현은 `src/dllmain.cpp`의 `LogLine()`/`LogLinef()` — CRT iostream을
안 쓰고 `CreateFileA`/`WriteFile`을 직접 호출하는 이유는 코드 상단
주석 참고(DllMain에서 스레드로 띄운 뒤 `std::ofstream`을 쓰면 파일이
아예 안 만들어지는 문제가 있었음).

**로그 프리픽스별 의미**:

| 프리픽스 | 찍히는 시점 | 확인할 것 |
|---|---|---|
| `=== DllMain: ... ===` | DLL 로드/언로드 | 플러그인이 아예 로드됐는지 |
| `[init] ...` | 후킹 초기화 단계 (`InitThread`, `HookD3D9`, `EnsureImGuiInit`) | MinHook 초기화, vtable 주소, `MH_CreateHook`/`MH_EnableHook` 결과 코드 - 여기서 실패하면 오버레이 자체가 절대 안 뜬다 |
| `[error] ...` | 초기화 실패 지점 | `Direct3DCreate9`/`CreateDevice` 실패, vtable 못 읽음 등 |
| `[warn] ...` | 폰트 폴백 | `malgunbd.ttf` 못 찾아서 `malgun.ttf`로, 그마저 없어서 기본 폰트로 폴백했는지 |
| `[subtitle] raw bytes: ...` | 새 자막 문자열이 감지될 때마다 1회 | 자모 디코더에 실제로 들어간 원본 바이트 시퀀스(헥스) - 매핑 안 된 바이트를 의심할 때 여기서 원본을 봐야 함 |
| `[subtitle] now showing: ...` | 디코딩된 문자열이 바뀔 때마다 | 최종적으로 화면에 그려지는 UTF-8 결과 |
| `[subtitle-debug] selection changed, active slots: ...` | `PickCurrentMessage()`의 선택 결과가 바뀔 때마다 | `BIGMessages`/`BriefMessages` 15개 슬롯 중 지금 뭐가 활성인지, 그중 어떤 게 선택됐는지(`<-- chosen`) - 대사가 잘못 골라지거나 옛 대사가 다시 뜨는 버그 조사용 |
| `[display-debug] native draw SUPPRESSED/ALLOWED ...` | `hkDisplay`의 억제 여부가 뒤바뀔 때마다 | 원본(깨진) 자막 그리기가 언제 막히고 언제 풀리는지, `until`/`now` 틱값 - 대사 전환 시 겹침/깜빡임 버그는 대부분 여기서 원인이 드러남 |
| `[subtitles-pref] current value at 0xBA678C: ...` | 최초 1회 | `PrefsShowSubtitles` 강제 on 처리 직전 원래 값 |

**변경 시 찍히는 것만 로깅**: 위 상당수는 "매 프레임"이 아니라 "값이
바뀔 때만" 찍히도록 되어 있다(그렇지 않으면 로그가 순식간에 수백MB로
불어남). 새로운 디버그 로그를 추가할 땐 이 패턴(정적 변수로 이전 상태
기억, 바뀔 때만 출력)을 따르는 게 좋다.

**타이밍 버그는 로그만으로 부족할 수 있다**: "언제, 어떤 순서로
그려지는가" 계열 버그(자막 전환 시 겹침/깜빡임 등)는 로그 타임스탬프
(`GetTickCount()` 기반)만으로 재구성이 안 될 때가 있다. 이럴 땐
[4번](#4-타이밍-버그-화면-녹화--ffmpeg-프레임-대조)의 화면 녹화 +
ffmpeg 프레임 대조 절차를 쓴다.

---

## 2. 오프라인 전수 검증 — `tools/gxt_scan.cpp`

**목적**: 게임을 켜지 않고, `dllmain.cpp`와 완전히 동일한 자모 디코드
로직을 복제해서 `american.gxt` 파일 자체를 파싱 - 게임에 있는 모든
대사 문자열(127개 테이블, 한글 인코딩 문자열 전부)을 한 번에 디코딩해
미매핑 바이트가 있는지 검사하는 도구. 미션 하나하나 직접 플레이하며
확인하는 것보다 훨씬 철저하다.

**GXT 파일 구조**: `TABL`(테이블명+오프셋 목록) → 각 테이블마다
`TKEY`(8바이트 엔트리: offset+hash) → `TDAT`(실제 null-terminated
문자열 블롭). **TKEY 영역을 문자열로 착각해서 스캔하면 노이즈가 대량
발생하므로 반드시 TDAT 영역만 스캔해야 한다** (초기 버전에서 이 실수로
가짜 미매핑 바이트 128개가 나온 적 있음).

**빌드**:
```
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cl.exe /EHsc /nologo /Fe:gxt_scan.exe tools\gxt_scan.cpp /link user32.lib
```

**실행**:
```
gxt_scan.exe "<게임경로>\modloader\_BASIC\Korean_Patch\american.gxt" report.txt
```

**출력 해석**: `report.txt` 마지막 줄 요약에 `distinct unmapped byte
values`가 **0**이면 전수 검증 통과. 0이 아니면 어떤 바이트가 몇 번,
어느 문자열에서 안 잡혔는지가 리포트에 나온다.

**주의**: `finalJamoForByte()`/`combineMedial()` 매핑 테이블은
`dllmain.cpp`와 이 파일에 **각각 따로** 있고 자동으로 동기화되지
않는다 - `dllmain.cpp`의 디코드 로직을 고치면 이 파일도 반드시 같이
고칠 것.

---

## 3. 글리프 직접 확인 — `FontExtract`

**위치**: `../../FontExtract` (이 저장소 밖, 옆 디렉터리 - 별도
프로젝트). `libtxd`(TXD 읽기/쓰기 라이브러리)는 서드파티 오픈소스
[txdedit](https://github.com/vaibhavpandeyvpz/txdedit)를 로컬에 clone해
가져다 쓴다(자세한 배경은 [HD_FONT_MERGE.md](HD_FONT_MERGE.md) 참고).

**목적**: `fonts.txd` 안의 텍스처를 16×N 글자 격자로 잘라서 각 셀을
`cell_XX.png`(XX=글자 코드, hex)로 개별 저장. 텍스트 근거가 약하거나
없는 바이트를 매핑할 때, 이미 확정된 바이트의 글리프 모양과 직접 눈으로
비교(Read 툴로 이미지를 열어서)해 검증할 수 있다.

**빌드**: `FontExtract/build.bat` 실행 (vcvars64.bat 호출 후 `main.cpp`
+ `libtxd`/`libsquish`/`libimagequant` 소스를 한 번에 컴파일).

**실행**:
```
FontExtract.exe <fonts.txd 경로> <출력 디렉터리>
```

**출력물** (텍스처별로, 예: `font1`이면):
- `N_font1_rgba.png` — 텍스처 전체를 RGBA로 디코딩한 원본
- `N_font1_alpha.png` — 알파 채널만 흑백으로 뽑은 것 (글리프 모양이
  실제로 여기 있음 - SA 폰트 텍스처는 글리프가 알파 채널에 있다)
- `N_font1_grid.png` — 위 알파 이미지에 빨간 격자선을 그려 4배
  확대한 것. 여러 바이트를 한 번에 비교하거나 셀 경계 정렬을 확인할 때
  씀 (받침 글리프가 셀 하단에 배치되는 게 정상이라 "잘린 것처럼"
  보이는 건 버그 아님)
- `N_font1_cells/cell_XX.png` — 셀 하나하나를 개별 파일로 잘라낸 것
  (XX = 그 칸의 글자 코드, hex). 특정 바이트 하나만 볼 때 격자 이미지
  전체에서 세는 것보다 빠름

**알아둘 것 (DXT5 지원)**: 원래 `libtxd`는 `DXT1`/`DXT3`만 지원했고
`DXT5` 압축 텍스처를 열면 크래시가 났다(힙 버퍼 오버런 - 압축 인식을
못 해서 비압축이라고 착각하고 실제보다 훨씬 큰 범위를 읽으려 함). 이걸
로컬 `txdedit` clone에 패치해서 고쳤다 - 자세한 원인/수정 내용은
[HD_FONT_MERGE.md의 "막혀있던 부분"](HD_FONT_MERGE.md#막혀있던-부분-dxt5-미지원-버그)
참고. 이 수정은 아직 upstream에 반영 안 된 로컬 패치 상태다.

---

## 4. 타이밍 버그: 화면 녹화 + ffmpeg 프레임 대조

**목적**: "언제, 어떤 순서로 그려지는가" 계열 버그(자막 전환 시 겹침/
깜빡임 등)는 바이트 매핑 검증만으로는 못 잡는다. 실제 플레이 중
타이밍이 문제이기 때문에, 위 1번(런타임 로그)만으로 재구성이 안 될 때
쓰는 절차다.

**순서**:

1. `dllmain.cpp`에 상태 전환이 있을 때만 찍는 디버그 로그를 미리
   추가해둔다 (매 프레임 로그하면 순식간에 파일이 거대해지므로,
   "선택된 슬롯이 바뀔 때만", "네이티브 억제 on/off가 바뀔 때만" 식으로
   트리거를 건다 - `LogSubtitleSelectionIfChanged`, `hkDisplay`의
   `[display-debug]` 로그가 실제 예시).
2. 문제되는 구간을 화면 녹화한다(Xbox Game Bar 등).
3. `ffmpeg`(winget으로 설치: `winget install --id Gyan.FFmpeg`)로
   영상에서 자막이 나오는 화면 하단 영역만 crop해서 **60fps 그대로
   (다운샘플링 없이)** 프레임 이미지로 추출한다:
   ```
   ffmpeg -i in.mp4 -vf "crop=W:H:X:Y" f_%05d.jpg
   ```
4. 프레임 수가 많아서(51초 영상 기준 3097장) 한 장씩 보는 대신,
   ffmpeg의 `tile` 필터로 여러 프레임을 한 장의 콘택트시트 이미지로
   묶어서(`scale=작게,tile=5x50` 식) 한 번에 훑어본다. 이러면 Read 툴
   호출 몇 번으로 전체 영상을 다 확인할 수 있다.
5. 로그의 타임스탬프(`GetTickCount()` 기반)와 영상 속 대사 내용을
   나란히 대조해서, "그 순간 실제로 무슨 일이 있었는지"를 재구성한다.

**주의**: 콘택트시트를 너무 작게 스케일하면(예: 384x30px 셀) "겹쳐
보이는" 것 같은 명백한 이상은 잘 보여도, "문장이 한 글자 덜 나온"
것처럼 텍스트 내용을 한 글자 한 글자 비교해야 알 수 있는 미묘한 차이는
놓치기 쉽다 - 실제로 그렇게 한 번 놓친 사례가 있었고, 사용자가
스크린샷으로 직접 짚어준 뒤에야 발견했다. 의심되는 특정 구간은 스케일을
낮추지 말고 개별 프레임을 확대해서 봐야 한다.

---

## 5. 어떤 문제엔 뭘 써야 하는지

| 상황 | 먼저 쓸 도구 |
|---|---|
| 특정 대사가 깨져 나온다 (매핑 안 된 바이트 의심) | `KoreanSubtitleOverlay.log`의 `[subtitle] raw bytes:` 로 원본 바이트 확인 → `gxt_scan.cpp`로 같은 바이트가 다른 문장에서도 나오는지 전수 검사 |
| 새로 발견한 바이트가 어느 자모인지 확신이 안 선다 | `FontExtract`로 그 바이트의 글리프 셀을 뽑아서 이미 확정된 자모들과 모양 비교 |
| 대사 전환 시 겹치거나 잘려 보인다 | `KoreanSubtitleOverlay.log`의 `[display-debug]`/`[subtitle-debug]` 타임스탬프 대조, 재현 안 되면 [4번](#4-타이밍-버그-화면-녹화--ffmpeg-프레임-대조) 화면 녹화 + ffmpeg 프레임 대조 |
| 오버레이가 아예 안 뜬다 | `KoreanSubtitleOverlay.log`의 `[init]`/`[error]` 라인부터 확인 - MinHook 초기화나 `MH_CreateHook`/`MH_EnableHook` 결과 코드가 실패인지부터 봄 |
| `fonts.txd` 안 텍스처 구조/이름/해상도를 확인하고 싶다 | `FontExtract` 콘솔 출력(`texture count`, 텍스처별 이름/크기/압축 방식) |
