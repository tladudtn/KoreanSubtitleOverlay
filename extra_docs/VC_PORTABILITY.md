# GTA Vice City 포팅 가능성 조사

**질문**: GTA SA에서 잘 동작하는 이 오버레이를 GTA Vice City(VC)에도 그대로/비슷하게
적용할 수 있는가?

**결론 요약**: 원본 파일을 웹에서는 못 구했지만, 이후 사용자가 실제
`Vice_City_Korean_Patch_Final_Version.rar`(2005, 서로 다른 두 미러 소스,
바이트 동일)를 구해줘서 직접 열어봤다. **VC 한글패치도 SA와 같은
"자모조각을 겹쳐 그리는" 트릭을 쓴다** - `fonts.txd` 컨테이너 구조(텍스처
2개, `font1`/`font2`, 512×512, DXT3)까지 SA와 완전히 동일하다. 다만
**정확한 바이트 매핑(어느 값이 어느 자모인지)은 SA와 다르고**, 결정적으로
**`.gxt` 문자열 자체가 UTF-16LE(와이드 문자)로 저장돼 있다** - 이전
조사에서 plugin-sdk 기준 "VC는 `wchar_t*` 파이프라인"이라고 봤던 게 파일
레벨에서도 확인된 셈이다. 즉 이 프로젝트의 **방법론(자모조각 이론, 검증
도구, 후킹 구조)은 그대로 재사용 가능**하지만, **바이트 매핑 테이블과
자막 읽기 코드(싱글바이트 → UTF-16LE)는 처음부터 다시 만들어야 한다** -
"복사해서 오프셋만 바꾸면 되는" 수준이 아니라 SA 때와 비슷한 분량의
새 리버스 엔지니어링 작업이다.

## 1. VC 한글패치 원본 파일 - 웹 조사 (실패) → 사용자 제공 (성공)

**1차 시도(웹 조사)**: 이 세션에서 WebSearch/WebFetch/브라우저/curl로
직접 찾아본 결과는 전부 실패했다. 시도한 경로:

