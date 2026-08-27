---
marp: true
title: jichi の使い方
theme: default
paginate: true
---
<!-- tracks: ../../../presentations/02-using-jichi.md @ 0632b94 -->
<!-- 注意: この翻訳は機械下訳です。英語版 ../../../presentations/02-using-jichi.md を正とします。 -->

<!-- _class: lead -->

# jichi の使い方

### 初回起動から自律タスクまで

---

# 1コマンドでセットアップ

```sh
jichi setup                      # interactive wizard
# or, reuse your global config in a project:
jichi setup --from-global --preset developer
# or, adopt an unfamiliar repo (propose-only analysis + tutorial draft):
jichi setup --onboard
jichi --config local/config.json doctor   # validate everything
```

`doctor` は libcurl、config、モデル、キー、到達性、git、MCP、LSP、そしてあなたの
プロジェクトアセットをチェックする。

---

# 実行する3つの方法

| サーフェス | コマンド | いつ |
|---|---|---|
| **TUI** | `jichi` | 対話作業、レビュー、ワンキー承認。 |
| **ヘッドレス** | `jichi -p "…"` | スクリプト、CI、自動化、SSH。 |
| **構造化** | `jichi -p "…" --output jsonl` | 別のプログラム/エージェントが駆動。 |

プロンプトが `-` のとき stdin を読むので、`ARG_MAX` の制限なしにファイル全体を
プロンプトにできる。

---

# モード：どこまで手綱を緩めるか

- **`/chat`**——通常；変更を伴う操作の前に尋ねる。
- **`/plan`**——読み取り専用；調査して提案、編集なし。
- **`/auto`**——自律；サンドボックス化されたツールを尋ねずに実行。

```sh
jichi --plan -p "how would you add feature X?"    # a plan
jichi --auto -p "add feature X and make tests pass"  # do it
```

<!-- plan モードは、見慣れない/共有リポジトリを探索するときの安全な既定である。 -->

---

# 持っているツール

- **ファイル：** `read_file`、`write_file`、`edit_file`、`apply_patch`（アトミックな
  複数編集）、`list_files`、`search_code`。
- **実行：** `run_terminal_command`（+ バックグラウンド）、`run_tests`。
- **知識：** `codebase_search`、`search_docs`、`fetch_url`、`web_search`。
- **Git：** status/diff/log/blame + add/commit/branch/stash。
- **ナビ/リファクタ：** LSP `find_definition`/`references`/`symbols`、rename、format。
- **委譲：** `spawn_subagent`、`spawn_parallel`。
- **メディアと音：** `generate_image`、`generate_audio`、`transcribe_audio`、
  `play_audio`、`record_audio`。

---

# ターンに持ち込むコンテキスト

プレーンなメッセージ内の `@` 参照がコンテキストを引き込む：

```
review @src/parser.c against @diff and the @rss:https://…/releases.xml feed
explain @sym:jc_agent_run_turn and @folder:src/net
```

`@file @diff @url @rss @sym @docs @problems @folder @mcp @audio @img`——それぞれが
メッセージに追加される上限付きブロックへ解決される。

---

# セッションが長くなったら

- **自動圧縮**が古い接頭部を要約してウィンドウ内に収める；**ターン中圧縮**が
  1つの暴走ターン上で重いツール出力を刈り込む。
- **トークンキャリブレーション**が各モデルの実際のバイト/トークンを学習し、見積もりが
  楽観的に走るのをやめさせる。
- `/context` はライブな予算内訳を示す；`/cost` は進行中の支出を示す。

たいていは意識しなくてよい——それが狙いだ。

---

# 自律実行を安全に

```sh
jichi --auto \
  --verify "make test" \
  --budget-tokens 500k --deadline 30m \
  --edit-scope "src/**" \
  --journal run.jsonl --control \
  -p "fix the failing ring-buffer tests"
```

合格 → 前進；失敗 → N 回まで前進修正、それでもだめなら最後の緑へロールバック。
すべてがジャーナルにある。

ライブに操舵：`jichi control <sock> status | inject "…" | pause | abort`。
読み返す：`jichi runs` と `jichi audit`（どちらも `--output json`）。

---

# エディタの中で

- **Emacs**（`jichi.el`）、**Vim/Neovim**（`jichi.vim`）、**nano**（`jichi-nano`）——すべて
  ヘッドレス契約越し。
- **Zed / 任意の ACP エディタ**——`jichi serve`（きめ細かな承認、ストリーミング）。

```vim
:JichiAsk how does the fence work?    " answer in a scratch split
:'<,'>JichiRegion tidy this           " transform a selection in place
:JichiTask add a test for edge case Y " agentic, confirms first
```

---

<!-- _class: lead -->

# 便利なコマンド

`/model` `/mode` `/diff` `/undo` `/rewind` `/compact` `/context` `/cost`
`/skills` `/mcp` `/export` `/fork` `/sessions`

PR や授業のためにトランスクリプトを `export`；自分の位置を失わずにブランチを探索
するために `fork`。
