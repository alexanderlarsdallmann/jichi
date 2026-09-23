<!-- tracks: ../../VOCABULARY.md @ 046f5236 -->
<!-- 注意: この翻訳は機械下訳です。ネイティブによるレビューを歓迎します。 -->
<!-- Machine draft, llm-jp-4-8b-thinking via a local LM Studio, 2026-09-22.
     INCOMPLETE, AND HERE IS EXACTLY HOW MUCH. 18 of 77 non-blank body lines
     (23%) are still English -- counted by one stated rule, after that figure
     was got wrong three times with three different filters. They are not
     scattered: they are essentially two whole
     sections -- "Who may do what" (posture, verdict, fence, reference root,
     privileged gate, kinetic) and "Learning with jichi" (assignment, hint
     ladder, prediction, grade, tier, tutor stance, gate, record) -- plus two
     headings. Those two sections resisted four different pipelines and six
     retries each, so this is a limit of the model on definition-dense text,
     not a bad roll. A REVIEWER SHOULD TRANSLATE THOSE TWO SECTIONS BY HAND.
     Everything else was verified against the English with i18n_tracks_lint's
     own extraction: heading count, link targets and figures all match. -->
> ⚠️ この翻訳は未完成の下訳です。一部の節は英語のままです。正確さについては英語版（[`../../VOCABULARY.md`](../../VOCABULARY.md)）を優先してください。

# Vocabulary — このプロジェクトが使用する単語

このプロジェクトは独自の言語を構築します。そのプライベート言語は新規参入者にとっての「税金」です。このページはその領収書です。ドキュメント内で **jichi** が最初に使う用語を定義し、何も説明せずに先に示しています。そのため、ここで読むすべてのページの意味を文脈から推測する必要はありません。

> **[GLOSSARY.md](GLOSSARY.md)** と混同しないでください。これは *feature*（機能）であり、`.jichi/glossary.md` ファイルです。**あなた**がプロジェクト固有のドメイン用語を定義し、エージェントに使わせるための設定ファイルです。このページは設定ファイルについて書かれています。このページは私たちの言葉についてです。

もし出会った用語がこの一覧に無い場合、それはこのページの欠陥—正直な種類のバグであり、報告すべき価値があります。

## モデルとの会話
- **turn** — 1回のやり取り：あなたのメッセージ、エージェントが応答で行うこと（ツール呼び出しを含む）および回答。予算や通知は主にターン単位です。
- **token** — モデルが読み取って課金する単位；英語文章では約3/4語分、コードの場合ははるかに少ない。「コスト」はすべてトークンです。
- **context window** — モデルが同時に保持できるトークン数。`config.contextLength`で宣言しますが、サーバー側の実際のウィンドウより大きいと予算が無効化され、サーバーがリクエストを拒否するまで適用されません。
- **system prompt** — エージェントに送る事前指示：誰がエージェントか、利用可能なツール、リポジトリマップ、ルールや記憶など。`jichi sysmsg`で表示し、`jichi context`でサイズを調整できます。
- **role** — 設定されたモデルが何のためにあるか：`chat`, `embed`, `rerank`, `audio`, `transcribe`, `vision`。1つのエントリに複数のロールを持たせられます。`jichi`はロールに基づいてモデルを選択し、例えば「埋め込みロールのモデルがない」場合はセマンティック検索がオフになります。
- **routing tier** — `fast` または `strong`: 機械的な作業には安価なモデル、推論が必要な作業には高性能なモデルを割り当てます。
- **compaction** — 古い履歴を要約してコンテキストウィンドウ内に収める処理です。ターン間だけでなく、1つのターンが大きすぎる場合にも途中で行われます。
- **elision** — ツール出力が長すぎるときに省略し、その欠落部分を**クレームチケット**として残します。モデルはこのマーカーを使って削除された部分を取得できます。
- **quantized** — ハードウェア要件を抑えるために縮小（4ビット、8ビット…）したモデル。ローカルでダウンロードするほとんどのモデルは量子化されています。

## Who may do what
- **posture** — how much the agent may do *without asking*. The three modes, widest first: `auto` (approve everything permitted), `chat` (ask before changing anything), `plan` (read‑only; nothing changes at all).
- **verdict** — the resolved answer for one tool call: **ASK**, **ALLOW** or **DENY**. See [TOOL_DECISIONS.md](TOOL_DECISIONS.md) for how the six mechanisms compose into it.
- **fence** — a boundary that refuses rather than warns. The *path* fence keeps file tools inside the workspace; the *tool* fence limits which tools an agent is offered; the *edit‑scope* fence limits which paths a run may write.
- **reference root** — an external directory you explicitly allow reads from while the fence is on. Writes stay in the workspace.
- **privileged gate** — the separate check for a model‑issued `sudo`/`doas`/`pkexec`/`su`/`run0`, applied after the verdict, so no blanket approval can cover it.
- **kinetic** — a tool whose call moves mass or energy in the physical world: a motor, a valve, a siren. Gated and audited like a privileged command.

