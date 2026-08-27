---
marp: true
title: jichi — 超能力特性
theme: default
paginate: true
---
<!-- tracks: ../../../presentations/00-super-features.md @ 0632b94 -->
<!-- slides-behind: 1 (en has 12 slide separators, this has 11). The English deck
     gained slides this translation does not carry. Translating them is prose in a
     language this repository cannot review, so the gap is DECLARED rather than
     filled -- and the numbers above are checked, so the declaration cannot be left
     behind silently either. M582. -->
<!-- 注意：本翻译为机器初译，以英文原版 ../../../presentations/00-super-features.md 为准。 -->

<!-- _class: lead -->

# jichi

### 一个用 **~C89** 写成的 AI 编程代理，单个小巧二进制

对 Continue CLI 的从零重写：聊天、agentic 工具循环、RAG、
自主性、MCP、LSP、一个 TUI——用可移植的 ANSI C 实现，无运行时。

<!-- 开场：卖点是"一个完整的现代编程代理，装进 ~1.2 MB，在任何有 POSIX +
libcurl 的地方都能跑。" -->

---

# 会让人吃惊的十件事

1. 它是 **C89**。在 `-std=c89 -pedantic -Wall -Wextra` 下零警告。
2. 二进制约 **1.2 MB**；一次 headless 回合驻留在 **约 10–17 MB RSS**。
3. 它会**编辑你的代码**——弹性/模糊的多文件补丁，带 diff。
4. 它带护栏**自主运行**——运行中可暂停/可引导。
5. 它做 **RAG**——在你的仓库*和*文档上做 BM25 + 嵌入 + rerank 的混合检索。
6. 它讲 **MCP**（客户端）*也*讲 **ACP**（服务端，供 Zed 这类编辑器用）。
7. 它有 **LSP** 导航*和*重构（rename/format/code-actions）。
8. 它生成**图像和语音**，并能**播放/录制**音频。
9. 未经授权它不会 `sudo` 或驱动**马达**——每次尝试都被审计。
10. 它为每次改动**打检查点**，并能在 **树莓派或手机**（Termux）上运行。

---

# 一口气讲完代理循环

```
history + system + tools
  → provider.build_request()      (Anthropic 或 OpenAI，流式)
  → HTTP/SSE                      (libcurl)
  → execute tool calls           (read/edit/run/search/git/…)
  → append results, loop         直到得到最终答案
```

一个循环，两个提供方，约 35 个内置工具，且它从不因提供方而分支。

<!-- 整个东西就是一个函数 jc_agent_run_turn；其余一切要么是工具，要么是
提供方 vtable。 -->

---

# 你可以放心交给它的护栏

- **模式：** chat（会问）、plan（只读）、auto（自主）。
- **路径围栏：** 文件工具无法逃出工作区；读取可延伸到命名的参考根，写入永远不行。
- **自主性封套：** token/时钟/工具调用预算、一道编辑范围围栏、一道
  **验证关卡**（跑你的测试），以及一份 JSONL 审计日志——外加可选启用的
  对编辑范围之外由 shell 引入的改动的**自动回退**。
- **多文件编辑信守承诺：** `apply_patch` 做全有或全无的校验，且若某次写入中途
  失败，会回退已写入的文件并报告每个文件的状态。
- **快照：** 一个影子 git 仓库在首次编辑前打检查点——`/undo`
  和 `/rewind` 恢复文件*和*对话。

> 任务进行到一半预算耗尽？它会验证一次并保留通过的工作；只有*红色*树才回退。
> 部分进展不会被丢弃。

---

# 它也能向*上*扩展

- **子代理**——委派一个范围明确的子任务（自己的历史、模型、工具围栏），
  现在默认两层深，带按深度递减的预算。
- **并行代理**——一个 fork 池，每个任务在隔离的 git worktree 中，按文件级
  先到先得合并。
- **守护进程**——一个热进程让 config/MCP/LSP/index 保持就绪，并通过 socket
  服务请求，带一个**有界的工作者池** + 按请求的看门狗。
- **循环与舰队**——一个监督者抽干任务队列（tmux/systemd/cron）；一个协调者
  在对等实例间通过 SSH + MCP 分派工作。

<!-- 这正是让 zigodot 重写可行的东西：扇出、隔离、合并。 -->

---

# 真实自用：zigodot 重写

jichi 正在驱动一次**大型、自主的 Godot → Zig 移植**——那颗北极星测试。

- 在封套下、在真实代码库上的长时间 `--auto` 运行。
- 遥测 + 按会话的时间线显示 token/成本实际去了哪里。
- **学习循环**把它自己的日志反馈为持久的教训，让它不再重犯错误。
- 每一个教会我们东西的战地故事都记在 `docs/ANECDOTES.md`。

那些难缠的部分（上下文溢出、无缓存的经济学、隔离 bug）是靠*使用*它发现的，
而不是靠空想。

---

# 在你工作的地方与你相遇

- **终端：** 一个真正的 TUI——markdown + 语法高亮、实时 diff、一键批准、
  `/cost`、`/context`、`/undo`。
- **Headless：** `jichi -p "…"`；`--output json`/`jsonl` 供自动化用。
- **编辑器：** Emacs、Vim/Neovim、nano，以及任何 **ACP** 编辑器（Zed）。
- **远程：** SSH + tmux，用于在 GPU/CI 机器上做长时间自主运行。
- **你的语言：** `"language": "日本語"` 它就用它回答——批准提示随之跟进
  （en/de/es/ja/zh）；上手文档提供全部五种语言。

一个二进制，每一处界面，底下同一套契约。

---

# 本地、私有、便宜

- 把它指向**任何 OpenAI 兼容端点**——LM Studio、llama.cpp、LocalAI、
  vLLM。无供应商锁定；无需密钥的本地服务器开箱即用。
- **Prompt 缓存**（两个提供方）+ 缓存感知的成本核算。
- **媒体生成**对着一个本地 **LocalAI** 二进制——已在消费级 GPU 上验证图像生成，
  无需 Docker。
- 在容器跑不了的地方运行：嵌入式、低内存、类气隙环境。

---

<!-- _class: lead -->

# 为什么是 C89？

因为"可移植、小巧、依赖极少、十年后仍然在此"本身就是一项特性——
为了嵌入式目标，为了教学，也为了信任。

**依赖：** libcurl + 一个内置 JSON 库。就这些。

---

<!-- _class: lead -->

# 试试看

```sh
make && ./jichi setup            # 引导式设置
./jichi -p "explain this repo"   # headless
./jichi                          # TUI
```

文档：`README.md`、`docs/ROADMAP.md`（主题化索引）、`docs/AGENTS_GUIDE.md`。
