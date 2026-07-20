# 한글패치 + 고해상도 UI 폰트 동시 적용 (fonts.txd 병합)

이 문서는 이 프로젝트(자막 오버레이)와는 별개로, GTA SA의 `fonts.txd`를
직접 편집해서 한글패치와 HD(고해상도) UI 폰트 모드를 동시에 적용한 방법을
기록한다. WASTED/BUSTED/MISSION PASSED 배너, 화면 좌하단 지역명·차량명
표시가 이 대상이다(자막 텍스트는 이 프로젝트의 오버레이가 별도로 처리하며
`fonts.txd`와 무관함).

## 문제 배경

`models/fonts.txd`는 통짜 파일 하나다. 한글패치와 HD 폰트 모드(예:
"Interface HD GTASA/Fontes")가 둘 다 이 파일 전체를 자기 것으로 교체하려고
한다. modloader는 나중에 스캔되는 폴더가 이기고 앞의 것을 완전히 덮어쓰기
때문에, 우선순위를 어떻게 조정해도 둘 중 하나는 반드시 죽는다. 실제로
이 세션 시작 시점엔 한글패치가 항상 나중에 로드돼서, 로컬에 이미 설치돼
있던 HD 폰트 모드가 한 번도 적용된 적이 없었다(`modloader.log`에
`Uninstalling ... Interface HD GTASA ... fonts.txd` →
`Installing ... Korean_Patch ... fonts.txd` 로 확인됨).

## 돌파구: 텍스처 단위 분리

자체 제작한 TXD 파서 도구(`../../FontExtract`, `../../txdedit/libtxd`
기반)로 `fonts.txd`를 열어보니 안에 독립된 네이티브 텍스처가 정확히
2개뿐이었다:

- **`font1`** (512×512, DXT3): 한글 자모 글리프. 한글패치가 전담 교체하는
  부분.
- **`font2`** (512×512, DXT3): WASTED/BUSTED/MISSION PASSED 등에 쓰이는
  고딕(블랙레터) 서체 + 화면 하단 지역명·차량명에 쓰이는 일반 서체, 두
  스타일이 한 텍스처 안에 상단/하단으로 같이 들어있음.

`font2`를 한글패치본과 바닐라본에서 픽셀 단위(알파 채널 PNG 해시)로
비교한 결과 **완전히 동일**했다 — 즉 한글패치는 `font1`만 건드리고
`font2`는 손도 안 댔다. 반면 로컬에 있던 HD 폰트 모드의 `fonts.txd`는
같은 두 텍스처를 1024×1024로 업스케일한 것이었다(파일 크기가 정확히
4배).

이걸로 문제가 "둘 중 하나를 고르는 문제"에서 **"텍스처 단위로 필요한
조각만 서로 교환해서 새 파일을 만드는 문제"**로 바뀌었다: 한글패치의
`font1` + HD 모드의 `font2`를 합치면 둘 다 잃는 것 없이 가질 수 있다.

## 막혀있던 부분: DXT5 미지원 버그

HD 모드 쪽 `fonts.txd`를 파서로 열려고 하면 크래시가 났다. 원인을 헥스
덤프로 직접 추적한 결과, HD 텍스처가 `DXT5` 압축(FourCC `"DXT5"`,
`depth=16`, 1024×1024 텍스처당 1MB)인데, 우리 `libtxd` 라이브러리
(`../../txdedit/libtxd`)의 `Compression` enum이 `NONE/DXT1/DXT3`만
정의하고 있어서 DXT5를 인식 못 했다. 인식 못 한 압축은 조용히
`Compression::NONE`으로 남는데, 이후 `convertToRGBA8()`이 이걸 "비압축
원본 픽셀"로 착각해서 실제 버퍼(1MB)보다 훨씬 큰 범위(가정상 4MB)를
읽으려다 힙 버퍼 오버런으로 세그폴트가 난 것이었다.

수정한 파일(라이브러리 저장소는 `../../txdedit`, 그 서브셋을 이
프로젝트 밖에서 별도로 빌드해 쓰는 `../../FontExtract`가 참조함):

- `txd_types.h`: `Compression` enum에 `DXT5 = 5` 추가
- `txd_texture.cpp`: FourCC 파싱(`readD3DStruct`)과 기록(`writeD3DStruct`)
  양쪽에 `'5'` 케이스 추가