| 경로 | 결과 |
|---|---|
| [게임세상(gamess.co.kr) 게시글](https://gamess.co.kr/pds_board_view.php?no=14154) | 페이지 본문이 비어있음(로그인/세션 필요로 추정) |
| [playwares.com 게시글](http://playwares.com/gametalk/20550677) | 접속 자체가 거부됨(`ECONNREFUSED`) - 사이트 다운으로 추정 |
| [Daum 카페 "GTA 커뮤니티"](https://cafe.daum.net/GTA3mods/16Vv/44) | 브라우저로 직접 접속 성공, 다운로드 버튼은 있으나 `javascript:` 트리거라 실제 URL이 안 잡힘 |
| [blog.daum.net 미러 게시글](http://blog.daum.net/_blog/BlogTypeView.do?articleno=67&blogid=0T725) | 도메인 자체가 더 이상 존재하지 않음(`ENOTFOUND`) |
| Wayback Machine (`web.archive.org` CDX API) | `gtacommunity.cafe24.com` 스냅샷은 있지만 실제 첨부파일(`/zeroboard/file/*`)로는 4개만 크롤링됐고 한글패치는 없음 |
| [libertycity.net](https://libertycity.net/files/gta-vice-city/various-files/localizations/) | 검색 UI 응답 없음, 접근 실패(403) |
| [bcpark.net 게시글](http://www.bcpark.net/bbs/147358) | 파일은 있으나(`ViceCity_Patch_11.exe`, 2003년 5월) 시기상 락스타 공식 v1.1 패치로 추정, 한글패치 아님 |
| [나무위키 SA 한국어 패치 문서](https://namu.wiki/w/Grand%20Theft%20Auto:%20San%20Andreas/%ED%95%9C%EA%B5%AD%EC%96%B4%20%ED%8C%A8%EC%B9%98) | GTA3/VC 패치가 SA보다 번역 품질이 낫다는 언급만 있고 기술적 설명은 없음 |

**2차: 사용자가 파일 확보**. `Vice_City_Korean_Patch_Final_Version.rar`와
`vice_city_korean_patch_final_version-kojei80.rar`(다른 업로더의 재배포본)
두 개를 다운로드 폴더에서 받아 전달받음 - 두 파일 MD5가 완전히 동일해서
같은 원본의 서로 다른 미러 사본임을 확인.

**압축 해제 결과** (`Vice_City_Korean_Patch_Final_Version/`):
```
gta-vc.exe          3,088,896 bytes  (NO-CD 크랙 포함 실행파일 - 분석 대상 아님)
models/fonts.txd       524,584 bytes
TEXT/american.gxt      568,452 bytes
TEXT/french.gxt         421,946 bytes
ReadMe.txt                4,886 bytes  (UTF-16LE 텍스트)
```

`ReadMe.txt`(UTF-16LE로 재인코딩해서 읽음)로 출처 확인:
> 한글화 by 문을열면 in GTA 커뮤니티 gtacommunity.cafe24.com
> Release date 2005.9.4 / 한글화 완료율 : 100%

이전 웹 조사에서 찾은 정보(제작자, 사이트, 날짜, 완료율)와 정확히
일치 - 진짜 원본 파일이 맞다.

## 2. `fonts.txd` 구조 비교 - 컨테이너는 동일, 내용물은 다름

`FontExtract`로 VC의 `fonts.txd`를 열어본 결과:

```
texture count: 2
[0] name=font2 mask=font2m 512x512 hasAlpha=1 compression=3 (DXT3)
[1] name=font1 mask=font1m 512x512 hasAlpha=1 compression=3 (DXT3)
```

SA Korean_Patch의 `fonts.txd`(텍스처 2개, `font1`/`font2`, 512×512,
DXT3)와 **텍스처 이름·개수·해상도·압축방식까지 전부 동일**. 우연이라고
보기 어렵고, 최소한 같은 제작 도구/파이프라인을 썼을 가능성이 높다.

다만 실제 내용은 다르다:

- **`font1`(자모 텍스처)**: SA와 마찬가지로 초성/중성/종성 낱자 글리프가
  격자에 박혀있는 건 같다. 하지만 **배치 순서가 다르다** - SA는 19개
  초성을 겹모음까지 섞어서(`ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ` 순)
  0x80~0x92에 배치한 반면, VC는 **기본 14개 초성을 표준 순서로
  (`ㄱㄴㄷㄹㅁㅂㅅㅇㅈㅊㅋㅌㅍㅎ`) 먼저 깔고 쌍자음(`ㄲㅆ`)을 뒤에** 붙인
  것으로 보인다(0x80~0x8D 기본형, 0x8E~0x8F 쌍자음 추정 - 아래 3번 절
  참고). 즉 "SA의 표를 그대로 오프셋만 밀어서 쓰면 된다"는 가정은 틀렸다.
- **`font2`**: SA는 고딕체(WASTED/BUSTED용) + 일반 서체(HUD용) 조합이었는데,
  VC의 `font2`는 **필기체(cursive) 스타일의 장식용 라틴 폰트**로, 한글
  글리프가 전혀 없다. 즉 SA와 달리 VC는 "font1 하나에 라틴+한글 자모가
  다 들어있고, font2는 완전히 별개 용도"인 구조로 보인다 - font1/font2의
  **역할 분담 자체가 SA와 다르다**.

## 3. `.gxt` 문자열이 UTF-16LE라는 결정적 차이

`american.gxt`의 `TDAT`(실제 문자열 블록) 시작 부분을 헥스로 직접 열어봤다:

```
00007690: 5444 4154 f011 0300 8800 9200 a700 5e00  TDAT..........^.
000076a0: 8d00 9400 9000 5b00 5e00 8200 9400 af00  ......[.^.......
000076b0: 0000 4100 6400 6d00 6900 7200 6100 6c00  ..A.d.m.i.r.a.l.
000076c0: 0000 4100 6d00 6200 7500 6c00 6100 6e00  ..A.m.b.u.l.a.n.
```

`41 00 64 00 6d 00 69 00 72 00 61 00 6c 00` = `A.d.m.i.r.a.l.`(각 글자
뒤에 널바이트) - **UTF-16LE로 인코딩된 "Admiral"**(차량 이름)이다. GXT의
`TABL`/`TKEY`/`TDAT` 청크 구조 자체는 SA와 같지만(자세한 파싱 규칙은
[WORK_LOG.md의 "검증 방법론"](../docs/WORK_LOG.md#검증-방법론) 참고),
**문자열 데이터 자체가 SA는 싱글바이트, VC는 와이드 문자**라는 게 확정됐다
- 이건 이전 조사에서 plugin-sdk 기준으로 "VC의 `CMessages`/`CFont`가
`wchar_t*` API"라고 추론했던 게 실제 게임 데이터 파일 레벨에서도 그대로
확인된 것이다.

바로 그 앞(TDAT 시작 직후 첫 문자열)이 한글 인코딩 문자열이다:

```
88 00 92 00 a7 00 5e 00 8d 00 94 00 90 00 5b 00 5e 00 82 00 94 00 af 00 00 00
```

16비트 단위로 읽으면(항상 상위 바이트가 `00`) 값 자체는
`88 92 a7 5e 8d 94 90 5b 5e 82 94 af`(뒤에 `00`으로 종료) - **SA와 똑같이
`0x80`대/`0x90`대/`0xA0`대에 낮은 바이트 값이 몰려있는 패턴**이다. 즉
**"UTF-16LE 컨테이너 안에, 낮은 바이트만 0x80~0xFF 범위의 자모조각
코드로 쓰고 상위 바이트는 항상 0으로 채우는" 방식** - SA의 싱글바이트
자모조각 트릭을 와이드 문자 컨테이너 안에 그대로 욱여넣은 형태로 보인다.
아마 `CFont`의 글리프 조회 테이블 자체가 여전히 0~255 범위(텍스처 격자
16×16=256칸)로 고정돼 있어서, 문자열 컨테이너가 넓어져도 실제 매핑 가능한
값 종류는 SA와 똑같이 256개로 제한된 것으로 보인다.

**단, 값 배치 자체는 SA와 다르다.** 위 첫 글자 `0x88`을 "0x80부터 기본
14자음 표준순" 가설로 풀면 인덱스 8 = `ㅈ`이 되는데, SA 표로 풀면 다른
자모가 나온다(SA 0x88은 초성 배열 인덱스 8 = 겹자음 위치). 3번 절에서 본
`font1` 격자 배치 순서 차이와 일치하는 결과다.

## 4. plugin-sdk SA vs VC 비교

[DK22Pac/plugin-sdk](https://github.com/DK22Pac/plugin-sdk)는 SA/VC/III를 모두
지원하지만, 두 게임 지원 수준과 구조 자체가 다르다.

### 4.1. `CMessages` - 가장 중요한 차이

| | SA | VC |
|---|---|---|
| 문자열 타입 | `char*` (싱글바이트) | `wchar_t*` (와이드 문자) - **위 3번 절에서 실제 파일로도 확인됨** |
| `tMessage` 구조체 | 전부 공개됨, 필드 오프셋까지 `VALIDATE_OFFSET`로 검증됨 (`m_pText` 0x0, `m_dwNumber[6]` 0x10 등) | 구조체 자체가 plugin-sdk에 없음 - 함수 시그니처만 있음 |
| `BIGMessages`/`BriefMessages` 배열 주소 | 공개됨 (`0xC1A970`, `0xC1A7F0`) | plugin-sdk에 없음 |
| `Display()` 시그니처 | `Display(bool flag)` | `Display()` - 인자 없음 |

이 프로젝트(`src/dllmain.cpp`)의 핵심 트릭 두 가지 - ① `tMessage::m_pText`를
바이트 단위로 직접 읽어서 자모 디코딩, ② `hkDisplay(bool)`로 원본 그리기를
조건부 억제 - 둘 다 SA가 **싱글바이트 문자열 + `bool` 인자가 있는 `Display`**
라는 전제 위에 만들어져 있다. VC는 이 전제 자체가 plugin-sdk 기준으로는
성립하지 않고, 위 실제 파일 분석으로 그 전제(와이드 문자)가 맞다는 것도
확인됐다.

### 4.2. `CFont` - 같은 패턴이 한 번 더 확인됨

| | SA | VC |
|---|---|---|
| 문자 단위 API | `PrintChar(float x, float y, char character)`, `GetNextSpace(char*)` 등 - `char` | `GetStringWidth(const wchar_t*)`, `GetTextRect(..., const wchar_t*)` 등 - `wchar_t` |

### 4.3. `CMenuManager` - 이건 비슷하게 이식 가능

| | SA | VC |
|---|---|---|
| `m_bMenuActive` 오프셋 | `0x5C` | `0x38` |
| plugin-sdk 검증 상태 | `VALIDATE_OFFSET`로 검증됨 | `VALIDATE_OFFSET`로 검증됨 |

SA/VC 둘 다 구조체가 잘 정리돼 있어서, 오프셋만 바꾸면 이식 가능한
부분이다.

### 4.4. D3D9 후킹 부분 - 영향 없음

`DEVELOPER_GUIDE.md`에 정리된 더미 디바이스로 `EndScene`/`Reset` vtable을
읽어서 후킹하는 방식은 GTA 엔진과 무관한 범용 D3D9 기법이라 VC에도 그대로
쓸 수 있다. ImGui 초기화/렌더링 부분도 마찬가지.

## 5. 결론 및 다음 단계

**VC도 SA와 같은 부류의 문제를 갖고 있다** (자모조각을 겹쳐서 완성형처럼
흉내내는 방식 → 정렬이 안 맞아 기울어져 보임). 이 프로젝트가 채택한
해법(원본 바이트 직접 읽기 → 올바르게 재조합 → 시스템 폰트로 오버레이
렌더링)이 VC에도 개념적으로 그대로 적용 가능하다는 뜻이다. 이전 조사에서
"VC는 와이드 문자라 이 문제 자체가 없을 수도 있다"고 봤던 가설은 **틀렸다**
- wchar_t든 char든, `CFont`의 실제 글리프 조회 범위가 여전히 256개로
제한돼 있어서 자모조각 트릭이 그대로 필요했던 것으로 보인다.

**재사용 가능한 것**:
- 방법론 전체 - FontExtract로 격자 확인, 실제 문장과 대조해 바이트 매핑
  확정, 오프라인 전수 스캔으로 검증(단 이번엔 GXT TDAT를 UTF-16LE로
  읽어야 함)
- D3D9 후킹 구조, ImGui 오버레이 렌더링
- `CMenuManager` 기반 ESC 메뉴 숨기기(오프셋만 교체)

**새로 해야 하는 것**:
- **바이트 매핑 테이블을 처음부터 재작성** - SA 테이블 재사용 불가,
  `font1` 격자 이미지 대조 + 실제 대사 문자열 대조로 처음부터 확정해야 함
  (다만 "초성/중성/종성 조각을 값 범위로 나눠 배치하고 겹쳐 그린다"는
  전체 이론적 틀은 SA와 같아서, 완전히 새로운 접근을 고안할 필요는 없어
  보인다)
- **자막 읽기 로직을 `char*` → UTF-16LE `wchar_t*` 기반으로 재작성** -
  `DecodeJamoBytes()`가 1바이트씩 읽는 지금 구조 대신, 2바이트(1
  code unit)씩 읽고 상위 바이트가 0인지 확인하는 방식으로 바꿔야 함
- **`tMessage`/`BIGMessages`/`BriefMessages` 동급 구조체와 주소를 VC용으로
  직접 리버스 엔지니어링** (plugin-sdk에 없음 - Cheat Engine 등 필요)
- **`hkDisplay` 후킹 시그니처를 `void(bool)` → `void()`로 변경**

즉 "SA 프로젝트를 복사해서 주소만 바꾸는" 수준이 아니라, **SA 때 했던
조사·검증 과정을 VC용으로 거의 그대로 다시 밟아야 하는 별도 프로젝트**에
가깝다. 다만 이번 조사로 "그럴 가치가 있는 문제인지"(그렇다 - VC도
자모조각 트릭 때문에 기울어져 보이는 게 맞다)와 "어떤 새 도구가 몇 개나
필요한지"(바이트 매핑 재작업 + UTF-16LE 파싱 + VC 전용 주소 리버싱, 이
세 가지)는 명확해졌다.

## 참고 링크

- [DK22Pac/plugin-sdk](https://github.com/DK22Pac/plugin-sdk)
- [plugin_vc/game_vc/CMessages.h](https://github.com/DK22Pac/plugin-sdk/blob/master/plugin_vc/game_vc/CMessages.h)
- [plugin_sa/game_sa/CMessages.h](https://github.com/DK22Pac/plugin-sdk/blob/master/plugin_sa/game_sa/CMessages.h)
- [plugin_vc/game_vc/CFont.h](https://github.com/DK22Pac/plugin-sdk/blob/master/plugin_vc/game_vc/CFont.h)
- [Daum 카페 "GTA 커뮤니티" - 바이스시티 한글패치 최종버전](https://cafe.daum.net/GTA3mods/16Vv/44)
- [게임세상 - 한글패치 (100% 완전 해결)](https://gamess.co.kr/pds_board_view.php?no=14154)
- [나무위키 - GTA SA 한국어 패치](https://namu.wiki/w/Grand%20Theft%20Auto:%20San%20Andreas/%ED%95%9C%EA%B5%AD%EC%96%B4%20%ED%8C%A8%EC%B9%98)
