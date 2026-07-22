# 개발자 가이드

이 프로젝트를 빌드/수정/디버깅하려는 개발자를 위한 참고 문서. "지금
코드가 어떻게 동작하는지"와 "문제가 생겼을 때 뭘로 확인하는지"를
다룬다. 각 결정의 배경(왜 이렇게 됐는지, 어떤 시행착오를 거쳤는지)은
여기 없고 [WORK_LOG.md](WORK_LOG.md)에 있다. 빌드/설치 절차와 의존성
목록은 [../README.md](../README.md), 비개발자용 안내는
[가이드.md](가이드.md) 참고.

## 목차

- [프로젝트 구조](#프로젝트-구조)
- [후킹 구조 / 오버레이 렌더링 구조](#후킹-구조--오버레이-렌더링-구조)
- [설정 파일 — KoreanSubtitleOverlay.ini](#설정-파일--koreansubtitleoverlayini)
- [디버깅 도구](#디버깅-도구)

---

## 프로젝트 구조

```
C:\Users\SYS\Documents\GTA-SA-Dev\
├── KoreanSubtitleOverlay\   (이 프로젝트, git 저장소)
│   ├── src\dllmain.cpp      실제 플러그인 소스 (디코더 + 오버레이 렌더링 본체)
│   ├── src\sa_messages.h    게임 구조체/주소 (plugin-sdk 참조)
│   ├── tools\gxt_scan.cpp   오프라인 전수 검증 도구 (아래 "디버깅 도구" 참고)
│   └── docs\                문서 모음 (이 파일, WORK_LOG.md, 가이드.md, screenshots\)
├── imgui\                   (사이드로드, 프로젝트에 포함 안 됨)
├── minhook\                 (사이드로드, 프로젝트에 포함 안 됨)
├── FontExtract\             fonts.txd 글리프 추출 + merge_fonts 병합 도구 (별도 프로젝트,
│                             자세한 내용은 WORK_LOG.md)
└── txdedit\                 서드파티 오픈소스 TXD 편집기 clone, libtxd가 위 도구들의 실제 구현체
```

게임 설치 경로: `C:\Program Files (x86)\Steam\steamapps\common\Grand Theft Auto San Andreas`
- 배포 위치: `modloader\_BASIC\KoreanSubtitleOverlay\KoreanSubtitleOverlay.asi`
- 원본 한글패치 에셋(참조용): `modloader\_BASIC\Korean_Patch\{american.gxt, fonts.dat, fonts.txd}`

### 이 저장소만 clone해도 빌드가 완결되는지

`KoreanSubtitleOverlay.vcxproj`가 참조하는 로컬 파일은 `src\dllmain.cpp`,
`src\sa_messages.h` 둘뿐이고 둘 다 이 저장소 안에 있다 - 나머지 참조
(`..\imgui\*`, `..\minhook\*`)는 전부 [README.md](../README.md)의
빌드 의존성에 문서화된 사이드로드 clone이다. 소스의 `#include`도
표준 라이브러리/Windows SDK/imgui·MinHook뿐이라 미문서화된 숨은
의존성은 없다. `.gitignore`는 빌드 산출물(`bin/`, obj 폴더,
`*.pdb`/`*.obj`/`*.tlog` 등)과 개인용 `*.local.md`만 정확히 걸러낸다.
`.sln`은 없지만 `.vcxproj`를 VS2022에서 바로 열면 되므로 문제없다.

즉 README의 안내(`imgui`/`minhook`을 옆에 clone)만 따라 하면, 이
저장소 하나만 클론해서 바로 빌드할 수 있는 상태다. `FontExtract`/
`txdedit`(HD 폰트 병합용 별도 도구, WORK_LOG.md Part 2 참고)는 이 ASI
빌드 자체에는 필요 없다.

---

## 후킹 구조 / 오버레이 렌더링 구조

"어떤 함수를 어떻게 후킹했고, 오버레이를 화면에 어떻게 띄웠는지"를
다룬다. 자모 바이트를 유니코드로 디코딩하는 로직 자체는
[WORK_LOG.md](WORK_LOG.md#바이트-레이아웃)를, 자막 선택/네이티브 억제
타이밍 버그(대사 전환 시 겹침/잘림)에 대한 상세 시행착오 기록은
[WORK_LOG.md의 "오버레이 렌더링 로직"](WORK_LOG.md#오버레이-렌더링-로직-자막-선택--네이티브-억제--전환-버그)
절 참고. 여기서는 지금 동작하는 구조를 처음부터 정리한다. 코드 위치는
전부 `src/dllmain.cpp` 기준.

### 전체 그림

```
DllMain(DLL_PROCESS_ATTACH)
  -> InitThread (별도 스레드)
       -> MH_Initialize()
       -> HookD3D9()로 EndScene/Reset 실제 주소 알아냄
       -> MH_CreateHook: EndScene, Reset, CMessages::Display
       -> MH_EnableHook(MH_ALL_HOOKS)

매 프레임, 게임이 IDirect3DDevice9::EndScene을 호출할 때마다:
  hkEndScene
    -> (최초 1회) EnsureImGuiInit: ImGui 컨텍스트 생성 + 한글 폰트 로드
    -> UpdateDisplaySize: 현재 백버퍼 크기로 ImGui DisplaySize 갱신
    -> ForceSubtitlesPref: "자막" 옵션 강제 on
    -> ImGui NewFrame -> DrawDialogueSubtitles() -> ImGui Render
    -> ImGui_ImplDX9_RenderDrawData
    -> 원본 EndScene 호출 (oEndScene)

게임이 자막을 그리려고 CMessages::Display를 호출할 때마다:
  hkDisplay
    -> 지금 한글 메시지가 활성인지 + 그 메시지의 표시 시간이 아직
       안 끝났는지로 "억제할지" 판단
    -> 억제 안 하면(순수 ASCII 배너 등) 원본 그대로 통과(oDisplay)
```

### 후킹 대상 3곳과 그 이유

- **`IDirect3DDevice9::EndScene`** - 매 프레임 렌더링이 끝나는 시점.
  ImGui를 여기서 그린다(원본 호출 직전에 우리 draw call들을 먼저
  제출).
- **`IDirect3DDevice9::Reset`** - 창모드 전환·해상도 변경 시 D3D
  디바이스가 재생성된다. 이때 ImGui가 들고 있던 D3D 리소스(텍스처,
  버텍스 버퍼 등)도 무효화됐다가 다시 만들어져야 하므로, 원본 Reset
  전후로 `ImGui_ImplDX9_InvalidateDeviceObjects`/`CreateDeviceObjects`를
  끼워 넣는다 (`hkReset`).
- **`CMessages::Display`** (게임 메모리 고정 주소 `kCMessagesDisplayAddr`,
  `sa_messages.h`) - 게임 원본의 (깨진 한글) 자막 그리기 함수. 이걸
  후킹해서 한글 메시지가 활성인 동안만 조건부로 호출을 건너뛴다.

### EndScene 주소를 얻는 방법: 더미 디바이스

DLL이 로드되는 시점엔 게임이 아직 실제 D3D9 디바이스를 안 만들었을 수도
있어서, 그 인스턴스에 직접 접근할 방법이 없다. 대신 `HookD3D9()`가
**임시 더미 디바이스를 하나 직접 만들어서** vtable만 읽고 바로
버린다:

```cpp
IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
d3d->CreateDevice(..., GetDesktopWindow(), ..., &dummy);
void** vtable = *reinterpret_cast<void***>(dummy);
endSceneAddr = vtable[42]; // IDirect3DDevice9::EndScene
resetAddr    = vtable[16]; // IDirect3DDevice9::Reset
dummy->Release();
d3d->Release();
```

같은 `d3d9.dll`을 쓰는 한 vtable 레이아웃(각 가상함수의 슬롯 번호)은
어떤 디바이스 인스턴스든 동일하다. 그래서 이 더미 디바이스에서 읽은
함수 주소에 MinHook을 걸어두면, 나중에 게임이 실제로 만드는 (다른
인스턴스의) 디바이스에도 똑같이 적용된다 - COM처럼 인스턴스별 vtable이
아니라 클래스(= DLL) 단위로 하나의 vtable을 공유하기 때문.

### ImGui는 첫 EndScene 호출 시점에 지연 초기화

DLL 로드 직후엔 아직 진짜 디바이스 포인터가 없다. 그래서 `ImGui::
CreateContext()`/`ImGui_ImplDX9_Init()`는 `hkEndScene`이 처음 호출될
때, 그 인자로 넘어온 진짜 `device`를 가지고 딱 한 번만 실행한다
(`EnsureImGuiInit`, `g_ImGuiReady` 플래그로 가드). 이때 한글 폰트도
같이 로드한다(`LoadKoreanFont`): `C:\Windows\Fonts\malgunbd.ttf`(맑은
고딕 볼드)를 우선 시도하고, 없으면 `malgun.ttf` → 그마저 없으면 ImGui
기본 폰트(한글 미지원)로 순차 폴백.

### 자막 데이터는 별도 후킹 없이 게임 메모리를 직접 읽음

`CMessages::AddMessage`류를 후킹하는 방식이 아니다. 대신 게임이 이미
채워놓은 전역 배열 `BIGMessages`(스타일 7종) / `BriefMessages`(슬롯
8개) - `sa_messages.h`에서 실제 게임 메모리 주소로 매핑된 구조체 -
를 **매 프레임 직접 읽는다**. `PickCurrentMessage()`가 이 15개 슬롯
중 텍스트가 있고 한글 바이트를 포함하며(`ContainsHighByte`) 가장 최근에
시작된(`m_dwStartTime` 최댓값) 것 하나를 고른다. `DrawDialogueSubtitles()`
가 그 텍스트를 `DecodeToUtf8()`(자모 디코더,
[WORK_LOG.md](WORK_LOG.md#바이트-레이아웃) 참고)로 변환해서 ImGui
창으로 그린다.

이 방식의 장점: `CMessages`가 메시지를 큐에 넣고 빼는 원본 로직
(`CMessages::Process` 등)은 전혀 건드리지 않고 그대로 돌아가게 둔 채,
"이미 결정된 결과"만 읽기만 하면 되므로 게임 로직에 개입할 필요가
없다.

### 원본 그리기는 무조건이 아니라 조건부로만 막음

`hkDisplay`는 매번 원본을 죽이는 게 아니다:

```cpp
if (msg) // 한글 메시지가 활성이면
    g_nativeSuppressUntilTick = now + duration + kSuppressSafetyMarginMs;

bool suppress = (msg != nullptr) || (now < g_nativeSuppressUntilTick);
if (!suppress)
    oDisplay(arg); // 억제 안 할 때만 원본 호출
```

`duration`은 그 메시지 자신이 선언한 표시 시간(`tMessage::m_dwTime`)이다
- 즉 한글 메시지가 화면에 떠 있는 동안 + 그 메시지가 원래 표시되기로
한 시간이 끝날 때까지는 계속 원본을 막고, 그 이후엔 다시 원본이 통과되게
풀어준다. `PickCurrentMessage()`가 순수 ASCII 배너(WASTED/BUSTED/MISSION
PASSED)는 애초에 고르지 않으므로, 그런 배너가 떠 있을 땐 `msg`가
`nullptr`이라 원본이 그대로 통과되어 게임 본연의 폰트로 정상 렌더링된다.

이 "얼마나 오래 억제할지"를 프레임 카운트 같은 어림값이 아니라 메시지의
실제 선언 지속시간 + 안전 여유(`kSuppressSafetyMarginMs`)로 계산하게 된
경위(초기엔 짧게 잡았다가 실제 로그로 타이밍 경합을 재현/확인한 과정)는
[WORK_LOG.md의 "네이티브 억제 타이밍: `hkDisplay`"](WORK_LOG.md#네이티브-억제-타이밍-hkdisplay)
절 참고.

### 정리/해제

`DllMain`의 `DLL_PROCESS_DETACH`에서 `ImGui_ImplDX9_Shutdown()` +
`ImGui::DestroyContext()`, 그리고 `MH_DisableHook(MH_ALL_HOOKS)` +
`MH_Uninitialize()`로 정리한다.

---

## 설정 파일 — KoreanSubtitleOverlay.ini

`FontSize`/`OffsetX`/`OffsetY`/`Debug` 네 값은 하드코딩 상수가 아니라
`.asi`와 같은 폴더의 `KoreanSubtitleOverlay.ini`에서 매 실행 시 한 번
읽어온다(`src/dllmain.cpp`의 `Config` 구조체, `LoadConfig()`). 저장소
루트의 `KoreanSubtitleOverlay.ini`가 기본값이 채워진 배포용 템플릿이다.

**읽는 시점**: `InitThread`가 `MH_Initialize()`보다 먼저 `LoadConfig()`를
호출한다 - D3D9 후킹이나 게임 상태와는 무관한 순수 파일 I/O라 스레드
어디서 불러도 안전하지만, `EnsureImGuiInit()`(첫 `EndScene`에서
`LoadKoreanFont()`가 폰트 아틀라스를 굽는 시점)보다는 반드시 먼저
끝나 있어야 `FontSize`가 폰트 로드에 반영된다.

**ini 파일 위치를 찾는 방법**: 게임의 현재 작업 디렉터리에 의존하지
않고, `GetModuleFileNameA(g_hModule, ...)`로 **이 DLL 자신의 실제 경로**를
얻어서 같은 폴더의 `KoreanSubtitleOverlay.ini`를 연다(`BuildIniPath()`).
`g_hModule`은 `DllMain`의 `DLL_PROCESS_ATTACH`에서 넘어오는 `hModule`을
저장해둔 것.

**파싱**: 여분의 ini 파싱 라이브러리를 끌어오지 않고 Windows 표준 API
(`GetPrivateProfileStringA`, `kernel32`에 이미 있음)로 `[Subtitle]` 섹션의
문자열을 읽은 뒤 `atoi`/`atof`로 직접 변환한다(`ReadIntSetting`/
`ReadFloatSetting`/`ReadBoolSetting`). `GetPrivateProfileIntA`를 안 쓰는
이유: 음수 오프셋(예: `OffsetY=-20`)을 안정적으로 파싱하려고 문자열 경로를
택함. ini 파일 자체가 없거나 키가 빠져 있으면 각 함수가 `Config`
구조체의 기본값(코드 상단, 이 옵션이 생기기 전의 원래 하드코딩 값과
동일)으로 폴백하므로, ini 없이 `.asi`만 깔아도 이전과 동일하게 동작한다.

**`Debug` 값이 로그에 미치는 영향**: `Debug=no`(기본값)일 땐 초기화/에러/
경고 로그(`[init]`/`[error]`/`[warn]`, `[config]`)만 남고, 매 자막 줄/매
프레임 단위로 찍히던 진단 로그(`[subtitle] raw bytes:`, `[subtitle] now
showing:`, `[subtitle-debug] ...`, `[display-debug] ...`,
`[subtitles-pref] ...`)는 전부 억제된다 - 이 로그들은 `LogLine`/`LogLinef`
대신 `LogDebugLine`/`LogDebugLinef`(내부에서 `g_config.debug`를 체크하고
바로 반환)를 거치도록 바뀌어 있다. 자막 디코딩/타이밍 버그를 조사할 땐
ini에서 `Debug=yes`로 바꾸고 게임을 재시작해야 아래 로그 표의 해당
줄들이 다시 채워진다.

---

## 디버깅 도구

이 프로젝트를 개발/수정하면서 실제로 썼던 디버깅 수단을 모았다. 각
도구가 정확히 뭘 확인해주는지, 언제 어떤 걸 써야 하는지 정리한다.

### 런타임 로그 — `KoreanSubtitleOverlay.log`

**위치**: `.asi`와 같은 폴더 (`modloader/_BASIC/KoreanSubtitleOverlay/
KoreanSubtitleOverlay.log`). 게임을 실행하면 자동 생성/이어쓰기된다.
구현은 `src/dllmain.cpp`의 `LogLine()`/`LogLinef()` — CRT iostream을
안 쓰고 `CreateFileA`/`WriteFile`을 직접 호출하는 이유는 코드 상단
주석 참고(DllMain에서 스레드로 띄운 뒤 `std::ofstream`을 쓰면 파일이
아예 안 만들어지는 문제가 있었음).

**로그 프리픽스별 의미**:

| 프리픽스 | 찍히는 시점 | 확인할 것 | Debug 필요 여부 |
|---|---|---|---|
| `=== DllMain: ... ===` | DLL 로드/언로드 | 플러그인이 아예 로드됐는지 | 항상 |
| `[init] ...` | 후킹 초기화 단계 (`InitThread`, `HookD3D9`, `EnsureImGuiInit`) | MinHook 초기화, vtable 주소, `MH_CreateHook`/`MH_EnableHook` 결과 코드 - 여기서 실패하면 오버레이 자체가 절대 안 뜬다 | 항상 |
| `[config] path=... FontSize=... OffsetX=... OffsetY=... Debug=...` | `LoadConfig()`, 초기화 초반 1회 | ini 파일을 실제로 어느 경로에서 읽었는지, 각 값이 뭘로 최종 확정됐는지 - 설정이 안 먹히는 것 같을 때 가장 먼저 볼 것 | 항상 |
| `[error] ...` | 초기화 실패 지점 | `Direct3DCreate9`/`CreateDevice` 실패, vtable 못 읽음 등 | 항상 |
| `[warn] ...` | 폰트 폴백 | `malgunbd.ttf` 못 찾아서 `malgun.ttf`로, 그마저 없어서 기본 폰트로 폴백했는지 | 항상 |
| `[subtitle] raw bytes: ...` | 새 자막 문자열이 감지될 때마다 1회 | 자모 디코더에 실제로 들어간 원본 바이트 시퀀스(헥스) - 매핑 안 된 바이트를 의심할 때 여기서 원본을 봐야 함 | `Debug=yes` |
| `[subtitle] now showing: ...` | 디코딩된 문자열이 바뀔 때마다 | 최종적으로 화면에 그려지는 UTF-8 결과 | `Debug=yes` |
| `[subtitle-debug] selection changed, active slots: ...` | `PickCurrentMessage()`의 선택 결과가 바뀔 때마다 | `BIGMessages`/`BriefMessages` 15개 슬롯 중 지금 뭐가 활성인지, 그중 어떤 게 선택됐는지(`<-- chosen`) - 대사가 잘못 골라지거나 옛 대사가 다시 뜨는 버그 조사용 | `Debug=yes` |
| `[display-debug] native draw SUPPRESSED/ALLOWED ...` | `hkDisplay`의 억제 여부가 뒤바뀔 때마다 | 원본(깨진) 자막 그리기가 언제 막히고 언제 풀리는지, `until`/`now` 틱값 - 대사 전환 시 겹침/깜빡임 버그는 대부분 여기서 원인이 드러남 | `Debug=yes` |
| `[subtitles-pref] current value at 0xBA678C: ...` | 최초 1회 | `PrefsShowSubtitles` 강제 on 처리 직전 원래 값 | `Debug=yes` |

**변경 시 찍히는 것만 로깅**: 위 상당수는 "매 프레임"이 아니라 "값이
바뀔 때만" 찍히도록 되어 있다(그렇지 않으면 로그가 순식간에 수백MB로
불어남). 새로운 디버그 로그를 추가할 땐 이 패턴(정적 변수로 이전 상태
기억, 바뀔 때만 출력)을 따르는 게 좋다. 새 진단 로그를 추가할 땐
`LogLine`/`LogLinef`가 아니라 `LogDebugLine`/`LogDebugLinef`를 써서
`Debug=no`가 기본값인 일반 사용자 로그를 계속 작게 유지할 것 - "항상
켜져 있어야 하는 것"(초기화/에러/경고/설정값)만 `Debug` 여부와 무관하게
남도록 이미 나눠뒀다(위 표의 "Debug 필요 여부" 열 참고).

**타이밍 버그는 로그만으로 부족할 수 있다**: "언제, 어떤 순서로
그려지는가" 계열 버그(자막 전환 시 겹침/깜빡임 등)는 로그 타임스탬프
(`GetTickCount()` 기반)만으로 재구성이 안 될 때가 있다. 이럴 땐 아래
["타이밍 버그" 절](#타이밍-버그-화면-녹화--ffmpeg-프레임-대조)의 화면
녹화 + ffmpeg 프레임 대조 절차를 쓴다.

### 오프라인 전수 검증 — `tools/gxt_scan.cpp`

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

### 글리프 직접 확인 — `FontExtract`

**위치**: `../../FontExtract` (이 저장소 밖, 옆 디렉터리 - 별도
프로젝트). `libtxd`(TXD 읽기/쓰기 라이브러리)는 서드파티 오픈소스
[txdedit](https://github.com/vaibhavpandeyvpz/txdedit)를 로컬에 clone해
가져다 쓴다(자세한 배경은 [WORK_LOG.md](WORK_LOG.md#의존성) 참고).

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
[WORK_LOG.md의 "막혀있던 부분"](WORK_LOG.md#막혀있던-부분-dxt5-미지원-버그)
참고. 이 수정은 아직 upstream에 반영 안 된 로컬 패치 상태다.

### 타이밍 버그: 화면 녹화 + ffmpeg 프레임 대조

**목적**: "언제, 어떤 순서로 그려지는가" 계열 버그(자막 전환 시 겹침/
깜빡임 등)는 바이트 매핑 검증만으로는 못 잡는다. 실제 플레이 중
타이밍이 문제이기 때문에, 위 런타임 로그만으로 재구성이 안 될 때 쓰는
절차다.

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

### 어떤 문제엔 뭘 써야 하는지

| 상황 | 먼저 쓸 도구 |
|---|---|
| 특정 대사가 깨져 나온다 (매핑 안 된 바이트 의심) | `KoreanSubtitleOverlay.log`의 `[subtitle] raw bytes:` 로 원본 바이트 확인 → `gxt_scan.cpp`로 같은 바이트가 다른 문장에서도 나오는지 전수 검사 |
| 새로 발견한 바이트가 어느 자모인지 확신이 안 선다 | `FontExtract`로 그 바이트의 글리프 셀을 뽑아서 이미 확정된 자모들과 모양 비교 |
| 대사 전환 시 겹치거나 잘려 보인다 | `KoreanSubtitleOverlay.log`의 `[display-debug]`/`[subtitle-debug]` 타임스탬프 대조, 재현 안 되면 [타이밍 버그 절](#타이밍-버그-화면-녹화--ffmpeg-프레임-대조)의 화면 녹화 + ffmpeg 프레임 대조 |
| 오버레이가 아예 안 뜬다 | `KoreanSubtitleOverlay.log`의 `[init]`/`[error]` 라인부터 확인 - MinHook 초기화나 `MH_CreateHook`/`MH_EnableHook` 결과 코드가 실패인지부터 봄 |
| `fonts.txd` 안 텍스처 구조/이름/해상도를 확인하고 싶다 | `FontExtract` 콘솔 출력(`texture count`, 텍스처별 이름/크기/압축 방식) |
