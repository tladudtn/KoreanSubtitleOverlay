# GTA Vice City 포팅 가능성 조사

**질문**: GTA SA에서 잘 동작하는 이 오버레이를 GTA Vice City(VC)에도 그대로/비슷하게
적용할 수 있는가?

**결론 요약**: 아니오, 지금 갖고 있는 근거로는 "같은 구조로 작업"이라고 판단할
수 없다. VC용 한글패치 원본 파일은 광범위하게 찾아봤지만 확보하지 못했고,
plugin-sdk의 VC 쪽 `CMessages`/`CFont`는 SA와 달리 **`wchar_t*`(와이드 문자) 기반
API**라서, SA 한글패치가 쓰는 "싱글바이트 자모조각 오버랩" 트릭 자체가 VC에는
애초에 필요 없었을 가능성이 구조적으로 높다. 즉 파일을 못 구해서 확인을 못 한
게 아니라, 엔진 차원에서부터 이미 다른 종류의 문제일 가능성이 크다는 뜻이다.

## 1. VC 한글패치 원본 파일 조사

**목표**: 실제 파일(`fonts.txd`/`fonts.dat`/`.gxt`)을 받아서 SA 때처럼
`FontExtract`로 열어보고, SA와 같은 자모조각 텍스처인지 픽셀 단위로 직접
확인하는 것.

**찾은 것**: VC 한글패치가 실제로 존재한다는 사실 자체는 다수 소스로
확인됨 - 2005년 9월 4일 "Vice_City_Korean_Patch_Final_Version.rar",
100% 번역 완료, 제작자 "문을열면", `gtacommunity.cafe24.com`("GTA 커뮤니티")
사이트에서 배포. 이 사이트가 GTA3/VC/SA 세 게임 한글패치를 전부 만들었다는
정황도 있음 - 만약 그렇다면 SA 패치와 제작 방식이 겹칠 가능성이 있어서 더
확인해볼 가치가 있었음.

**시도한 경로와 결과**:

