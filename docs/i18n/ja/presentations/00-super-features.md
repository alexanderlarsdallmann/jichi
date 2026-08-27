---
marp: true
title: jichi — 目玉機能
theme: default
paginate: true
---
<!-- tracks: ../../../presentations/00-super-features.md @ 0632b94 -->
<!-- slides-behind: 1 (en has 12 slide separators, this has 11). The English deck
     gained slides this translation does not carry. Translating them is prose in a
     language this repository cannot review, so the gap is DECLARED rather than
     filled -- and the numbers above are checked, so the declaration cannot be left
     behind silently either. M582. -->
<!-- 注意: この翻訳は機械下訳です。英語版 ../../../presentations/00-super-features.md を正とします。 -->

<!-- _class: lead -->

# jichi

### **~C89** で書かれた AI コーディングエージェント、1つの小さなバイナリ

Continue CLI のゼロからの書き直し：チャット、エージェント的ツールループ、RAG、
自律性、MCP、LSP、TUI ——移植性の高い ANSI C で、ランタイム不要。

<!-- 冒頭：売り文句は「約1.2 MB に収まり、POSIX + libcurl があればどこでも動く、
完全な現代的コーディングエージェント」。 -->

---

# 人を驚かせる10のこと

1. **C89** である。`-std=c89 -pedantic -Wall -Wextra` で警告ゼロ。
2. バイナリは**約1.2 MB**；ヘッドレスの1ターンは**約10–17 MB RSS**で動く。
3. **あなたのコードを編集する**——差分付きの、耐性のある/ファジーな複数ファイルパッチ。
4. **自律的に**動く、ガードレール付きで——実行中に一時停止/操舵できる。
5. **RAG** を行う——リポジトリ*および*ドキュメントに対する BM25 + 埋め込み + rerank のハイブリッド。
6. **MCP**（クライアント）*および* **ACP**（サーバ、Zed のようなエディタ向け）を話す。
7. **LSP** のナビゲーション*と*リファクタ（rename/format/code-actions）を持つ。
8. **画像と音声**を生成し、音声を**再生/録音**する。
9. 命じられずに `sudo` したり**モーター**を駆動したりしない——すべての試みは監査される。
10. すべての変更を**チェックポイント**し、**Pi や電話**（Termux）でも動く。

---

# ひと息で語るエージェントループ

```
history + system + tools
  → provider.build_request()      (Anthropic or OpenAI, streamed)
  → HTTP/SSE                      (libcurl)
  → execute tool calls           (read/edit/run/search/git/…)
  → append results, loop         until a final answer
```

1つのループ、2つのプロバイダ、約35個の組み込みツール、そしてプロバイダで分岐する
ことは決してない。

<!-- 全体は1つの関数 jc_agent_run_turn であり、それ以外はすべてツールかプロバイダ
vtable である。 -->

---

# 任せて信頼できるガードレール

- **モード：** chat（尋ねる）、plan（読み取り専用）、auto（自律）。
- **パスフェンス：** ファイルツールはワークスペースの外へ出られない；読み取りは
  名前付き参照ルートまで拡張できるが、書き込みは決して。
- **自律エンベロープ：** トークン/実時間/ツール呼び出しの予算、編集スコープフェンス、
  **検証ゲート**（あなたのテストを実行）、そして JSONL 監査ジャーナル——編集スコープ外の
  シェル由来の変更を**自動リバート**するオプトインつき。
- **複数ファイル編集は約束を守る：** `apply_patch` は all-or-nothing で検証し、
  書き込みが途中で失敗すれば、すでに書き込んだファイルをリバートして各ファイルの
  状態を報告する。
- **スナップショット：** シャドウ git リポジトリが最初の編集の前にチェックポイントを
  取る——`/undo` と `/rewind` がファイル*と*会話を復元する。

> タスクの途中で予算を使い切った？ 一度検証し、通っている作業は保持する；*赤い*
> ツリーだけをロールバックする。部分的な進捗は捨てられない。

---

# *上*にもスケールする

- **サブエージェント**——スコープを絞ったサブタスクを委譲（独自の履歴、モデル、
  ツールフェンス）、既定でいまや2階層深く、深さごとの予算逓減つき。
- **並列エージェント**——fork プール、各タスクを隔離された git worktree で、
  ファイル単位・先勝ちでマージ。
- **デーモン**——ウォームなプロセスが config/MCP/LSP/index をホットに保ち、ソケット
  越しにリクエストを処理、**上限付きワーカープール** + リクエストごとのウォッチドッグつき。
- **ループとフリート**——スーパーバイザがタスクキューを処理（tmux/systemd/cron）；
  コーディネータが SSH + MCP 越しにピアインスタンス間で作業を分散。

<!-- これこそが zigodot の書き直しを実現可能にするもの：ファンアウト、隔離、マージ。 -->

---

# 本物のドッグフード：zigodot の書き直し

jichi は**大規模で自律的な Godot → Zig 移植**を駆動している——北極星となるテスト。

- 実際のコードベース上での、エンベロープ下での長い `--auto` 実行。
- テレメトリ + セッションごとのタイムラインが、トークン/コストが実際にどこへ行くかを示す。
- **学習ループ**が自らのログを durable な教訓として還元し、同じ間違いを繰り返さない
  ようにする。
- 何かを教えてくれた武勇伝はすべて `docs/ANECDOTES.md` に残る。

難しい部分（コンテキストオーバーフロー、キャッシュレスの経済性、隔離のバグ）は、
理論化ではなく*使うこと*によって見つかった。

---

# あなたの働く場所に合わせる

- **ターミナル：** 本物の TUI——markdown + 構文ハイライト、ライブ差分、ワンキー承認、
  `/cost`、`/context`、`/undo`。
- **ヘッドレス：** `jichi -p "…"`；自動化向けに `--output json`/`jsonl`。
- **エディタ：** Emacs、Vim/Neovim、nano、そして任意の **ACP** エディタ（Zed）。
- **リモート：** GPU/CI ボックス上での長い自律実行のための SSH + tmux。
- **あなたの言語：** `"language": "日本語"` とすればそれで答える——承認プロンプトも
  追随する（en/de/es/ja/zh）；オンボーディングドキュメントは5言語すべて。

1つのバイナリ、あらゆるサーフェス、その下では同じ契約。

---

# ローカル、プライベート、安価

- **任意の OpenAI 互換エンドポイント**に向ける——LM Studio、llama.cpp、LocalAI、
  vLLM。ベンダーロックインなし；キー不要のローカルサーバはそのまま動く。
- **プロンプトキャッシュ**（両プロバイダ） + キャッシュを意識したコスト計算。
- ローカルの **LocalAI** バイナリに対する**メディア生成**——Docker なしのコンシューマ
  GPU で画像生成を検証済み。
- コンテナが動けない場所でも動く：組み込み、低 RAM、準エアギャップ。

---

<!-- _class: lead -->

# なぜ C89 か？

「移植性が高く、小さく、依存が少なく、10年後もまだここにある」ことが機能だから
——組み込みターゲットのため、教育のため、そして信頼のために。

**依存：** libcurl + 同梱の JSON ライブラリ。それだけ。

---

<!-- _class: lead -->

# 試してみる

```sh
make && ./jichi setup            # guided setup
./jichi -p "explain this repo"   # headless
./jichi                          # the TUI
```

ドキュメント：`README.md`、`docs/ROADMAP.md`（テーマ別インデックス）、
`docs/AGENTS_GUIDE.md`。
