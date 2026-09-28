# qindaportage-apply: the privileged helper

`helper/qindaportage-apply` is the only part of QindaPortage that changes
the system. It is a small Python program (stdlib only) that is installed to
`/usr/libexec/qindaportage/` with its two modules, `qportage_validate.py`
and `qportage_state.py`. It runs as root through `pkexec` under the polkit
action `org.qindaqt.portage.apply` (`auth_admin_keep`, see
`helper/org.qindaqt.portage.policy`). In a development tree,
`Ops::Executor` falls back to `sudo -n <tree>/helper/qindaportage-apply`.

AGENT-CONTRACT: this document is the protocol between the helper and
`src/ops/executor.cpp`. The plan format is `Ops::Plan::toJson()`
(`src/ops/plan.cpp`). Validation rules exist twice: `Validate` in plan.cpp
and `qportage_validate.py`. Change them together, along with
`tests/cpp/tst_plan.cpp` and `helper/tests/test_apply.py`.

## Commands

```
qindaportage-apply apply < plan.json     # one JSON object on stdin
qindaportage-apply revert <txn>
qindaportage-apply list                  # one {"event":"txn",...} per journal entry
qindaportage-apply show <txn>            # the journal entry, one JSON line
```

Test-only options: `--root DIR` (prefixes every path; the helper then also
runs without root), `--state-dir DIR`, `--emerge-cmd CMD`,
`--ebuild-cmd CMD`, and `--git CMD`. Production never passes them. pkexec
runs the helper with a clean environment regardless.

## Plan (stdin)

```json
{"version":1, "id":"20260928-120000-abcd", "title":"…", "goal":"…",
 "created":"2026-09-28T12:00:00Z",
 "steps":[{"kind":"use","atom":"app-misc/jq","values":["static-libs"],
           "rationale":"static binary for rescue use"},
          {"kind":"emerge","values":["app-misc/jq"],"options":["--oneshot"]}]}
```

| kind | fields | effect |
|---|---|---|
| `use` `keyword` `license` `unmask` `mask` | `atom`, `values` | appends to the managed file for `package.use` etc. |
| `patch` | `atom` (`cat/pn` or `=cat/pn-ver`), `path` (file name), `content` | writes `/etc/portage/patches/<cat>/<pn[-ver]>/<path>` |
| `ebuild` | `path` (`cat/pn/pn-ver.ebuild`, `cat/pn/metadata.xml`, `cat/pn/files/…`), `content` | writes into `/var/db/repos/qinda-local`, creating the overlay and its repos.conf entry on first use, then runs `ebuild <file> manifest` before the next build step |
| `emerge` | `values` (atoms or `@sets`), `options` (allowlist) | `emerge --color=n --nospinner <options> <values>` |
| `unmerge` | `values` | `emerge --deselect …` then `emerge --depclean …`: a safe removal that refuses when something still needs the package |
| `depclean` | `values` (may be empty) | `emerge --depclean …` |
| `sync` | `values` (repository names, may be empty) | `emerge --sync …` |

Each step is re-validated before anything happens, and a single invalid
step rejects the whole plan (`result` with `error: "rejected: …"`, exit
code 2). The helper never uses a shell. It adds no `-j`, `--jobs` or
`--load-average` option; the operator's `EMERGE_DEFAULT_OPTS` and `MAKEOPTS`
apply. It drops `USE`, `FEATURES`, `MAKEOPTS`, `PORTAGE_*` and similar
variables from its environment, so the configuration files alone decide the
build.

### Managed files

For each of `package.use`, `package.accept_keywords`, `package.license`,
`package.unmask` and `package.mask`:

- if `/etc/portage/package.X` is a directory, or does not exist yet: append
  to `/etc/portage/package.X/90-qindaportage`;
- if it is a single file: append a `# BEGIN qindaportage <txn>` …
  `# END qindaportage <txn>` block to that file.

Every entry is written in the house style of the existing fragments: a
dated comment with the rationale, then the line. A line that is already
present is not written again.

```
# 2026-09-28 — static binary for rescue use (txn 20260928-120000-abcd)
app-misc/jq static-libs
```

`PretendRunner::managedFile()` uses the same placement for the virtual
configuration root.

## Output (stdout, one JSON object per line)

| event | fields |
|---|---|
| `step` | `index` (0-based plan step), `text` |
| `log` | `line`: helper messages and every line emerge or ebuild prints |
| `undo` | `commands`: revert only; suggested commands to undo merged or removed packages (never run by the helper) |
| `result` | `ok`, `txn`, `error`; apply adds `merged` and `removed` (cpv lists); revert adds `restored` and `conflicts` |
| `txn` | `list` only: `id`, `kind`, `title`, `ok`, `started`, `ended`, `reverted_by`, `of` |

`result` is always the last event. If the process ends without one (killed,
pkexec dismissed with 126/127, or sudo refused), the executor reports a
failure itself.

## State

- `/var/lib/qindaportage/state.git`: a bare repository. Every snapshot is a
  commit of the managed scope relative to `/`: `etc/portage` (without
  `gnupg/` and any `.git`), `var/lib/portage/world`, `world_sets`, and
  `var/db/repos/qinda-local`. A temporary index is used for each snapshot,
  so it works whether or not `/etc/portage` is versioned elsewhere.
- `/var/lib/qindaportage/journal/<txn>.json` (0644, readable by the GUI):
  `id`, `kind` (`apply`|`revert`), `title`, `goal`, `steps`, `pre`/`post`
  snapshot commits, `touched` (paths changed between them), `commands`,
  `merged`, `removed`, `previous` (installed versions before, per key),
  `started`/`ended`, `ok`, `error`, and `reverted_by`/`of`.
- `/var/lib/qindaportage/lock`: one transaction at a time.

## Revert

`revert <txn>` restores only the paths the transaction touched, and only
when each one is still exactly as the transaction left it. A path that
changed since then (a hand edit, or a later transaction) is left alone and
reported in `conflicts`, with `ok: false`. A file the transaction created
is deleted, and a file it changed gets its snapshot content back. Nothing
else is written or deleted. Packages are not rebuilt; the `undo` event
lists the commands that would do it, for example
`emerge --ask --oneshot --usepkg =cat/pkg-<previous>` for an upgrade, or
`emerge --ask --depclean cat/pkg` for a new package. A transaction can be
reverted once.

## Interruption

SIGTERM or SIGINT (`Executor::cancel()`) terminates the running emerge. The
transaction is journaled with `error: "interrupted"` and its `post`
snapshot, so it can still be reverted.