## 無監督実行

- **envelope** ― 監視されていない実行全体を囲む安全境界：予算、スコープ、検証コマンド、ロールバック、ジャーナル。
- **budget** ― ハードキャップ：トークン数、壁時計時間（`--deadline`）、ツール呼び出し回数、読み取り回数。いずれかに達すると実行が停止する。
- **verifier** ― 終了ステータスで成功が決まるコマンド（`--verify` または `testCommand`）。過去に一度も失敗したことがない検証コマンドはゲートとして機能しない。
- **green** ― 検証が通過した状態。「緑チェックポイント」はその状態を記録したワークスペースの状態で、失敗した実行はこの地点までロールバックされる。
- **hollow green** ― 「合格」だが何も証明できていない状態：空のテ​​ストスイート、失敗しない検証コマンド、チェックの0.5％しか走らないゲートなど。jichi は現在、成功が空である旨をモデルに通知する。
- **checkpoint / snapshot** ― `~/.jichi.d/checkpoints/` 配下のシャドウ Git リポジトリに保存されたワークスペース状態。自分の `.git` は決して触れない。`/undo` コマンドでそれらを遡って復元できる。
- **rollback** ― 実行が赤で終了した場合に、最後の緑チェックポイントへ戻す操作。
- **fix‑forward** ― 解析したテスト失敗情報をモデルに再度提示し、別の試行を行う前に諦める手段。
- **baseline** ― 実行開始時点のコミット。変更量の判定や「範囲外」の判断の基準になる。
- **journal** ― あなたが不在の間に取られたすべての意思決定を JSONL 形式で記録したもの（`jichi runs`）。テレメトリはオプトインのメトリクスログ、監査ログは特権的・動的な試みを常に記録する。
- **drift** ― 以前は正しかったという主張。誰も再測定していない数値、古い挙動を説明するドキュメント、翻訳が追従している旧版など。

## このコードベースは構築・検証されます

これらの資料は、ソースコードの読み取りガイドやテスト、分析ノートを読む際に必要です。

**pure core / thin shell** ― デザイン原則：決定ロジックは I/O を持たない関数に実装し（オフラインでテスト可能、網羅的に）、世界と接する最小限の層だけをその周囲に配置すること。

**chokepoint** ― すべてのパスが必ず通過する唯一の場所であり、そこで保証を一度だけ適用できる。

**seam** ― 挙動を観察または差し替えるための意図的な箇所で、通常は外部世界が介入すべきテストが置かれる位置。

**invariant** ― 常に真でなければならない性質（例：「ターン中にアリーナは決して解放されない」）。この不変条件をピンポイントで検証するテストは、サンプル的テスト10個分の価値があります。

**immutable** ― 作成後に変更されることのないもの。他のコードが背後で変更できないため、推論が容易になる。

**lint** ― ソースコードやドキュメントを読み取り実行するプログラムではなくチェックするツール。「すべての文書化されたフラグが存在する」「すべてのツール名が登録されている」などを確認。このプロジェクトは監査よりもリントを好む――監査は人の午後を要し、リントはビルド失敗として扱えるからです。

**TAP** ― Test Anything Protocol：`ok 3 - <description>`／`not ok 3 - …` の出力形式を煙テスト階層が使用。計画行（`1..8`）で期待するチェック数を示すので、早期に死んだスイートは緑に見えません。

**smoke tier** ― 実際のバイナリをエンドツーエンドで駆動する POSIX‑sh テストスイート（`tests/smoke/`）。319 台のドライバと約 1,870 件のチェックがあり、Python を使わないため 256 MB のマシンや四つのカーネル上でも動作します。

**two‑sided proof** ― 新しいテストは修正なしで失敗し、修正後に成功することを示しなければならない。過去に赤になったことがないテストは、そのバグを覆す証拠になりません。

**floor** ― テスト自身の抽出下に置くアサーション（例：「このスキャンで最低 400 ファイルが見つかった」）。チェックが黙って探索を止めた場合は失敗させ、合格させないようにします。

**vacuous check** ― 決して失敗しないチェック：常に存在することが保証された文字列を grep したり、タイプミスが既に生成したステータスを検証したりするもの。本プロジェクトの履歴にいくつか記載されており、そのため floor が導入されています。

**rig** ― VM やボード上に OS をインストールし、そこに jichi を構築して各階層のテストを実行し結果を報告するスクリプト。**Tier** はテストピラミッドのレベル（unit、smoke、e2e、platform）を指す。

