---
marp: true
title: jichi 在中小学
theme: default
paginate: true
---
<!-- tracks: ../../../presentations/05-school.md @ 0632b94 -->
<!-- slides-behind: 1 (en has 11 slide separators, this has 10). The English deck
     gained slides this translation does not carry. Translating them is prose in a
     language this repository cannot review, so the gap is DECLARED rather than
     filled -- and the numbers above are checked, so the declaration cannot be left
     behind silently either. M582. -->
<!-- 注意：本翻译为机器初译，以英文原版 ../../../presentations/05-school.md 为准。 -->

<!-- _class: lead -->

# jichi 在课堂

### 安全地*与*一个代理一起学编程

---

# 把担忧说出来

> "如果 AI 写代码，学生还能学到什么吗？"

jichi 的 assignments 特性正是为相反的目的而设计：代理是一个**带旋钮的教练**，
而不是一台答案机器。学习者做工作；帮助是可调的量，评估标准是可见的。

---

# 一堂课如何进行

```
teacher: /assign implementation "a function that reverses a list"
         → docs/assignments/reverse-list.md  (brief + rubric + hint ladder)
student: works it in the editor; stuck? → one hint at a time (hint tool)
         still stuck? → a focused question (ask_for_help)
teacher: /check reverse-list.md student.py  → rubric-keyed feedback (read-only)
```

参考解答被**扣留**；评分标准被**展示**。

---

# 提示阶梯就是教学法

- 卡住变成一个**旋钮**，而不是一堵墙。
- 提示是**分级的**——最轻的一推在先，完整的剧透在最后。
- 学习者把他们的"挣扎预算"用得富有成效。
- `ask_for_help` 对一个*具体*的困惑给出有针对性的答案，而不是一个
  替我做作业的 prompt。

> 按年龄/水平调节这道阶梯。那种调节*就是*教学。

---

# 让它在课堂上安全的护栏

- **Plan / 只读模式**——学生探索而不改动共享材料。
- **路径围栏**——代理无法碰课程文件夹之外的文件。
- **只读评分**——`solution-checker` 从不编辑学生的代码。
- **仅提议式撰写**——教师在每份作业和参考被发出之前批准它。
- **自托管模型**——孩子们的作业留在学校的服务器上。

---

# 分层学习者示范好习惯

四个画像展示了在某个水平上*如何*着手一个问题：

- **`learner-junior`**——倚重提示、帮助和委派。
- **`learner-student`**——使用参考、适度帮助。
- **`learner-senior`**——最少帮助，先做计划。
- **`learner-agent`**——讲究策略、注重效率；面向智能体的层级。

看一个层级去攻克一个问题，能激发一场关于*策略*而非仅仅答案的课堂讨论。

---

# 教师实际做什么

1. 一次性撰写一小组作业（`/assign`，评审草稿）。
2. 发出题目（把 `.solution.md` 文件留下）。
3. 巡场；当一个学生卡住时，把他们引导到*下一个提示*，而不是答案。
4. 对提交 `/check` 得到一个一致的初评，供你复核。

没有新工具要学——就是同一个 `jichi` 二进制。

---

# 超越 assignments

- **"解释这个"**——在编辑器里 `:JichiExplain` 一段代码，或 `jichi-nano
  explain file.py`——一个耐心的、按需的讲解者。
- **代码评审作为一堂课**——`/check` 或一个 review 代理把一份提交变成
  带标签、有证据支撑的反馈。
- **能在 Chromebook 级别的机器上运行**，通过网络连到一台学校 LLM 服务器。

---

<!-- _class: lead -->

# 开始（教师）

```sh
jichi init assignments        # 搭起这个 pack
# 在 config 中加  "assignments": true ，然后：
jichi -p "/assign implementation 'sum a list'"
```

完整演练：`docs/TEACHING_ASSIGNMENTS.md`（课堂、辅导、
自学、班级/TA）。
