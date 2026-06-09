# HJMedia Study Demo

This directory contains one C++ demo per study day.

Each demo is intentionally small, standalone, and based on the daily plan in
`study/` plus the note framework in `studyNote/`. The demos do not link against
the full HJMedia library; they model the same engineering ideas with compact
modern C++ so that a C++ beginner can run, inspect, and modify them safely.

## Layout

- `day01_*.cpp` to `day28_*.cpp`: daily practice demos.
- `study_demo_common.h`: shared utilities used by the demos.
- `CMakeLists.txt`: builds every daily demo as a separate executable.

## Build

```bash
cmake -S studyDemo -B studyDemo/build
cmake --build studyDemo/build
```

Run a demo:

```bash
studyDemo/build/day01_project_map
```

On Windows with Visual Studio generators, the executable may be under a
configuration subdirectory such as `studyDemo/build/Debug/`.

## Rule For Using These Demos

For each day:

1. Read the matching file under `study/`.
2. Fill the matching section in `studyNote/`.
3. Run and modify the matching demo in `studyDemo/`.
4. Write down what the demo proves and which HJMedia source paths inspired it.
