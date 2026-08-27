---
marp: true
title: jichi 在大学
theme: default
paginate: true
---
<!-- tracks: ../../../presentations/04-university.md @ 0632b94 -->
<!-- slides-behind: 2 (en has 12 slide separators, this has 10). The English deck
     gained slides this translation does not carry. Translating them is prose in a
     language this repository cannot review, so the gap is DECLARED rather than
     filled -- and the numbers above are checked, so the declaration cannot be left
     behind silently either. M582. -->
<!-- 注意：本翻译为机器初译，以英文原版 ../../../presentations/04-university.md 为准。 -->

<!-- _class: lead -->

# jichi 在大学

### 研究、课程作业、可复现性

---

# 为什么它适合学术环境

- **在你现有的硬件上运行**——一个共享登录节点、一台老旧的实验室机器、一个
  树莓派、一部装了 Termux 的手机。约 1.2 MB，约 10–17 MB RSS，与远程或
  本地模型通信。
- **无供应商锁定**——把它指向一台院系 LLM 服务器、一个本地
  llama.cpp/vLLM，或一个商业 API。无需密钥的本地服务器开箱即用。
- **可审计**——它是带测试套件的 C89；学生可以*读*这个代理，而不只是用它。
- **成本感知**——prompt 缓存 + 实时 `/cost`；预算给一次自主运行封顶。

---

# 用于研究软件

- **快速理解一个遗留代码库**——`@folder:` 地图 + `codebase_search` +
  在论文参考材料上的 `search_docs`。
- **可复现的运行**——headless `-p` + `--output json`，由一个 Makefile
  或一个 Slurm 作业驱动；JSONL 日志就是你的溯源记录。
- **带缰绳的自主**——`--auto --verify "make test" --budget-*`，让一次
  通宵重构不会失控或悄悄弄坏构建。
- **案例研究：zigodot 重写**——一次完全由 jichi 驱动的大型、自主语言移植，
  遥测精确显示了工作花在了哪里。
- **具身 / 机器人研究**——jichi 作为一个机器人的*思辨*层
  （传感器 + 执行器作为工具、一道动能安全关卡、声音 I/O）；一个无硬件的
  模拟器随 `examples/robot-sim/` 一同发布（`docs/ROBOTICS.md`）。
- **把软件工程与 PM 当作一门科目**——`docs/PROJECT_TIMELINE.md` 是一份
  以数据为依据的回顾（阶段、强度、四种交付模式，含一名开发者 + AI），
  可用于项目管理或 SE 课程。

---

# 用于教一门课

- **assignments** 特性：教师撰写题目 + 评分标准 + 一道提示阶梯；参考解答被
  扣留；评分是**只读**且以评分标准为键的。（参见 `docs/TEACHING_ASSIGNMENTS.md`。）
- **各 TA 之间评分一致**——同一份评分标准 + 参考 + 只读检查器，意味着更少的
  评分者间差异。
- **代理是可读的**——一道"代理循环是如何工作的？"的作业可以指向*这个*代码库的
  `jc_agent_run_turn`。

---

# 可复现性与溯源

```sh
jichi --auto \
  --verify "pytest -q" \
  --budget-tokens 300k \
  --journal artifacts/run-$(date +%s).jsonl \
  --output json \
  -p "implement the FFT variant from the spec and pass the tests" \
  > artifacts/result.json
```

每一次模型调用、工具调用、成本和结果都被捕获——把它附到实验记录本
或论文的制品里。

---

# 低资源与远程

- **`--lite`** 用一个标志关掉繁重的子系统（快照、repo 地图、并行），
  在受限节点上得到微小的占用。
- **SSH + tmux**——在 GPU 机器上启动一次长时间 `--auto` 运行，脱离，
  稍后重新接入（`docs/REMOTE_SSH.md`、`docs/TMUX.md`）。
- **守护进程**——实验室服务器上的一个热进程服务许多快速请求，无需每次
  重新加载 config/index。

---

# 隐私与数据治理

- 对着一个**自托管**模型运行，所以学生代码和研究数据永不需要离开机构。
- **路径围栏**和**编辑范围**约束一次自主运行能碰什么；**参考根**允许对
  共享语料的只读访问而无写入风险。
- 密钥来自 `apiKeyEnv` 环境变量——从不写入 config，从日志中被脱敏。

---

# 一个具体的教学大纲位置

> *"第 9 周：agentic 工具。"* 学生阅读 `jc_agent_run_turn`，在注册表后面加一个
> 新的内置工具，并看着模型调用它。整个循环约是*一个*函数；工具接口是一个 struct。
> 它把"AI 代理"祛魅了。

---

<!-- _class: lead -->

# 从这里开始

```sh
jichi setup --preset developer
# 教学：
jichi init assignments   # + 在 config 中加 "assignments": true
```

`docs/TUTORIAL_ADVANCED.md`、`docs/TEACHING_ASSIGNMENTS.md`、`docs/LOW_MEMORY.md`。
