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

## Source-Evidence Contract

Treat source code as the authority for every architecture, data-flow, control-flow, lifecycle, and behavior claim in a study note. Documentation, names, and general multimedia knowledge may help locate code, but must not establish a conclusion on their own.

Before writing a conclusion or diagram:

1. Locate the implementation with `rg`, then read the relevant function/class body and its caller or graph connection. Do not infer a call, edge, default, thread, or ownership rule from a filename, class name, comment, or declaration alone.
2. Record evidence as `path + symbol` and include a line number when it makes review easier. For a cross-layer statement, trace every required boundary (for example TS/NAPI -> entry -> graph -> component), not just one endpoint.
3. Classify statements precisely:
   - **源码确认**: directly shown by executable code or an active graph connection.
   - **条件路径**: shown only behind a platform macro, feature switch, disabled node, configuration, or error branch; state the condition.
   - **待验证**: plausible but not traced; do not put it in the main flow or phrase it as fact. State the missing source evidence and the next file/symbol to inspect.
4. If the evidence cannot be found, say “源码未确认” rather than completing the story from domain knowledge. Ask the user for missing context only when that prevents useful source inspection.

## Evidence-First Notes and Diagrams

For every new or substantially changed technical explanation, add a compact `## 源码依据` section (or update the existing one) before the conclusion. Use a table or bullets that map each important claim to its evidence:

```markdown
## 源码依据

- `src/path/File.cpp` — `Class::method`: establishes <specific observed behavior>.
- `src/path/Graph.cpp` — `connectCom(A, B)`: establishes the A -> B edge under <condition>.
```

Apply these rules to Mermaid diagrams:

- Derive every node and primary edge from a concrete call such as `connect`, `connectCom`, `deliver`, `pop`, `process`, callback registration/invocation, or a verified data member handoff.
- Label optional/conditional edges with the actual macro, configuration key, enable flag, or API condition. Never present a disabled or platform-specific path as the universal default.
- Separate side channels (callbacks, metadata, PBO readback, control messages) from the media-frame path. Do not draw a result as if it rewrites a frame unless code performs that write.
- Keep a source path or symbol in each important node label, caption, or adjacent evidence list so a reviewer can audit the graph without guessing.
- Do not invent ordering from visual layout. State ordering only when a call chain, graph connection order, scheduler rule, or synchronization primitive proves it.

Use the following wording discipline in notes and final answers:

- Write “源码显示/确认” only with recorded evidence.
- Write “在默认图中” only after reading the graph construction code and its enable/configuration values.
- Write “可能/待验证” for an incomplete trace, and omit it from interview-ready conclusions unless the uncertainty itself is relevant.

## Select The Day

- If the user names a day, use that day.
- If the user says "继续" or "今天", inspect `study/`, `studyNote/`, and `studyDemo/` to infer the next incomplete day.
- If a demo or note already exists, preserve useful content and fill gaps instead of replacing it blindly.
- Read `references/28-day-plan.md` for the selected day's reading list, practice task, outputs, and acceptance check.

## Daily Workflow

1. Read the selected day from `references/28-day-plan.md`.
2. Read only the repository files needed for the day. Prefer `rg` for discovery; for every important claim, read both the defining implementation and enough caller/connection code to establish how it is reached.
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
7. For every newly written or substantially updated note, add Mermaid diagrams for both data flow and control flow, derived from recorded source evidence. Mark conditional edges and preserve frame paths versus metadata/control paths.
8. For every newly written or substantially updated note, include a `## 问题解答` section that records the user's study questions and the answers given during the session; when answering follow-up questions for an existing day, update that day's note before the final response.
9. Before finalizing, audit the note: every key claim and every primary Mermaid edge must have a source path and symbol in `源码依据` or immediately adjacent text. Use `rg -n` to re-open the cited symbols; remove or relabel unsupported claims.
10. Build and run the day demo when practical. If `cmake` is not on PATH, try `C:\Program Files\JetBrains\CLion 2026.1.2\bin\cmake\win\x64\bin\cmake.exe`.
11. Finish with changed files, verification result, the source evidence reviewed, and a concise interview-ready explanation that contains no unverified claim.
12. After finishing edits, add every file modified by this skill to VCS with `git add <path>...`; scope this to the files changed in the current study task and do not stage unrelated existing changes.

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
- Cite the source path and symbol that proves each diagram's primary path in the note's `源码依据`; inspect `connect`/`connectCom` or the actual callback/data handoff before drawing an edge.
- Draw optional, disabled, platform-gated, and configuration-gated paths as explicitly conditional. Do not convert a source comment into a runtime fact without checking the surrounding active code.
- Prefer `flowchart LR` or `flowchart TD` for pipeline/data movement, `sequenceDiagram` for API/callback/thread interactions, and `stateDiagram-v2` for lifecycle/state-machine topics.
- Use concrete source names in node labels, such as `HJGraphPusher::internalInit`, `HJPluginAVInterleave::runTask`, `HJMediaNode::flush`, or `HJPusherNapi::openPusher`. Avoid vague labels like "module A" or "process".
- Keep diagrams small enough to review in Markdown. If a day has a broken/fixed debugging scenario, include the fixed path in the main diagram and mention the broken path in labels or notes.
- If an existing note already has plain-text data/control flow, convert or supplement it with corresponding Mermaid diagrams instead of leaving only prose.

## Question Answer Requirements

Every daily note created or substantially updated by this skill must include a `## 问题解答` section.

- Use this section to record the user's questions asked during the learning session and the answers given by Codex.
- Add new Q&A entries incrementally when the user asks follow-up questions about the day's topic.
- For follow-up questions about an existing day, read the current day note before answering, then append or update the answer inside the existing `## 问题解答` section.
- If the full explanation is added elsewhere in the note, still add a concise Q&A entry under `## 问题解答` that points to the detailed section.
- Keep each question as a concrete heading, such as `### FLV 如何区分音频帧和视频帧？`.
- Keep answers tied to real HJMedia source paths, classes, functions, diagrams, demos, or terminology when possible.
- If the note already has a `## 问题解答` section, append or update entries there instead of creating a duplicate section.
- Before finishing, verify with `rg -n "问题解答|<question keywords>" studyNote/<day-note>.md` or an equivalent check that the Q&A entry exists under `## 问题解答`.
- If no question has been asked yet, still create the section with a short placeholder such as `本节用于记录学习过程中的提问和回答。`.

## Finished Artifact Checklist

A daily result is complete only when it includes:

- exact source/doc paths read;
- a data flow, control flow, lifecycle, queue, thread, or state explanation;
- a Mermaid data-flow diagram and a Mermaid control-flow diagram in the note;
- a `## 问题解答` section in the note for the user's questions and answers;
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

- Do not write a technical claim without source evidence. Never fill an unknown implementation detail with a likely framework convention.
- Treat comments as supporting context only; executable code and active graph construction decide the documented behavior when they differ.
- Preserve uncertainty: distinguish source-confirmed behavior, conditional paths, and unverified hypotheses in both prose and diagrams.
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
