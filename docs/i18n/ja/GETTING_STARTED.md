<!-- tracks: ../en/GETTING_STARTED.md (canonical) -->
<!-- 注意: この翻訳は機械下訳です。ネイティブによるレビューを歓迎します。 -->
> ⚠️ この翻訳は下訳です。用語の正確さについては英語版（`en/`）を優先してください。

# jichi をはじめる

jichi は Linux 向けのコマンドライン AI コーディングエージェントです。この
ページでは、何もない状態から動作するセッションまでを案内します。詳細ガイド（下部の
リンク）は英語です。

## 1. インストール

**libcurl** と C コンパイラが必要です。2 つのバイナリをビルドします:

```sh
make
```

これで `jichi`（エージェント）と `jichi-convert`（設定インポーター）が生成
されます。システム要件は [INSTALL.md](../../INSTALL.md) を参照してください。

## 2. 設定

ガイド付きセットアップを実行します。プロジェクトの言語を検出して設定を書き込みます:

```sh
jichi setup
```

最小限で安全な出発点（学習に最適）には `--profile beginner` を付けます。API キーは
**環境変数**から読み込まれます（設定ファイルには保存されません）。

すべてが正しく設定されているか確認します:

```sh
jichi doctor      # 健全性チェック
jichi benchmark   # ベストプラクティス達成度のスコア
```

jichi はあなたの言語で答えられます。設定に `"language": "日本語"` を追加するか、
`--language` を渡してください（[LANGUAGE.md](../../LANGUAGE.md) 参照）。

## 3. 使う

対話セッション（REPL）:

```sh
jichi
```

要望を自然な言葉で入力します。コードは `@file` や `@sym:name` で指し示せます。
便利なコマンド: `/help`、`/model`、`/mode`、`/constraints`、`/undo`。**Ctrl-C** で
プログラムを終了せずに現在のタスクだけを停止できます。

ヘッドレス（スクリプトや自動化向け）:

```sh
jichi -p "src/main.c が何をするか説明して"
```

## 4. 制御を保つ

- **モード:** chat（実行前に確認）、plan（読み取り専用）、auto（自律）。
- **制約 (constraints):** 「ビルドを実行しないで」と言うと、記憶されるだけでなく
  *強制*されます — [CONSTRAINTS.md](../../CONSTRAINTS.md) を参照。
- **元に戻す:** すべての変更はチェックポイントに保存され、`/undo` で戻せます —
  [SNAPSHOTS.md](../../SNAPSHOTS.md) を参照。

## 次に読むもの

- 最初の一歩から熟達までの道のり: [JOURNEY.md](JOURNEY.md)
- 目的別のガイド: [WORKFLOWS.md](../../WORKFLOWS.md)
- 初心者向けチュートリアル: [TUTORIAL_BEGINNER.md](../../TUTORIAL_BEGINNER.md)
- 上級者向けサブシステム: [TUTORIAL_ADVANCED.md](../../TUTORIAL_ADVANCED.md)
