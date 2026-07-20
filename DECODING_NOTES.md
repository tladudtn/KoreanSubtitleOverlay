# 개발 참고 문서 (세션 간 컨텍스트용)

이 문서는 다른 세션(다른 대화)에서 이 프로젝트를 다시 열었을 때, 그동안의
조사/결정 과정을 처음부터 다시 하지 않도록 남기는 기술 참고 자료다.
사용자용 요약은 [README.md](README.md) 참고. 이 문서는 "왜 이렇게
매핑했는지"의 근거를 전부 담는다.

## 프로젝트 구조

```
C:\Users\SYS\Documents\GTA-SA-Dev\
├── KoreanSubtitleOverlay\   (이 프로젝트)
│   ├── src\dllmain.cpp      실제 플러그인 소스 (디코더 본체)
│   ├── src\sa_messages.h    게임 구조체/주소 (plugin-sdk 참조)
│   └── tools\gxt_scan.cpp   오프라인 전수 검증 도구 (아래 참고)
├── imgui\                   (사이드로드, 프로젝트에 포함 안 됨)
├── minhook\                 (사이드로드, 프로젝트에 포함 안 됨)
└── FontExtract\             fonts.txd -> 글리프 PNG 추출 도구 (별도 프로젝트)
```

게임 설치 경로: `C:\Program Files (x86)\Steam\steamapps\common\Grand Theft Auto San Andreas`
- 배포 위치: `modloader\_BASIC\KoreanSubtitleOverlay\KoreanSubtitleOverlay.asi`
- 원본 한글패치 에셋(참조용): `modloader\_BASIC\Korean_Patch\{american.gxt, fonts.dat, fonts.txd}`

## 문제 배경

이 한글패치의 `fonts.txd`는 진짜 텍스트 인코딩이 아니라 "자모조합형" 폰트다.
GXT 대사 문자열의 바이트가 각각 완성형 글자가 아니라 초성/중성/종성
"조각" 하나씩을 가리키고, 게임 렌더러가 2~4개를 겹쳐 그려서 하나의
한글 음절처럼 보이게 만드는 방식(옛날 조합형 콘솔 폰트와 동일한 트릭).
네이티브 렌더러가 이미 이 결합 방식을 잘못 처리해서 자막이 깨져 보인다.

이 플러그인은 `CMessages::BIGMessages`/`BriefMessages`에서 원본 바이트를
직접 읽어서 우리가 자체적으로 재조합 -> 유니코드 한글 음절로 변환한 뒤
ImGui로 그려서 네이티브 드로우를 대체한다.

## 바이트 레이아웃

- `0x00–0x7F`: 표준 ASCII 그대로 통과
- `0x80–0x92`: 초성(19개, ㄱ~ㅎ 표준 순서)
- `0x93–0x9C`: 중성(10개 기본 모음: A YA EO YEO O YO U YU EU I)
- `0x60`, `0x5D`: "종성 없음" 종결 바이트 (두 종류 다 씀, 이유 불명이나 상호
  교환 가능하게 처리)
- `0x9D`, `0x9E`, `0x9F`, `0xA1`–`0xAF`: 종성(받침) 바이트 - 순서가 초성/중성처럼
  단순 연속이 아니라서 실제 확인된 것만 표로 정리(아래)
- `0x5C`, `0x9C`: 겹모음 만들 때 쓰는 "+ㅣ" 보정 바이트(두 값이 같은 역할)

## 확정된 종성(받침) 매핑

전부 `american.gxt` 전수 스캔(아래 도구) + 다수 문장에서 자연스러운 실제
단어로 확인됨. `0xAE`만 텍스트 근거가 하나뿐이라 FontExtract 글리프
비교로 추가 검증함(아래 "글리프 검증" 참고).

| 바이트 | 자모 | 유니코드 | 근거 단어 |
|---|---|---|---|
| 0x3C | ㅌ | U+314C | 같이 (2회, ASCII `<` 슬롯 재사용) |
| 0x9D | ㄱ | U+3131 | 가속하려면, 죽기엔 (가장 흔한 종성, 압도적 빈도) |
| 0x9E | ㄲ | U+3132 | 밖에 ×3, 낚이던데 |
| 0x9F | ㄴ | U+3134 | 만 |
| 0xA1 | ㄶ | U+3135 | 않아, 않습니다 |
| 0xA2 | ㄷ | U+3137 | 받고, 싣는, 얻어, 믿는 |
| 0xA3 | ㄹ | U+3139 | 를, 살 |
| 0xA4 | ㄺ | U+313A | 닭대가리, 닭살 |
| 0xA5 | ㄻ | U+313E | 옮기나 |
| 0xA6 | ㄼ | U+313B | 밟기 |
| 0xA7 | ㅀ | U+3140 | 잃기, 잃었습니다, 싫으니 |
| 0xA8 | ㅁ | U+3141 | 이놈아, 임마, 좀 (**초기에 ㅇ으로 잘못 매핑했었음, 수정됨**) |
| 0xA9 | ㅂ | U+3142 | 십, 습 |
| 0xAA | ㅄ | U+3144 | 없다, 값 |
| 0xAB | ㅅ | U+3145 | 그것도, 것을, 거짓말쟁이, 멋쟁이 |
| 0xAC | ㅆ | U+3146 | 있 |
| 0xAD | ㅇ | U+3147 | 상황 (두 음절 다 이 종성 공유) |
| 0xAE | ㅈ | U+3148 | "좆같이도" 슬랭 + FontExtract 글리프 비교 (0xAB와 형태 비교, 위 획 하나 추가됨) |
| 0xAF | ㅊ | U+314A | 몇 (9회 이상), 쫓는 |

