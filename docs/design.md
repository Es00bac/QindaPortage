# QindaPortage: design

QindaPortage is a QindaQt desktop application (C++20 + QML on QindaTK) that
manages Portage on a Gentoo machine and uses a local LLM to plan, explain
and repair the work. The repository directory is `LocalAiPortage`; the
application, binary and package are `qindaportage` (`app-portage/qindaportage`
in the `qindaqt` overlay).

## Goals

1. **See the system.** Show what is installed, why it is installed, its USE
   flags and where each flag's value comes from, its dependencies and
   reverse dependencies, and how the whole system is layered (`@system`, `@world`,
   the dependencies they pull in, and orphans).
2. **See a change before it happens.** Every install, update, USE change or
   removal is shown as an impact preview first: new packages, upgrades,
   downgrades, rebuilds with a USE diff, blockers and slot conflicts, download
   size, binary or source build, and an estimated build time from `qlop`
   history. The preview is drawn on the dependency graph as well as listed.
3. **Install anything and keep it tracked by Portage.** Software not in any
   repository, such as a GitHub URL, a tarball URL or a local directory, is
   packaged as an ebuild in a managed local overlay (`qinda-local`) and
   installed with `emerge`. Its dependencies become real Portage atoms, so it
   is updated, rebuilt and removed like any other package. Nothing is
   installed outside Portage. This matches the operator's Portage-only rule.
4. **Do not refuse. Make it work.** When the resolver or a build fails, the
   assistant classifies the failure and finds a fix: a USE change, a keyword,
   a slot rebuild, a user patch in `/etc/portage/patches`, a fixed ebuild in
   the local overlay, or a patch it finds upstream (Gentoo bugzilla, the
   upstream repository) or writes itself. It stops only when every option is
   exhausted, and it says what it tried.
5. **Nothing breaks silently.** All changes are transactions. The helper
   snapshots `/etc/portage`, the world file and the local overlay before
   applying one. Each transaction is journaled, and a transaction's
   configuration can be reverted.
6. **The model gives the resources back.** The LLM is loaded only while it is
   thinking. Before `emerge` builds anything, the model process is stopped so
   the compiler gets the RAM and the GPU.

## Machine constraints (qinda-top)

- Ryzen 7 5825U (8C/16T), 13.5 GiB RAM, Radeon Barcelo iGPU (Vulkan, shared
  memory). The desktop and browser commonly leave only 4 to 6 GiB available.
- `sci-misc/llama-cpp[vulkan]` from ::guru. `/usr/share/gguf/Qwen3.5-4B-UD-Q4_K_XL.gguf`
  (2.9 GB) is installed by `sci-ml/qinda-notes-llm`.
- qinda (desktop) serves `qinda-llm.service` on the tailnet: a llama.cpp router
  with Qwen3.6-35B-A3B / Ornith-1.5-35B-A3B (OpenAI API, key in
  `~/.config/qinda-llm/api-key`).
- `claude` and `codex` CLIs are installed and signed in.
- The user is in the `portage` group, so `emerge --pretend` runs unprivileged.
  `/var/db/pkg` holds about 1,300 packages.

## Model choice

| Tier | Model | Where | When |
|---|---|---|---|
| 1 | **Qwen3.5-9B** UD-Q4_K_XL (6.0 GB) | laptop, spawned `llama-server` | default when `MemAvailable` ≥ model + 2.5 GiB |
| 1 | Qwen3.5-4B UD-Q4_K_XL (2.9 GB) | laptop, same runtime | fallback when memory is short |
| 2 | Qwen3.6-35B-A3B / Ornith 35B | qinda over Tailscale | the tier-1 model gives up or loops, or the user picks it |
| 3 | Claude (`claude -p`) / Codex (`codex exec`) | cloud, through the signed-in CLIs | tier 2 is unavailable or fails; **every escalation needs the user's approval** |

