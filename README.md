# QindaPortage

**See, plan and repair Portage with a local LLM**: a QindaTK desktop app for
Gentoo on the QindaQt desktop. The repository directory is `LocalAiPortage`;
the application and package are `qindaportage` (`app-portage/qindaportage`
in the `qindaqt` overlay).

- **See the system.** Every installed package, with its USE flags and where
  each value comes from (`package.use` file:line, `make.conf`, profile,
  forced/masked). Also why each package is installed (the chain back to
  `@world`/`@system`), its dependencies both ways, its files, a
  layered view of `@system`/`@world`/dependencies/orphans, disk use by
  category, and packages by repository. Orphan detection matches
  `emerge --depclean` exactly.
- **See a change before it happens.** Every plan is previewed with a virtual
  `emerge --pretend`: a scratch copy of `/etc/portage` with the plan applied,
  and a scratch `qinda-local` overlay with a real Manifest for new ebuilds.
  The preview shows new packages, upgrades, rebuilds with USE diffs, binary
  or source, blockers, slot conflicts, REQUIRED_USE and mask problems with
  candidate fixes, and build time estimated from `emerge.log`. It is drawn
  on the dependency graph too.
- **Install anything, tracked by Portage.** A GitHub, GitLab or Codeberg URL,
  a tarball or a local tree is probed. The prober detects the build system,
  the version and the dependencies (mapped to Gentoo atoms) and drafts an
  ebuild for the managed `qinda-local` overlay (cargo `CRATES` come from
  `Cargo.lock`). If a repository already carries the software, that package
  is used instead.
- **The assistant.** A tool-using local model (Qwen3.5-9B when memory allows,
  otherwise Qwen3.5-4B, on llama.cpp/Vulkan) plans changes, checks them with
  pretend, reads build logs, searches Gentoo Bugzilla and upstream, and
  proposes USE/keyword changes, patches or fixed ebuilds. It escalates to
  qinda's 35B model, and then, only with your approval of the exact brief,
  to Claude or Codex. **The model unloads itself** when idle and before any
  build.
- **Nothing changes without approval.** Plans are applied by
  `qindaportage-apply`, run as root through pkexec. It re-validates every
  step and writes only managed files
  (`/etc/portage/package.*/90-qindaportage`, `/etc/portage/patches`,
  `qinda-local`). It snapshots `/etc/portage`, the world file and the overlay
  into `/var/lib/qindaportage/state.git` first, and journals every
  transaction; a transaction's configuration can be reverted.

Read `docs/design.md` for the architecture and `docs/helper.md` for the
helper's protocol.

## Build and test

```
cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build
build/qindaportage                      # the app
build/qportage-dump summary             # what the core reads on this machine
build/qportage-dump pkg media-video/mpv # one package: USE origins, deps, why
build/qportage-dump search json         # every repository
build/qportage-probe https://github.com/owner/project   # SourceInfo + draft ebuild
build/qportage-ask "install jq with static libraries"   # assistant, headless; applies nothing
build/qportage-ask --tier qinda "…"     # the same on qinda's 35B model
```

## Verified on qinda-top

| What | Result |
|---|---|
| vardb + profile + graph | 1,320 packages, 9,693 edges, in ~0.6 s warm; the orphan set equals `emerge -p --depclean` (58/58) |
| repository index | 21,798 packages, 37,409 versions, ~1.1 s warm |
| local model | Qwen3.5-4B on the Barcelo iGPU: load 6.6 s, ~90 tok/s prompt, ~10 tok/s generation |
| "Install jq with static libraries" (local 4B) | found that oniguruma needs `static-libs` too, verified, planned both plus the emerge, ~4 min |
| "Install https://github.com/sharkdp/hexyl" (qinda) | used `sys-apps/hexyl::guru` instead of packaging it; planned `=sys-apps/hexyl-0.17.0 ~amd64` |
| "Install https://github.com/PaulJuliusMartinez/jless" (qinda) | cargo ebuild with 81 crates in qinda-local; the virtual Manifest fetched the sources and pretend resolved |
