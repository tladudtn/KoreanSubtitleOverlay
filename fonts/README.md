이 폴더는 커스텀 한글 폰트 파일(.ttf/.otf)을 넣는 곳입니다.

기본 상태(아무 파일도 안 넣음)에서는 시스템에 설치된 맑은 고딕
(`C:\Windows\Fonts\malgunbd.ttf` → 없으면 `malgun.ttf`)을 그대로 씁니다 -
이 폴더가 비어 있어도 플러그인은 정상 동작합니다.

다른 폰트를 쓰고 싶다면:

1. 원하는 `.ttf`/`.otf` 파일을 이 폴더(`fonts/`)에 넣습니다.
2. `KoreanSubtitleOverlay.ini`의 `[Subtitle]` 섹션에서 `FontPath`를
   이 폴더 기준 상대 경로로 지정합니다:
   ```ini
   FontPath=fonts/MyFont.ttf
   ```
   (이 ini 파일과 `fonts/` 폴더는 항상 `.asi`와 같은 위치에 있어야 합니다.)
3. 게임을 완전히 재시작하면 적용됩니다.

지정한 파일을 못 찾거나 못 읽으면 자동으로 시스템 맑은 고딕으로
되돌아가고, `KoreanSubtitleOverlay.log`에 그 사실이 기록됩니다.

## 번들 폰트: 나눔고딕 Bold

이 폴더에 `NanumGothic-Bold.ttf`가 기본으로 같이 들어있습니다(네이버가
만들어 SIL Open Font License 1.1로 배포하는 폰트, 라이선스 원문은 같은
폴더의 [OFL.txt](OFL.txt) 참고 - 재배포/수정 자유). 실제 게임에서
`FontPath=fonts/NanumGothic-Bold.ttf`로 지정해 정상 로드/렌더링까지
확인했다. 기본값(`FontPath=` 빈 값)은 여전히 시스템 맑은 고딕이므로, 이
폰트를 쓰려면 ini에서 명시적으로 위 경로를 지정해야 한다.