Why: on BFCL v4 tool calling, Qwen3.5-9B scores 66.1 and the 4B scores 50.3.
Multi-turn tool use gets reliable at about 7 to 9B dense, and dense models
out-call MoE models of similar active size. The 9B is the better planner. The
laptop cannot always hold it next to the desktop, so the runtime checks
`/proc/meminfo` at load time and picks the largest model that fits. Both use
Qwen3.5's hybrid attention, so a 16k-token KV cache stays small. Thinking is
off by default because a reasoning trace costs minutes on the iGPU. It is
turned on per request for failure diagnosis.

The 9B weights are packaged like the 4B: pinned Hugging Face revision,
`/usr/share/gguf`, and the `qwen3-5-9b` USE flag on `app-portage/qindaportage`,
which pulls `sci-ml/qindaportage-models`.

### Keeping the model small in context

A 4 to 9B model is a good planner and a poor database. The code does the
analysis; the model chooses and explains:

- Tools return compact, pre-digested JSON (for example the resolver output
  already parsed into `{action, atom, useChanges, problems:[{kind, packages,
  hint}]}`), never raw logs. A build failure arrives as the classified error
  plus the 60 most relevant log lines.
- The conflict classifier (`EmergePlanParser`) proposes candidate fixes in
  code (for example "required USE: enable `X` on `cat/pkg`"). The model picks,
  combines and justifies them.
- The system prompt carries a short system profile: arch, profile, key
  `make.conf` variables, repos and world size. Details are fetched with tools.

## Architecture

```
┌──────────────────────── qindaportage (user) ─────────────────────────┐
│ QML (QindaTK)  Overview · Packages · Graph · Changes · Sources ·     │
│                Config · Activity · Assistant dock                    │
│──────────────────────────────────────────────────────────────────────│
│ core/   VarDb · Atom/Version · DepSpec · PackageGraph · PortageConfig│
│         RepoIndex · UseDescriptions · EmergePlanParser · BuildHistory│
│ agent/  LlmRuntime (spawn/stop llama-server, memory-adaptive)        │
│         LlmClient (OpenAI chat + tools) · Toolbox · AgentSession     │
│         Escalation (qinda tier, claude -p / codex exec)              │
│ ops/    Plan (transaction) · Pretend runner · Executor · SourceProbe │
└───────────────┬──────────────────────────────────────────────────────┘
                │ plan JSON on stdin, JSON-lines progress on stdout
                ▼
┌────────── qindaportage-apply (root, via pkexec / sudo -n) ───────────┐
│ validates every step against an allowlist, snapshots state, applies, │
│ runs emerge, journals, reverts                                       │
└──────────────────────────────────────────────────────────────────────┘
```

### Reading the system (core/)

- **VarDb** reads `/var/db/pkg/<cat>/<pf>/` directly (1,300 packages in well
  under a second): `SLOT`, `repository`, `USE`, `IUSE`, `IUSE_EFFECTIVE`,
  `*DEPEND`, `SIZE`, `BUILD_TIME`, `DESCRIPTION`, `HOMEPAGE`, `LICENSE`,
  `KEYWORDS`, and `CONTENTS` on demand.
- **Atom / Version** follow PMS: version comparison (suffixes, `_p`,
  `-rN`), operators `= ~ < <= > >= =*`, slot and sub-slot deps, and USE deps.
- **DepSpec** parses dependency strings into a tree (`||`, `flag? ( )`,
  `!flag? ( )`, blockers) and evaluates them against the package's `USE`.
  The result is the set of edges that are actually in effect.
- **PackageGraph** resolves each atom to the installed provider and builds
  typed edges (build, run, post, blocker). It computes reverse dependencies,
  the shortest "why installed" chain from `@world`/`@system`, and orphans
  (unreachable, the `depclean` candidates). It also assigns layers: selected,
  system, direct dependency of the selection, deeper dependency.
