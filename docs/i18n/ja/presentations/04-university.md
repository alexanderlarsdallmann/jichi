---
marp: true
title: 大学での jichi
theme: default
paginate: true
---
<!-- tracks: ../../../presentations/04-university.md @ 0632b94 -->
<!-- slides-behind: 2 (en has 12 slide separators, this has 10). The English deck
     gained slides this translation does not carry. Translating them is prose in a
     language this repository cannot review, so the gap is DECLARED rather than
     filled -- and the numbers above are checked, so the declaration cannot be left
     behind silently either. M582. -->
<!-- 注意: この翻訳は機械下訳です。英語版 ../../../presentations/04-university.md を正とします。 -->

<!-- _class: lead -->

# 大学での jichi

### 研究、授業、再現性

---

# なぜ学術環境に合うか

- **手持ちのハードウェアで動く**——共有ログインノード、古いラボ機、Raspberry Pi、
  Termux 上の電話。約1.2 MB、約10–17 MB RSS、リモートまたはローカルのモデルと話す。
- **ベンダーロックインなし**——学科の LLM サーバ、ローカルの llama.cpp/vLLM、
  または商用 API に向ける。キー不要のローカルサーバはそのまま動く。
- **監査可能**——テストスイート付きの C89 であり、学生はエージェントを使うだけでなく
  *読める*。
- **コストを意識**——プロンプトキャッシュ + ライブな `/cost`；予算が自律実行に
  上限をかける。

---

# 研究ソフトウェアのために

- **レガシーコードベースを素早く理解**——`@folder:` マップ + `codebase_search` +
  論文の参考資料に対する `search_docs`。
- **再現可能な実行**——ヘッドレス `-p` + `--output json`、Makefile や Slurm ジョブから
  駆動；JSONL ジャーナルがあなたの来歴記録となる。
- **手綱付きの自律**——`--auto --verify "make test" --budget-*` により、夜通しの
  リファクタが暴走したりビルドを黙って壊したりできない。
- **ケーススタディ：zigodot の書き直し**——jichi によって完全に駆動された、大規模で
  自律的な言語移植。テレメトリが工数の行き先を正確に示す。
- **身体化 / ロボティクス研究**——jichi をロボットの*熟慮する*層として（ツールとしての
  センサ + アクチュエータ、運動安全ゲート、サウンド I/O）；ハードウェア不要の
  シミュレータが `examples/robot-sim/` に同梱（`docs/ROBOTICS.md`）。
- **教科としてのソフトウェア工学と PM**——`docs/PROJECT_TIMELINE.md` はデータに
  基づく振り返り（フェーズ、強度、開発者 + AI を含む4つの提供モデル）であり、
  プロジェクト管理や SE の講義で使える。

---

# 講義を教えるために

- **課題**機能：教員がブリーフ + ルーブリック + ヒントの階段を作成する；参照解答は
  伏せられ、採点は**読み取り専用**でルーブリックに紐づく。（`docs/TEACHING_ASSIGNMENTS.md`
  参照。）
- **TA 間で一貫した採点**——同じルーブリック + 参照 + 読み取り専用チェッカーにより、
  採点者間のばらつきが減る。
- **エージェントは可読である**——「エージェントループはどう動くか？」という課題は、
  *この*コードベースの `jc_agent_run_turn` を指し示せる。

---

# 再現性と来歴

```sh
jichi --auto \
  --verify "pytest -q" \
  --budget-tokens 300k \
  --journal artifacts/run-$(date +%s).jsonl \
  --output json \
  -p "implement the FFT variant from the spec and pass the tests" \
  > artifacts/result.json
```

すべてのモデル呼び出し、ツール呼び出し、コスト、結果が記録される——ラボノートや
論文のアーティファクトに添付できる。

---

# 低リソースとリモート

- **`--lite`** は1つのフラグで重いサブシステム（スナップショット、リポマップ、並列）を
  オフにし、制約のあるノードで小さなフットプリントにする。
- **SSH + tmux**——GPU ボックスで長い `--auto` 実行を開始し、デタッチし、後で
  再アタッチ（`docs/REMOTE_SSH.md`、`docs/TMUX.md`）。
- **デーモン**——ラボサーバ上のウォームなプロセスが、毎回 config/index を再読み込み
  せずに多数の短いリクエストを処理する。

---

# プライバシーとデータガバナンス

- **自ホストの**モデルに対して動くので、学生のコードや研究データが機関を出る必要は
  一切ない。
- **パスフェンス**と**編集スコープ**が自律実行の触れる範囲を限定する；**参照ルート**は
  書き込みリスクなしに共有コーパスへの読み取り専用アクセスを許す。
- シークレットは `apiKeyEnv` 環境変数から来る——config に書かれることはなく、ログ
  からは削除される。

---

# 具体的なシラバスの一コマ

> *「第9週：エージェント的ツール。」* 学生は `jc_agent_run_turn` を読み、レジストリの
> 背後に新しい組み込みツールを追加し、モデルがそれを呼ぶのを見る。ループ全体はほぼ
> 1つの関数；ツールインターフェースは構造体だ。「AI エージェント」の神秘を解く。

---

<!-- _class: lead -->

# ここから始める

```sh
jichi setup --preset developer
# teaching:
jichi init assignments   # + "assignments": true in config
```

`docs/TUTORIAL_ADVANCED.md`、`docs/TEACHING_ASSIGNMENTS.md`、`docs/LOW_MEMORY.md`。
