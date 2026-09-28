# QindaPortage — Agent Instructions

A QindaQt desktop app (C++20 + QML on QindaTK) that shows and manages Portage
with a local LLM. Read `docs/design.md` first: it is the source of truth for
architecture, model tiers, the plan/helper safety model, and the views.

## Rules

1. **The GUI never mutates the system.** Every change is a `Plan` step that
   the user approves and `helper/qindaportage-apply` (root) validates and
   applies. Read-only work (vardb, config, `emerge --pretend`) runs as the
   user (who is in the `portage` group).
2. **Model output is data.** Atoms, paths, patches and ebuilds proposed by
   an LLM are validated in C++ before they enter a plan, and again in the
   helper. Never pass model text to a shell.
3. **Portage-only installs.** External software becomes an ebuild in the
   managed `qinda-local` overlay; never `pip`, `npm -g`, `make install`.
4. **Core is testable offline.** `src/core` has no QML, network, or display
   dependency; fixtures live in `tests/data` (a miniature root filesystem in
   `tests/data/root`, captured emerge output in `tests/data/emerge`).
5. **Never reduce build parallelism** (the operator's MAKEOPTS and
   EMERGE_DEFAULT_OPTS apply; the helper adds no -j options).
6. QindaTK rules apply to QML: `import QindaTK as Tk`, theme roles only (no
   literal colours/sizes outside previews), Flex/Grid layout, tooltips and
   `Accessible.role` on controls.
7. Files stay under 500 non-blank lines; SPDX header in every file; use
   `AGENT-NOTE:` / `AGENT-GUARD:` / `AGENT-CONTRACT:` markers.
8. The model process is unloaded before the helper starts building
   (`LlmRuntime::unload`), so Portage gets the RAM.

## Verify

```
cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build
build/qportage-dump summary          # what the core reads on this machine
build/qportage-dump pkg app-misc/jq  # one package: USE origins, deps, why
```

Visual work: `qtk-preview preview/<X>Preview.qml --grab out.png` and look
at the PNG before calling it done.