- **PortageConfig** reads `make.conf` (sourced through `portageq envvar` for
  exact values), `package.*` files and directories, `repos.conf`, the profile
  chain and the world file. It also answers where a USE value came from:
  profile, `make.conf`, `package.use` file:line, forced or masked.
- **RepoIndex** lists every available `cat/pn` in every repository: the
  md5-cache where one exists, else the tree plus the ebuild `DESCRIPTION`.
  Full metadata for an available version is fetched on demand with
  `portageq metadata`.
- **EmergePlanParser** turns `emerge --pretend --verbose --tree` output into a
  merge list and classified problems: slot conflict, blocker, masked
  (keyword, license, `package.mask`, missing), REQUIRED_USE, USE-dependency
  change needed, and unsatisfied dependency. It attaches candidate fixes.
- **BuildHistory** reads `qlop` (`/var/log/emerge.log`) for merge times and
  failures.

### The assistant (agent/)

`AgentSession` runs the tool loop over OpenAI-compatible chat completions,
which llama-server serves with `--jinja` for tool calls.

Read-only tools run immediately:

| Tool | Returns |
|---|---|
| `system_profile` | arch, profile, key make.conf vars, repos, counts |
| `find_packages(query)` | installed and available matches |
| `package_info(atom)` | versions, slot, repo, USE (with origins), deps, rdeps, why-installed |
| `reverse_deps(atom)` | what needs it, and through which flag |
| `pretend(atoms, useOverrides?, options?)` | parsed resolver result and candidate fixes |
| `read_config(path)` | a `/etc/portage` file (read-only allowlist) |
| `use_flag_info(flag, atom?)` | description, packages using it |
| `build_log(atom)` | classified failure and the relevant log lines |
| `probe_source(url)` | build system, declared dependencies and version of a repository or tarball |
| `web_fetch(url)` | text of a page (bugs.gentoo.org, GitHub, upstream) |
| `search_patches(atom, error)` | candidate patches from Gentoo bugzilla and the upstream tracker |

Mutating tools only add steps to the draft **Plan**. The user sees and
approves the plan before anything changes:

`plan_use`, `plan_keyword`, `plan_unmask`, `plan_license`, `plan_patch`,
`plan_ebuild` (local overlay), `plan_emerge`, `plan_remove`, `plan_depclean`,
`plan_sync`. Every step carries a one-line rationale. It is written as a
comment above the line in the managed config file, following the house style
of `/etc/portage/package.use/80-gaming`.

**Repair loop.** Pretend → classify → propose fixes → pretend again with the
fixes applied virtually (`--pretend` with a scratch `PORTAGE_CONFIGROOT`
overlaying the plan's config) → repeat until the resolver is clean (at most
six rounds). After approval and a build failure: reload the model, classify
the log, propose a patch or ebuild fix, and retry (at most three attempts).
Then escalate a tier, with approval for tier 3.

**Escalation** hands the next tier a self-contained brief: goal, system
profile, plan so far, what was tried, and the classified error with its log
excerpt. Tier 2 is another OpenAI endpoint. Tier 3 runs `claude -p` or
`codex exec` in a scratch directory with the brief and the relevant files. It
reads the answer back as a structured plan, and that plan goes through the
same approval gate.

### Model lifecycle (the "unload" requirement)

`LlmRuntime` owns a `llama-server` child process. It never uses the shared
notes socket, so QindaNote is unaffected:

- **load** on the first request: choose the model by `MemAvailable`, start
  on a free loopback port, and wait for `/health`.
- **unload** in three cases: (a) as the first step of every approved plan,
  before the helper starts; (b) after an idle timeout (3 minutes by default);
  (c) on quit.
- The status bar always shows the model state (unloaded, loading, ready,
  thinking) and its resident memory.

### Changing the system (ops/ + helper/)

