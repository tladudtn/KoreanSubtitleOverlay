# 후킹 구조 / 오버레이 렌더링 구조

이 문서는 "어떤 함수를 어떻게 후킹했고, 오버레이를 화면에 어떻게
띄웠는지"를 다룬다. 자모 바이트를 유니코드로 디코딩하는 로직 자체는
[DECODING_NOTES.md](DECODING_NOTES.md)를, 자막 선택/네이티브 억제 타이밍
버그(대사 전환 시 겹침/잘림)에 대한 별도 상세 근거는 그 문서의 "오버레이
렌더링 로직" 절을 참고. 여기서는 전체 구조를 처음부터 정리한다. 코드
위치는 전부 `src/dllmain.cpp` 기준.

## 전체 그림

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

## 1. 후킹 대상 3곳과 그 이유

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

## 2. EndScene 주소를 얻는 방법: 더미 디바이스

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

## 3. ImGui는 첫 EndScene 호출 시점에 지연 초기화

DLL 로드 직후엔 아직 진짜 디바이스 포인터가 없다. 그래서 `ImGui::
CreateContext()`/`ImGui_ImplDX9_Init()`는 `hkEndScene`이 처음 호출될
때, 그 인자로 넘어온 진짜 `device`를 가지고 딱 한 번만 실행한다
(`EnsureImGuiInit`, `g_ImGuiReady` 플래그로 가드). 이때 한글 폰트도
같이 로드한다(`LoadKoreanFont`): `C:\Windows\Fonts\malgunbd.ttf`(맑은
고딕 볼드)를 우선 시도하고, 없으면 `malgun.ttf` → 그마저 없으면 ImGui
기본 폰트(한글 미지원)로 순차 폴백.

## 4. 자막 데이터는 별도 후킹 없이 게임 메모리를 직접 읽음

`CMessages::AddMessage`류를 후킹하는 방식이 아니다. 대신 게임이 이미
채워놓은 전역 배열 `BIGMessages`(스타일 7종) / `BriefMessages`(슬롯
8개) - `sa_messages.h`에서 실제 게임 메모리 주소로 매핑된 구조체 -
를 **매 프레임 직접 읽는다**. `PickCurrentMessage()`가 이 15개 슬롯
중 텍스트가 있고 한글 바이트를 포함하며(`ContainsHighByte`) 가장 최근에
시작된(`m_dwStartTime` 최댓값) 것 하나를 고른다. `DrawDialogueSubtitles()`
가 그 텍스트를 `DecodeToUtf8()`(자모 디코더, DECODING_NOTES.md 참고)로
변환해서 ImGui 창으로 그린다.

이 방식의 장점: `CMessages`가 메시지를 큐에 넣고 빼는 원본 로직
(`CMessages::Process` 등)은 전혀 건드리지 않고 그대로 돌아가게 둔 채,
"이미 결정된 결과"만 읽기만 하면 되므로 게임 로직에 개입할 필요가
없다.

## 5. 원본 그리기는 무조건이 아니라 조건부로만 막음

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
DECODING_NOTES.md의 "네이티브 억제 타이밍: `hkDisplay`" 절 참고.

## 6. 정리/해제

`DllMain`의 `DLL_PROCESS_DETACH`에서 `ImGui_ImplDX9_Shutdown()` +
`ImGui::DestroyContext()`, 그리고 `MH_DisableHook(MH_ALL_HOOKS)` +
`MH_Uninitialize()`로 정리한다.
