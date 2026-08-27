<!-- tracks: ../en/GETTING_STARTED.md (canonical) -->
<!-- 주의: 이 번역은 기계 초벌 번역입니다. 원어민의 검토를 환영합니다. -->
> ⚠️ 이 번역은 초벌 번역입니다. 용어의 정확성은 영어판(`en/`)을 우선하십시오.

# jichi 시작하기

jichi 는 Linux 용 명령줄 AI 코딩 에이전트입니다. 이 페이지는 아무것도 없는
상태에서 실제로 동작하는 세션까지 안내합니다. 아래에 링크된 상세 가이드는
영어로 되어 있습니다.

## 1. 설치

**libcurl** 과 C 컴파일러가 필요합니다. 두 개의 바이너리를 빌드합니다:

```sh
make
```

`jichi`(에이전트)와 `jichi-convert`(설정 가져오기 도구)가 만들어집니다.
시스템 요구 사항은 [INSTALL.md](../../INSTALL.md) 를 참고하십시오.

## 2. 설정

안내형 설정을 실행하면 프로젝트의 언어를 감지하여 설정 파일을 작성합니다:

```sh
jichi setup
```

최소한의 안전한 출발점(학습에 적합)을 원하면 `--profile beginner` 를 추가하십시오.
API 키는 **환경 변수**에서 읽으며, 설정 파일에 저장되지 않습니다.

모든 것이 제대로 연결되었는지 확인합니다:

```sh
jichi doctor      # 상태 점검
jichi benchmark   # 권장 설정 적용 점수
```

jichi 는 사용자의 언어로 답할 수 있습니다. 설정에 `"language": "Korean"`
(또는 다른 언어)를 추가하거나 `--language` 를 전달하십시오 —
[LANGUAGE.md](../../LANGUAGE.md) 참고.

> 작은 로컬 모델에서는 이 설정만으로 답이 비는 경우가 측정되었습니다. 그럴 때는
> 프롬프트 안에서 직접 언어를 지정하십시오 — 자세한 내용은
> [CJK.md](../../CJK.md) 4절에 있습니다.

## 3. 사용하기

대화형 세션(REPL):

```sh
jichi
```

평소 쓰는 말로 요청을 입력하십시오. 코드를 가리킬 때는 `@file` 또는 `@sym:name`
를 사용합니다. 유용한 명령: `/help`, `/model`, `/mode`, `/constraints`, `/undo`.
**Ctrl-C** 는 종료하지 않고 현재 작업만 중단합니다.

헤드리스(스크립트와 자동화용):

```sh
jichi -p "explain what src/main.c does"
```

## 4. 통제권 유지하기

- **모드:** chat(행동 전에 확인), plan(읽기 전용), auto(자율).
- **제약(Constraints):** "빌드를 실행하지 마라"고 말하면 기억만 하는 것이 아니라
  *강제*됩니다 — [CONSTRAINTS.md](../../CONSTRAINTS.md) 참고.
- **되돌리기:** 모든 변경은 체크포인트로 기록되며 `/undo` 로 되돌립니다 —
  [SNAPSHOTS.md](../../SNAPSHOTS.md) 참고.

## 한국어·일본어·중국어 사용자를 위한 안내

터미널과 글꼴 설정, CJK 파일명과 식별자, 입력기(IME) 동작, 그리고 직접 확인하는
방법은 [CJK.md](../../CJK.md) 에 정리되어 있습니다. 검증되지 않은 항목도 그대로
적어 두었습니다.

## 다음으로 볼 곳

- 처음부터 숙련까지의 전체 여정: [JOURNEY.md](../../JOURNEY.md)
- 목적에 맞는 경로 선택: [WORKFLOWS.md](../../WORKFLOWS.md)
- 입문 실습: [TUTORIAL_BEGINNER.md](../../TUTORIAL_BEGINNER.md)
- 고급 사용자를 위한 하위 시스템: [TUTORIAL_ADVANCED.md](../../TUTORIAL_ADVANCED.md)
