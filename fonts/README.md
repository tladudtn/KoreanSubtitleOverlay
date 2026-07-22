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