## 겹모음(compound vowel) 결합 규칙

두 가지 방식:
1. **두 모음 바이트 겹치기**: `O(0x97)+A(0x93)=WA`, `U(0x99)+EO(0x95)=WEO`
2. **"+ㅣ" 보정 바이트**: `A+i=AE`, `EO+i=E`, `EU+i=YI`, `O+i=OE`, `U+i=WI`,
   `YEO+i=YE`(미셸 확인) - 이때 "+i" 바이트는 `0x5C` **또는** `0x9C`(순수
   "이" 중성 바이트 재사용, "두려움의"/"뒤에서"로 확인, 발견 당시 가장 큰
   버그 원인이었음 - "의" 조사가 게임에서 워낙 흔해서)
3. **3중 겹침(트리플 스택)**: 이미 결합된 WA/WEO 뒤에 `0x5C`가 한 번 더
   오면 WA→WAE, WEO→WE로 업그레이드됨 ("됐어" = 됐 = ㄷ+WAE+ㅆ로 확인)

## 검증 방법론

### 1) 오프라인 GXT 전수 스캔 (`tools/gxt_scan.cpp`)

`dllmain.cpp`와 완전히 동일한 디코드 로직을 복제해서, 실제 게임을 켜지
않고 `american.gxt` 파일 자체를 파싱해 모든 대사 문자열(127개 테이블,
총 1775개 한글 인코딩 문자열)에 대해 디코딩을 돌려보는 진단 도구.
미매핑 바이트를 만나면 hex + 디코딩 결과를 리포트에 남긴다.

**GXT 파일 구조** (SA 표준): `TABL`(테이블명+오프셋 목록) → 각 테이블마다
`TKEY`(8바이트 엔트리: offset+hash) → `TDAT`(실제 null-terminated 문자열
블롭). TKEY 영역을 문자열로 착각해서 스캔하면 노이즈가 대량 발생하니
반드시 TDAT 영역만 스캔해야 함 (초기 버전에서 이 실수로 128개의 가짜
미매핑 바이트가 나왔었음).

빌드/실행법:
```
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cl.exe /EHsc /nologo /Fe:gxt_scan.exe tools\gxt_scan.cpp /link user32.lib

gxt_scan.exe "<게임경로>\modloader\_BASIC\Korean_Patch\american.gxt" report.txt
```
마지막 줄 요약에 `distinct unmapped byte values`가 0이면 전수 검증 통과.
**주의**: `finalJamoForByte`/`combineMedial` 테이블은 `dllmain.cpp`와
수동으로 동기화해야 함(자동 공유 안 됨) - `dllmain.cpp`를 고치면 이
파일도 같이 고칠 것.

### 2) FontExtract 글리프 직접 대조

`../FontExtract`가 `fonts.txd`의 각 32x32 글리프 셀을 `cell_XX.png`로
개별 추출해준다 (`output/1_font1_cells/cell_XX.png`, XX=hex 바이트).
**`font1`이 실제 자모 폰트**(font2는 다른 용도, 확인 안 함). 이미지를
직접 열어서(Read 툴로 - Claude는 이미지를 볼 수 있음) 이미 확정된
바이트의 글리프 모양과 비교하면 텍스트 근거가 약한 바이트도 검증 가능.
격자 전체 이미지(`_grid.png`, 빨간 격자선 포함)를 크롭해서 여러 바이트를
한 번에 비교하면 셀 경계 정렬도 같이 확인된다(받침 글리프는 셀 하단에
배치되는 게 정상이라 "잘린 것처럼" 보이는 건 버그가 아님).

## 세션 타임라인 요약

1. 겹모음 "와" 버그를 `combineMedial()`로 최초 수정
2. 실제 로그(`KoreanSubtitleOverlay.log`, 262줄) 수동 분석 -> "가<`이도",
   "사`화" 같은 스트레이 ASCII 버그 원인이 미매핑 종성이라는 걸 특정,
   0xA8을 ㅇ->ㅁ로 정정, 0x3C/0xAD 신규 확정
3. "됐어" 3중 겹모음 버그 -> `upgradeMedialWithI()` 추가
4. 게임을 직접 조작할 도구가 없어서(데스크톱 자동화/스크린샷 툴 없음),
   대신 `american.gxt` 전체를 오프라인으로 파싱하는 `gxt_scan.cpp`를
   작성해서 전수 검증하는 방식으로 전환 - 미션 하나 플레이하는 것보다
   훨씬 철저하게 검증됨
5. GXT 구조 파싱 실수(TKEY 영역 오염) 수정 후 재스캔 -> 0x9D=ㄱ,
   0x9E=ㄲ, 0xA1=ㄶ, 0xA2=ㄷ, 0xA4=ㄺ, 0xA5=ㄻ, 0xA6=ㄼ, 0xA7=ㅀ,
   0xAA=ㅄ, 0xAB=ㅅ, 0xAF=ㅊ 순차 확정, 0x9C "+i" 결합 발견(최대 버그
   원인), 미매핑 바이트 0개 달성
6. 0xAE(ㅈ)만 텍스트 근거가 하나뿐이라 FontExtract 글리프 비교로 추가
   확인 (0xAB=ㅅ과 형태 비교, 위 획 하나 차이)

## 남은 작업

- **인게임 실제 렌더링 스크린샷 검증** - 오프라인 디코딩/글리프 대조는
  끝났지만 실제 화면에 ImGui 오버레이가 정상적으로 그려지는지는 아직
  미확인. Claude에게는 게임 창을 조작/캡처할 도구가 없어서 사용자가
  직접 플레이하며 확인해야 함.
