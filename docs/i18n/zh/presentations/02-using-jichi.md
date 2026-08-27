---
marp: true
title: 使用 jichi
theme: default
paginate: true
---
<!-- tracks: ../../../presentations/02-using-jichi.md @ 0632b94 -->
<!-- 注意：本翻译为机器初译，以英文原版 ../../../presentations/02-using-jichi.md 为准。 -->

<!-- _class: lead -->

# 使用 jichi

### 从第一次运行到自主任务

---

# 一条命令完成设置

```sh
jichi setup                      # 交互式向导
# 或者，在项目中复用你的全局 config：
jichi setup --from-global --preset developer
# 或者，接手一个不熟悉的仓库（仅提议式分析 + 教程草稿）：
jichi setup --onboard
jichi --config local/config.json doctor   # 验证一切
```

`doctor` 检查 libcurl、config、模型、密钥、可达性、git、MCP、LSP，以及
你的项目资产。

---

# 运行它的三种方式

| 界面 | 命令 | 何时用 |
|---|---|---|
| **TUI** | `jichi` | 交互式工作、评审、一键批准。 |
| **Headless** | `jichi -p "…"` | 脚本、CI、自动化、SSH。 |
| **结构化** | `jichi -p "…" --output jsonl` | 另一个程序/代理来驱动它。 |

当 prompt 为 `-` 时读取 stdin，所以一整个文件可以作为 prompt，无
`ARG_MAX` 限制。

---

# 模式：给多长的缰绳？

- **`/chat`**——常规；在做变更动作前会问。
- **`/plan`**——只读；调查并提议，不做编辑。
- **`/auto`**——自主；不经提示地运行其沙盒化工具。

```sh
jichi --plan -p "how would you add feature X?"    # 一份计划
jichi --auto -p "add feature X and make tests pass"  # 直接干
```

<!-- Plan 模式是探索一个不熟悉或共享仓库时的安全默认。 -->

---

# 它拥有的工具

- **文件：** `read_file`、`write_file`、`edit_file`、`apply_patch`（原子
  多编辑）、`list_files`、`search_code`。
- **运行：** `run_terminal_command`（+ 后台）、`run_tests`。
- **知识：** `codebase_search`、`search_docs`、`fetch_url`、`web_search`。
- **Git：** status/diff/log/blame + add/commit/branch/stash。
- **导航/重构：** LSP `find_definition`/`references`/`symbols`、rename、format。
- **委派：** `spawn_subagent`、`spawn_parallel`。
- **媒体与声音：** `generate_image`、`generate_audio`、`transcribe_audio`、
  `play_audio`、`record_audio`。

---

# 你带进一个回合的上下文

在一条普通消息中的 `@`-引用把上下文拉进来：

```
review @src/parser.c against @diff and the @rss:https://…/releases.xml feed
explain @sym:jc_agent_run_turn and @folder:src/net
```

`@file @diff @url @rss @sym @docs @problems @folder @mcp @audio @img`——每一个
都解析成一个有界的块，追加到你的消息里。

---

# 当一个会话变长时

- **自动压缩**把旧前缀摘要化，以留在窗口内；**回合内压缩**在单个失控回合上
  裁剪繁重的工具输出。
- **Token 校准**学习每个模型真实的每 token 字节数，让估算不再一味乐观。
- `/context` 显示实时的预算分解；`/cost` 显示累计花费。

你多半根本不会去想它——这正是重点。

---

# 安全地做自主运行

```sh
jichi --auto \
  --verify "make test" \
  --budget-tokens 500k --deadline 30m \
  --edit-scope "src/**" \
  --journal run.jsonl --control \
  -p "fix the failing ring-buffer tests"
```

通过 → 前进；失败 → 修正 N 次，否则回退到上一个绿色点。
一切都在日志里。

实时引导它：`jichi control <sock> status | inject "…" | pause | abort`。
读取回来：`jichi runs` 和 `jichi audit`（两者都支持 `--output json`）。

---

# 在你的编辑器里

- **Emacs**（`jichi.el`）、**Vim/Neovim**（`jichi.vim`）、**nano**（`jichi-nano`）——
  全部通过 headless 契约。
- **Zed / 任何 ACP 编辑器**——`jichi serve`（细粒度批准、流式）。

```vim
:JichiAsk how does the fence work?    " 在一个 scratch 分屏中回答
:'<,'>JichiRegion tidy this           " 原地变换一段选区
:JichiTask add a test for edge case Y " agentic，先确认
```

---

<!-- _class: lead -->

# 顺手的命令

`/model` `/mode` `/diff` `/undo` `/rewind` `/compact` `/context` `/cost`
`/skills` `/mcp` `/export` `/fork` `/sessions`

`export` 一份记录用于 PR 或一堂课；`fork` 去探索一个分支而不丢失你所在的位置。