| 경로 | 결과 |
|---|---|
| [게임세상(gamess.co.kr) 게시글](https://gamess.co.kr/pds_board_view.php?no=14154) | 페이지 본문이 비어있음(로그인/세션 필요로 추정) |
| [playwares.com 게시글](http://playwares.com/gametalk/20550677) | 접속 자체가 거부됨(`ECONNREFUSED`) - 사이트 다운으로 추정 |
| [Daum 카페 "GTA 커뮤니티"](https://cafe.daum.net/GTA3mods/16Vv/44) | 브라우저로 직접 접속 성공, 다운로드 버튼은 있으나 `javascript:` 트리거라 실제 URL이 안 잡힘. 회원가입/로그인 게이트일 가능성 높음 |
| [blog.daum.net 미러 게시글](http://blog.daum.net/_blog/BlogTypeView.do?articleno=67&blogid=0T725) | 도메인 자체가 더 이상 존재하지 않음(`ENOTFOUND`) |
| Wayback Machine (`web.archive.org` CDX API) | `gtacommunity.cafe24.com`은 2004~2022년 사이 여러 번 스냅샷이 있지만, 실제 첨부파일(`/zeroboard/file/*`)로는 4개만 크롤링됐고 그중 한글패치 파일은 없음 - 크롤러가 로그인/세션이 필요한 다운로드 링크까지는 못 따라간 것으로 보임 |
| [libertycity.net](https://libertycity.net/files/gta-vice-city/various-files/localizations/) | 검색 UI가 응답 없음, 접근 실패(403) |
| [bcpark.net 게시글](http://www.bcpark.net/bbs/147358) | 파일은 있으나(`ViceCity_Patch_11.exe`) 게시일이 2003년 5월로 VC PC 발매 직후 - 이건 한글패치가 아니라 **락스타 공식 v1.1 패치**로 추정(한글패치는 2005년산이라 시기가 안 맞음) |
| [나무위키 SA 한국어 패치 문서](https://namu.wiki/w/Grand%20Theft%20Auto:%20San%20Andreas/%ED%95%9C%EA%B5%AD%EC%96%B4%20%ED%8C%A8%EC%B9%98) | GTA3/VC 한글패치가 SA 패치보다 번역 품질이 나았다는 언급은 있지만, 폰트/인코딩 방식에 대한 기술적 설명은 없음 |

**결론**: 이번 세션에서 사용 가능한 도구(WebSearch/WebFetch/브라우저/curl)로는
실제 파일을 구하지 못했다. 로그인 게이트, 도메인 소실, 세션 필요 다운로드
링크가 겹쳐서 생긴 문제로, "덜 찾아봐서"가 아니라 20년 된 개인/카페 사이트
자료의 전형적인 접근성 문제로 보인다. 사용자가 개인적으로 갖고 있는
사본이나, 로그인 가능한 계정이 있다면 그걸로 다시 시도해볼 수 있다.

## 2. plugin-sdk SA vs VC 비교

[DK22Pac/plugin-sdk](https://github.com/DK22Pac/plugin-sdk)는 SA/VC/III를 모두
지원하지만, 두 게임 지원 수준과 구조 자체가 다르다.

### 2.1. `CMessages` - 가장 중요한 차이

| | SA | VC |
|---|---|---|
| 문자열 타입 | `char*` (싱글바이트) | **`wchar_t*` (와이드 문자)** |
| `tMessage` 구조체 | 전부 공개됨, 필드 오프셋까지 `VALIDATE_OFFSET`로 검증됨 (`m_pText` 0x0, `m_dwNumber[6]` 0x10 등) | **구조체 자체가 plugin-sdk에 없음** - 함수 시그니처만 있음 |
| `BIGMessages`/`BriefMessages` 배열 주소 | 공개됨 (`0xC1A970`, `0xC1A7F0`) | **plugin-sdk에 없음** |
| `Display()` 시그니처 | `Display(bool flag)` | `Display()` - **인자 없음** |

이 프로젝트(`src/dllmain.cpp`)의 핵심 트릭 두 가지 - ① `tMessage::m_pText`를
바이트 단위로 직접 읽어서 자모 디코딩, ② `hkDisplay(bool)`로 원본 그리기를
조건부 억제 - 둘 다 SA가 **싱글바이트 문자열 + `bool` 인자가 있는 `Display`**
라는 전제 위에 만들어져 있다. VC는 이 전제 자체가 plugin-sdk 기준으로는
성립하지 않는다.

### 2.2. `CFont` - 같은 패턴이 한 번 더 확인됨

| | SA | VC |
|---|---|---|
| 문자 단위 API | `PrintChar(float x, float y, char character)`, `GetNextSpace(char*)` 등 - **`char`** | `GetStringWidth(const wchar_t*)`, `GetTextRect(..., const wchar_t*)` 등 - **`wchar_t`** |

`CMessages`뿐 아니라 실제 글자를 그리는 `CFont`까지 VC는 와이드 문자
기준이다. 우연이 아니라 VC 엔진의 텍스트 파이프라인 자체가 SA와 다른
문자 폭을 쓴다는 뜻으로 보인다.

### 2.3. `CMenuManager` - 이건 비슷하게 이식 가능

| | SA | VC |
|---|---|---|
| `m_bMenuActive` 오프셋 | `0x5C` | `0x38` |
| plugin-sdk 검증 상태 | `VALIDATE_OFFSET`로 검증됨 | `VALIDATE_OFFSET`로 검증됨 |

이건 SA/VC 둘 다 구조체가 잘 정리돼 있어서, 오프셋만 바꾸면 이식 가능한
부분이다. `MenuManager_bMenuActive` 관련 로직(ESC 메뉴 중 오버레이 숨기기)은
VC로 옮겨도 큰 무리가 없을 것으로 보인다.

### 2.4. D3D9 후킹 부분 - 영향 없음

`DEVELOPER_GUIDE.md`에 정리된 더미 디바이스로 `EndScene`/`Reset` vtable을
읽어서 후킹하는 방식은 GTA 엔진과 무관한 범용 D3D9 기법이라 VC에도 그대로
쓸 수 있다. ImGui 초기화/렌더링 부분도 마찬가지.

## 3. 왜 `wchar_t` 차이가 결정적인가

SA 한글패치가 굳이 "초성+중성+종성 조각을 겹쳐 그리는" 복잡한 트릭을 쓴
이유는, SA의 텍스트 파이프라인이 **싱글바이트(`char`)** 라서 한 바이트(256
가지)로는 modern Hangul(11,172음절)을 다 담을 수 없었기 때문이다(자세한
내용은 [WORK_LOG.md](../docs/WORK_LOG.md) 참고).

VC는 애초에 **와이드 문자(`wchar_t`, 사실상 16비트)** 파이프라인이라, 한
"칸"에 담을 수 있는 값의 범위가 65536가지다. 완성형 한글 음절(11,172개)이
통째로 들어가고도 남는다. 즉 VC 한글패치를 만든 사람이 SA 패치와 똑같은
"자모 조각을 억지로 겹쳐서 흉내내기" 방식을 쓸 **이유 자체가 별로 없었을
가능성이 높다** - 오히려 진짜 완성형 유니코드/CP949 코드를 그대로 쓰거나,
최소한 SA보다 훨씬 단순한 방식을 썼을 가능성이 있다.

물론 이건 파일을 직접 못 열어본 상태에서의 추론이라 100% 확정은 아니다.
하지만 만약 이 추론이 맞다면, 오히려 좋은 소식이다 - VC 쪽은 이 프로젝트가
푸는 "네이티브 렌더러가 자모 조각을 잘못 겹쳐서 기울어져 보인다"는 문제
자체가 원래 없을 수도 있다는 뜻이기 때문이다.

## 4. 결론 및 다음 단계

- **파일 확보**: 실패. 사용자가 개인 소장 사본이나 로그인 가능한 경로를
  갖고 있다면 그걸로 재시도하는 게 가장 빠르다.
- **구조 비교**: SA와 VC는 `CMessages`/`CFont`의 문자 폭 자체가 달라서
  ("같은 구조" 판단의 전제가 되는) 이 프로젝트의 핵심 트릭이 VC에 그대로
  옮겨진다고 보기 어렵다. VC판을 만든다면 지금 프로젝트를 복사/수정하는
  게 아니라, VC 한글패치가 실제로 뭘 하는지부터(정말 자모 조각 방식인지,
  아니면 이미 완성형인지) 원점에서 재조사해야 한다.
- **재사용 가능한 부분**: D3D9 후킹(더미 디바이스로 vtable 읽기), ImGui
  오버레이 렌더링 구조, `CMenuManager` 기반 ESC 메뉴 숨기기 로직은 오프셋만
  바꾸면 VC에도 거의 그대로 재사용 가능해 보인다.
- **막힌 부분**: `CMessages`/`tMessage` 동급 구조체와 `BIGMessages`/
  `BriefMessages` 동급 배열 주소가 plugin-sdk에 없어서, VC로 포팅하려면
  이 부분은 직접 리버스 엔지니어링(Cheat Engine 등)해야 한다.

## 참고 링크

- [DK22Pac/plugin-sdk](https://github.com/DK22Pac/plugin-sdk)
- [plugin_vc/game_vc/CMessages.h](https://github.com/DK22Pac/plugin-sdk/blob/master/plugin_vc/game_vc/CMessages.h)
- [plugin_sa/game_sa/CMessages.h](https://github.com/DK22Pac/plugin-sdk/blob/master/plugin_sa/game_sa/CMessages.h)
- [plugin_vc/game_vc/CFont.h](https://github.com/DK22Pac/plugin-sdk/blob/master/plugin_vc/game_vc/CFont.h)
- [Daum 카페 "GTA 커뮤니티" - 바이스시티 한글패치 최종버전](https://cafe.daum.net/GTA3mods/16Vv/44)
- [게임세상 - 한글패치 (100% 완전 해결)](https://gamess.co.kr/pds_board_view.php?no=14154)
- [나무위키 - GTA SA 한국어 패치](https://namu.wiki/w/Grand%20Theft%20Auto:%20San%20Andreas/%ED%95%9C%EA%B5%AD%EC%96%B4%20%ED%8C%A8%EC%B9%98)
