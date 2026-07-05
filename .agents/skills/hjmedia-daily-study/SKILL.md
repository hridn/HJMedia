---
name: hjmedia-daily-study
description: Guide daily learning sessions for the Huajiao HJMedia C++ multimedia repository. Use when the user asks to start, continue, review, complete, or check a numbered HJMedia study day, asks for related demo and note outputs, says "我要开始第N天的学习", "继续学习", "今天学什么", "帮我完成相关demo和note", "给demo加中文注释", asks for Mermaid data-flow/control-flow diagrams, or wants interview-ready summaries from the HJMedia daily study plan.
---

# HJMedia Daily Study

Use this skill to run the Huajiao HJMedia learning plan as an execution loop, not passive reading. Each session should leave reviewable artifacts in the repository.

## Core Loop

Follow this sequence for every day:

```text
阅读源码/文档 -> 梳理链路或状态 -> 完成 demo/排查方案 -> 写 note -> 验证 -> 面试复述
```

When the user asks to "完成", "补齐", "生成", "加注释", or otherwise requests an implementation outcome, directly edit the demo/note files. Do not stop at a plan unless the user explicitly asks only for planning.

## Select The Day

- If the user names a day, use that day.
- If the user says "继续" or "今天", inspect `study/`, `studyNote/`, and `studyDemo/` to infer the next incomplete day.
- If a demo or note already exists, preserve useful content and fill gaps instead of replacing it blindly.
- Read `references/28-day-plan.md` for the selected day's reading list, practice task, outputs, and acceptance check.

## Daily Workflow

1. Read the selected day from `references/28-day-plan.md`.
2. Read only the repository files needed for the day. Prefer `rg` for discovery.
3. Briefly tell the user the day goal and what you are inspecting.
4. Create or update the expected artifacts:
   - demos under `studyDemo/dayXX_*.cpp`;
   - notes under `studyNote/*.md`;
   - weekly rollups under `studyNote/week*-*.md` when relevant.
5. Match existing style:
   - standalone C++17 demos;
   - use `study_demo_common.h` when it fits;
   - keep production HJMedia source untouched unless explicitly requested.
6. For every newly written or substantially updated demo, add Chinese comments that explain the learning point, key control/data flow, and the corresponding HJMedia source semantics.
7. For every newly written or substantially updated note, add Mermaid diagrams for both data flow and control flow.
8. Build and run the day demo when practical. If `cmake` is not on PATH, try `C:\Program Files\JetBrains\CLion 2026.1.2\bin\cmake\win\x64\bin\cmake.exe`.
9. Finish with changed files, verification result, and a concise interview-ready explanation.

## Mermaid Diagram Requirements

Every daily note created or substantially updated by this skill must include a `## Mermaid 图` section with both diagrams:

````markdown
## Mermaid 图

### 数据流

```mermaid
flowchart LR
```

### 控制流

```mermaid
sequenceDiagram
```
````

- The data-flow diagram must show real HJMedia data movement: frames, packets, queues, plugins, nodes, graph stages, muxer paths, or source/sink paths.
- The control-flow diagram must show real HJMedia control movement: API calls, lifecycle calls, init/start/process/stop/release, seek/flush/EOF, scheduler/handler dispatch, callbacks, locks, state transitions, or error notification propagation.
- Prefer `flowchart LR` or `flowchart TD` for pipeline/data movement, `sequenceDiagram` for API/callback/thread interactions, and `stateDiagram-v2` for lifecycle/state-machine topics.
- Use concrete source names in node labels, such as `HJGraphPusher::internalInit`, `HJPluginAVInterleave::runTask`, `HJMediaNode::flush`, or `HJPusherNapi::openPusher`. Avoid vague labels like "module A" or "process".
- Keep diagrams small enough to review in Markdown. If a day has a broken/fixed debugging scenario, include the fixed path in the main diagram and mention the broken path in labels or notes.
- If an existing note already has plain-text data/control flow, convert or supplement it with corresponding Mermaid diagrams instead of leaving only prose.

## Finished Artifact Checklist

A daily result is complete only when it includes:

- exact source/doc paths read;
- a data flow, control flow, lifecycle, queue, thread, or state explanation;
- a Mermaid data-flow diagram and a Mermaid control-flow diagram in the note;
- a demo, pseudocode, logging plan, comparison table, or debugging playbook;
- a note file with source entries, observations, risks, and conclusion;
- compile/run verification or a concrete reason it could not be run;
- Chinese comments in every newly written or substantially updated demo, explaining why the demo exists and how it maps to HJMedia source behavior;
- one answer the user can say in an interview without overstating ownership.

## Debugging Day Standard

For seek/flush/EOF, teardown, queue backlog, weak network, and similar problem-location days, require these sections:

```text
现象
可疑模块
源码入口
日志点
预期现象
可能原因
修复思路
新风险
验证方式
面试复述
```

Prefer a small broken/fixed simulation. Day 13 is the canonical pattern: `studyDemo/day13_seek_flush_eof_debug.cpp` compares stale frames and stale EOF against preFlush, full-chain flush, timeline reset, and generation gating.

## Quality Rules

- Keep scope to the selected day.
- Tie every concept to a real HJMedia path, class, function, graph, plugin, or demo.
- Use small concrete examples before framework-wide explanation.
- Preserve user edits and unrelated dirty worktree changes.
- For interview preparation, phrase the project as source analysis, architecture walkthrough, debugging practice, and small C++ validation demos.
- Do not claim the user independently built all of HJMedia.

## References

- `references/28-day-plan.md`: daily goals, practice tasks, output files, and acceptance checks.
- For MusicPlayer days, read repository docs as needed in this order: `docs/Readme_MusicPlayer.md`, `docs/architecture/HJGraphMusicPlayer.md`, `docs/architecture/HJGraphMusicPlayer_AudioContextGuide.md`.
- For plugin days, start with `src/plugins/doc/README.md` when present, then read the specific plugin docs named by the plan.
