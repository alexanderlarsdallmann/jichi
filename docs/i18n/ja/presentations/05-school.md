---
marp: true
title: 学校での jichi
theme: default
paginate: true
---
<!-- tracks: ../../../presentations/05-school.md @ 0632b94 -->
<!-- slides-behind: 1 (en has 11 slide separators, this has 10). The English deck
     gained slides this translation does not carry. Translating them is prose in a
     language this repository cannot review, so the gap is DECLARED rather than
     filled -- and the numbers above are checked, so the declaration cannot be left
     behind silently either. M582. -->
<!-- 注意: この翻訳は機械下訳です。英語版 ../../../presentations/05-school.md を正とします。 -->

<!-- _class: lead -->

# 教室での jichi

### エージェント*とともに*、安全にコードを学ぶ

---

# 名指しされた不安

> 「AI がコードを書くなら、学生は何か学ぶのか？」

jichi の課題機能はその逆のために設計されている：エージェントは**ダイヤルつきのコーチ**で
あって、答えの機械ではない。学習者が作業を行う；助けは調整可能な量であり、評価基準は
見える。

---

# レッスンはどう進むか

```
teacher: /assign implementation "a function that reverses a list"
         → docs/assignments/reverse-list.md  (brief + rubric + hint ladder)
student: works it in the editor; stuck? → one hint at a time (hint tool)
         still stuck? → a focused question (ask_for_help)
teacher: /check reverse-list.md student.py  → rubric-keyed feedback (read-only)
```

参照解答は**伏せられる**；ルーブリックは**示される**。

---

# ヒントの階段こそが教育法

- 行き詰まりが**壁**ではなく**ダイヤル**になる。
- ヒントは**段階付け**される——最初は最も穏やかな一押し、最後に完全なネタバレ。
- 学習者は「もがく予算」を生産的に使う。
- `ask_for_help` は*特定の*混乱に的を絞った答えを与えるのであって、宿題を代行させる
  プロンプトではない。

> 年齢/レベルごとに階段を調整せよ。その調整*こそが*教えることである。

---

# 教室で安全にするガードレール

- **Plan / 読み取り専用モード**——学生は共有材料を変えずに探索する。
- **パスフェンス**——エージェントはレッスンフォルダの外のファイルに触れられない。
- **読み取り専用採点**——`solution-checker` は学生のコードを決して編集しない。
- **propose-only な作成**——教員が配布前にすべての課題と参照を承認する。
- **自ホストのモデル**——子どもたちの作業は学校のサーバに留まる。

---

# 段階的な学習者が良い習慣をモデル化する

4つのプロファイルが、あるレベルで問題に*どう*取り組むかを示す：

- **`learner-junior`**——ヒント、助け、委譲に頼る。
- **`learner-student`**——参照を使い、ほどほどの助け。
- **`learner-senior`**——最小限の助け、まず計画する。
- **`learner-agent`**——戦略的で効率的。機械向けの段階。

ある段階が問題に取り組むのを見ることは、答えだけでなく*戦略*についてのクラスの
会話の種になる。

---

# 教員が実際にすること

1. 課題の小さなセットを一度作成する（`/assign`、ドラフトをレビュー）。
2. ブリーフを配布する（`.solution.md` ファイルは手元に残す）。
3. 巡回する；学生が行き詰まったら、答えではなく*次のヒント*へ導く。
4. あなたがレビューする一貫した初回採点のために提出物を `/check`。

学ぶべき新しいツールはない——同じ `jichi` バイナリだ。

---

# 課題を超えて

- **「これを説明して」**——エディタで `:JichiExplain` スニペット、または `jichi-nano
  explain file.py`——辛抱強い、オンデマンドの説明役。
- **レッスンとしてのコードレビュー**——`/check` やレビューエージェントが提出物を、
  ラベル付き・根拠に基づくフィードバックに変える。
- **Chromebook クラスのボックスで**、ネットワーク越しに学校の LLM サーバへ動く。

---

<!-- _class: lead -->

# 始め方（教員）

```sh
jichi init assignments        # scaffold the pack
# add  "assignments": true  to config, then:
jichi -p "/assign implementation 'sum a list'"
```

完全な手引き：`docs/TEACHING_ASSIGNMENTS.md`（教室、個別指導、自習、コホート/TA）。
