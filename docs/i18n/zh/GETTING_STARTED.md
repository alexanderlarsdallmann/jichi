<!-- tracks: ../en/GETTING_STARTED.md (canonical) -->
<!-- 注意：本翻译为机器初译，欢迎母语者校对。 -->
> ⚠️ 本翻译为初稿。术语的准确性请以英文版（`en/`）为准。

# jichi 入门

jichi 是一个用于 Linux 的命令行 AI 编程代理。本页带你从零开始建立一个可用的
会话。更深入的指南（见下方链接）为英文。

## 1. 安装

你需要 **libcurl** 和一个 C 编译器。构建两个可执行文件：

```sh
make
```

这会生成 `jichi`（代理）和 `jichi-convert`（配置导入工具）。系统要求见
[INSTALL.md](../../INSTALL.md)。

## 2. 配置

运行引导式设置——它会检测你项目的编程语言并写入配置：

```sh
jichi setup
```

若想要一个最小且安全的起点（适合学习），加上 `--profile beginner`。你的 API 密钥
从**环境变量**读取（绝不写入配置文件）。

检查是否一切就绪：

```sh
jichi doctor      # 健康检查
jichi benchmark   # 最佳实践覆盖度评分
```

jichi 可以用你的语言回答：在配置中加入 `"language": "中文"`，或传入
`--language` —— 参见 [LANGUAGE.md](../../LANGUAGE.md)。

## 3. 使用

交互式会话（REPL）：

```sh
jichi
```

用自然语言输入你的请求。用 `@file` 或 `@sym:name` 指向代码。常用命令：`/help`、
`/model`、`/mode`、`/constraints`、`/undo`。按 **Ctrl-C** 可停止当前任务而不退出
程序。

无界面模式（用于脚本与自动化）：

```sh
jichi -p "解释 src/main.c 的作用"
```

## 4. 保持掌控

- **模式：** chat（行动前询问）、plan（只读）、auto（自主）。
- **约束 (constraints)：** 说“不要运行构建”，它会被*强制执行*，而不仅仅是记住——
  参见 [CONSTRAINTS.md](../../CONSTRAINTS.md)。
- **撤销：** 每次更改都会创建检查点；`/undo` 可将其回退——参见
  [SNAPSHOTS.md](../../SNAPSHOTS.md)。

## 下一步

- 从第一步到精通的完整路线：[JOURNEY.md](JOURNEY.md)
- 选择你的路径：[WORKFLOWS.md](../../WORKFLOWS.md)
- 新手教程：[TUTORIAL_BEGINNER.md](../../TUTORIAL_BEGINNER.md)
- 进阶子系统：[TUTORIAL_ADVANCED.md](../../TUTORIAL_ADVANCED.md)