**dogfooding** ― 実際のプロジェクトで jichi を運用し欠陥を探す手法。このツリー内の多くの興味深いバグはこの手法で発見され、コードレビューだけでは見つかりませんでした。

**register** ― 決定・延期・通知を記録して検索可能にする表（`DECISIONS.md`、`DEFERRED.md`、`NOTICES.md`）。ここに無いものは口伝です。

**milestone (`M###`)** ― デザインノート、テスト、ドキュメント、スコープ付きコミットを含む作業単位。ROADMAP はマイルストーンごとのエンジニアリング記録です。

## Learning with jichi
- **assignment / spec** — one markdown file that is both the brief you read and   the machine‑checkable task; its `verify` line grades you.
- **hint ladder** — a spec's graded nudges, one **rung** at a time via `/hint`.   Free, recorded, never penalised.
- **prediction** — what you said you expected *before* you looked, via   `/predict <text>`; resolved with `/predict right|wrong`; tallied by `/predict`.   Its own file (`.jichi/predictions.jsonl`), never scored (M635). The names for   the argument you are making while you work are in [ARGUMENT.md](ARGUMENT.md).
- **grade** — run a spec's verifier and score it. **attempt** is the *agent*   solving the spec instead of you — useful for comparison, not for credit.
- **tier** (in a brief) — the audience framing: junior, student, senior, agent.
- **tutor stance** — while an assignment is active, the model guides and declines   to hand over the solution.
- **gate** (in the curriculum) — a stage's mechanical exit condition: points plus   a written record.
- **record** — your own debugging log: symptom, dead ends, root cause, lesson.

## The English idioms this project uses
Added at M657, after the first native Japanese‑speaking reviewer of the localized pages pointed out that a term like *dogfooding* needs its **metaphor** explained, not only its usage. These are not technical terms. They are English figures of speech that a reader can look up word by word and still not understand, and every one of them is load‑bearing somewhere in this repository. Where the literal image is what makes the term stick, it is given — guessing at the image is exactly what makes these words opaque to everyone who did not grow up with them.

- **dogfooding** — 定義は上記参照；画像は *自分の犬フードを食べる*、つまり、
会社自身が販売するドッグフードを自社の犬に与えること。実プロジェクトでツールを使うことは同様の賭け：もしそのツールが我々にとって十分でなければ、良いとは言えない。
- **blast radius** — 爆薬から派生した用語：「被害範囲」がどれだけ広がるか。フェンスや `--edit-scope`、読み取り専用エージェントはミスの **blast radius**（影響範囲）を限定するが、ミス自体は防げない。ここでは16ページがこの意味で使用されている。
- **teeth** — 「歯がある」規則は噛みつくことができる：ビルドに失敗する。`tests/teeth.sh` スクリプトはその儀式――守っているものを元に戻し、そのチェックが赤くなる様子を見る——を行う。誰も監視していないチェックには歯がなく、名前が示す通りだ。
- **born red** — 最初に失敗するように書かれている（修正が存在しない段階で）。テストレポートで失敗する色は赤なので、「生まれた時点で赤い」テストは最初に失敗し、その後修正が加えられる。緑で生まれたテストは何も検出できなかったとみなされるため、このプロジェクトでは未実装として扱われる。
- **flaky** — 同じコードに対してパスしたり失敗したりするので、テスト自体の問題ではなく外部要因（タイミング、共有ディレクトリ、実時計など）が原因となることが多い。失敗するテストより厄介で、人は再実行して読む代わりに済ませてしまう傾向がある。
- **footgun** — 自己の足を撃つほど簡単にしてしまう機能。バグではなく、ドキュメントどおりに動作し、そのドキュメントを読まなかった部分が問題になる。
- **happy path** — 何も起こらない正常な実行経路。このプロジェクトの多く教訓はこの道筋に基づいているが、エラーパスは別階層（`make smoke-faults`）で扱われている。
- **paper over** — 問題を覆い隠すだけで解決しない方法、壁紙がひび割れを隠すように。ここでは非難として使われ、主に自分自身への指摘に用いられる。
- **shu‑ha‑ri** （守破離）— 日本の学習曲線：形を守り、形を破り、形を離れる。英語ページでは翻訳せずにそのまま掲載されているが、読者によっては明らかに見えるものと不透明なものがあり、本節の主題となっている。

## See also

- [PLAIN_LANGUAGE.md](PLAIN_LANGUAGE.md) — jichi が専門用語なしですべて説明したもの
- [TOOL_DECISIONS.md](TOOL_DECISIONS.md) — 許可チェーンの順序について
- [STATE.md](STATE.md) — jichi が実際に保持しているすべての場所
- [GLOSSARY.md](GLOSSARY.md) — **your** プロジェクト用の定義集