- `txd_converter.cpp`: `convertToRGBA8`/`canConvert`/`decompressDXT`에
  DXT5 케이스 추가(`squish::kDxt5`), `convertDXT5()` 헬퍼 신설

이 수정은 미리보기/디코드 경로에만 필요했다 — 실제 병합 자체는 압축된
바이트를 그대로 복사하기 때문에 디코더가 없어도 동작은 하지만, HD
텍스처가 진짜 기대한 내용인지 눈으로 확인하려면 디코드가 돼야 했다.

## 병합 도구

`FontExtract/merge_fonts.cpp` (같은 저장소의 `libtxd`를 라이브러리로
사용, 빌드는 `FontExtract/build_merge.bat` 참고):

```
merge_fonts <korean fonts.txd> <hd fonts.txd> <font1 소스: korean|hd> <출력 fonts.txd>
```

동작 방식:
1. 두 `fonts.txd`를 각각 로드
2. 한글패치 쪽에서 `font1`, HD 모드 쪽에서 `font2`를 찾아
   `std::move`로 그대로 가져옴 (raw compressed 바이트 그대로, 디코드/
   재압축 없음 → 무손실)
3. 새 `TextureDictionary`에 두 텍스처를 담아 `save()`

저장 후 다시 로드해서 원본 두 텍스처와 알파 채널 PNG 해시가 완전히
일치하는지 검증했다(픽셀 단위 무손실 확인).

## `fonts.dat`는 손댈 필요 없음

`data/fonts.dat`는 바이너리가 아니라 평범한 텍스트 파일이고,
`[FONT_ID] 0`(GOTHIC), `[FONT_ID] 1`(GTA HEADER) 두 폰트에 대한 글자별
**상대 폭(proportional width)** 값만 담고 있다 (둘 다 `font2` 텍스처
안의 스타일이고, `font1`/한글 자모에 대한 폭 정보는 애초에 이 파일에
없다 - 한글 렌더링은 이 폭 테이블을 거치지 않는 별도 경로로 보임). 어느
칸이 어느 글자인지는 "16열 격자, 코드값 = 행×16+열"이라는 엔진 고정
규칙으로 정해지고 이건 정규화(0~1) UV라 텍스처 해상도와 무관하다. 즉
`font2`를 512→1024로 키워도 `fonts.dat` 값은 그대로 유효하다 - 한글패치
원본 `fonts.dat`를 그대로 재사용하면 된다.

## 설치 (modloader 우선순위 트릭)

`modloader/_BASIC/zzz_HD_Font_Merge (Korean Compatible)/`에
`models/fonts.txd`(병합본), `data/fonts.dat`(한글패치 원본 그대로)를
넣었다. modloader는 기본적으로 나중에 스캔되는 폴더가 충돌을 이기는데,
폴더명을 알파벳상 `Korean_Patch`보다 뒤로("zzz_" 접두사) 둬서 이 병합본이
항상 최종 승자가 되도록 했다.

## 의존성

- **[txdedit](https://github.com/vaibhavpandeyvpz/txdedit)** (서드파티
  오픈소스 TXD 편집기) - 이 저장소의 `libtxd/`가 `fonts.txd` 읽기/쓰기의
  실제 구현체. DXT5 미지원 버그를 로컬 클론에서 직접 고쳐서 썼다(위
  "막혀있던 부분" 참고). 이 프로젝트(KoreanSubtitleOverlay)에는
  포함되어 있지 않고, `FontExtract`/`merge_fonts` 도구가 옆 디렉터리의
  클론을 라이브러리 소스로 직접 컴파일해 쓰는 구조다. 수정 사항은 아직
  upstream에 반영/제안하지 않은 로컬 패치 상태.

## 관련 파일 위치

```
GTA-SA-Dev/
├── FontExtract/              fonts.txd 글리프 추출 + merge_fonts 도구
│   ├── main.cpp               텍스처 -> PNG 추출(디버깅/검증용)
│   ├── merge_fonts.cpp        font1+font2 병합 도구
│   ├── build.bat, build_merge.bat
│   └── output/                검증용 추출 결과(로컬, git 미포함)
└── txdedit/                   txdedit clone (위 "의존성" 참고)
    └── libtxd/                실제 읽기/쓰기 라이브러리 - DXT5 수정을 여기 적용함
```

게임 쪽 설치 결과물: `modloader/_BASIC/zzz_HD_Font_Merge (Korean Compatible)/`
(GTA SA 게임 폴더의 별도 git 저장소, `.gitignore`에 추적 등록됨).