A **Plan** is an ordered list of typed steps, serialized as JSON. The
privileged helper `qindaportage-apply` is a small Python program installed to
`/usr/libexec/qindaportage/` and started with `pkexec`. A polkit policy ships
in the ebuild; in a development tree it falls back to `sudo -n`. The helper
trusts nothing the GUI sends:

- Config writes go only to managed files:
  `/etc/portage/package.{use,accept_keywords,mask,unmask,license,env}/90-qindaportage`,
  `/etc/portage/patches/<cat>/<pn>/*.patch`, and the local overlay
  `/var/db/repos/qinda-local`. Atoms are validated, and paths are
  canonicalized and checked against these roots.
- `emerge` runs only with allowlisted options. Atoms are validated. There is
  no shell.
- **Before** applying, it commits `/etc/portage`, the world file and the
  local overlay into its own state repository,
  `git --git-dir=/var/lib/qindaportage/state.git`.
- It streams JSON-lines progress (`step`, `log`, `result`) that the GUI shows
  in a terminal view, and journals the transaction in
  `/var/lib/qindaportage/journal/`.
- **revert <txn>** restores the snapshot's configuration. Packages built by
  the transaction are listed with the re-emerge or unmerge command that
  undoes them. Binary packages from `FEATURES=buildpkg` or the qinda binhost
  make that fast.

### External software (Sources)

`SourceProbe` takes a URL (a GitHub or GitLab repository, a release tarball,
or a local path). It fetches the file list and the build files through the
GitHub API or a shallow clone into the scratch cache, then detects:

- Build system: CMake, Meson, Autotools, Cargo, Go modules, Python
  (pyproject), npm, or a plain Makefile.
- Version: the latest release or tag, otherwise a pinned commit date.
- Dependencies: `find_package`/`pkg_check_modules`, `dependency()`, the
  Cargo, go.mod and pyproject lists.

The model maps each dependency to a Gentoo atom through `find_packages`,
writes the ebuild with the matching eclass (`cmake`, `meson`, `cargo`,
`go-module`, `distutils-r1`, or git-r3 pinned to a commit), and the plan
creates it in `qinda-local` with a Manifest. `pretend` checks it, and the
build loop repairs it. The result is a normal package: `emerge -C`, updates by
bumping the ebuild, and it appears in the graph with the `qinda-local` badge.

### Views

| View | What it shows |
|---|---|
| **Overview** | counts (installed, world, orphans, pending updates, news), disk use by category, packages by repository, recent merges and failures |
| **Packages** | category tree, filterable table (name, version, slot, repo, size, installed, world/dep/orphan), inspector: USE flags as chips with state and origin, deps, reverse deps, why-installed chain, files |
| **Graph** | the selected package's neighbourhood as a node graph (depth 1 to 3, deps, reverse deps or both, edge-type filter), coloured by layer |
| **Changes** | a request (typed atoms or natural language), the impact preview (table, problem cards with fixes, the graph of what attaches where), the plan editor, Apply |
| **Sources** | install from a URL: probe results, dependency mapping, the generated ebuild in a code editor, and the plan |
| **Config** | make.conf variables, package.* files with the managed file highlighted, USE flag browser (global and local, with packages that use each flag) |
| **Activity** | the transaction journal, live build log, and revert |
| **Assistant** | a dock on every view: chat, tool-call trace, plan card, model and tier state |

## Safety rules (invariants)

1. The GUI never runs a mutating command. Only the helper mutates, and only
   from an approved plan.
2. Every applied plan is preceded by a snapshot, and every snapshot can be
   reverted.
3. The model's output is data. Its atoms, paths and patches are validated by
   code before they reach a plan, and again by the helper.
4. Cloud escalation sends only the brief shown to the user, and only after
   the user approves it. It never sends secrets, never sends `/etc` files
   outside the brief, and never sends the API key.
5. Build parallelism is the operator's `MAKEOPTS`/`EMERGE_DEFAULT_OPTS`. The
   helper adds no `-j` options.
