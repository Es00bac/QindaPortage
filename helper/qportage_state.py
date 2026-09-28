# SPDX-License-Identifier: GPL-3.0-or-later
"""Snapshots and journal for qindaportage-apply.

Snapshots are commits in a private git repository (state.git) whose tree
mirrors the managed scope relative to the root: etc/portage (without its
gnupg keyring), var/lib/portage/world{,_sets} and the qinda-local overlay.
The live system is never a git work tree of its own; a temporary index is
used for every snapshot, so this works whether or not /etc/portage is
versioned by someone else.
"""

import json
import os
import subprocess
import tempfile
import time

SCOPE = ('etc/portage', 'var/lib/portage/world', 'var/lib/portage/world_sets',
         'var/db/repos/qinda-local')
EXCLUDES = (':(exclude)etc/portage/gnupg', ':(exclude)etc/portage/.git',
            ':(exclude)var/db/repos/qinda-local/.git')


class State:
    def __init__(self, root, state_dir, git='git'):
        self.root = root or '/'
        self.state_dir = state_dir
        self.git_dir = os.path.join(state_dir, 'state.git')
        self.journal_dir = os.path.join(state_dir, 'journal')
        self.git_cmd = git

    def ensure(self):
        os.makedirs(self.state_dir, mode=0o755, exist_ok=True)
        os.makedirs(self.journal_dir, mode=0o755, exist_ok=True)
        os.chmod(self.journal_dir, 0o755)
        if not os.path.isdir(self.git_dir):
            self._git('init', '-q', '--bare', self.git_dir, bare_init=True)

    def _env(self, index=None):
        env = {
            'PATH': os.environ.get('PATH', '/usr/bin:/bin'),
            'GIT_DIR': self.git_dir,
            'GIT_WORK_TREE': self.root,
            'GIT_CONFIG_NOSYSTEM': '1',
            'GIT_CONFIG_GLOBAL': os.devnull,
            'GIT_AUTHOR_NAME': 'qindaportage', 'GIT_AUTHOR_EMAIL': 'root@localhost',
            'GIT_COMMITTER_NAME': 'qindaportage', 'GIT_COMMITTER_EMAIL': 'root@localhost',
            'HOME': self.state_dir,
        }
        if index:
            env['GIT_INDEX_FILE'] = index
        return env

    def _git(self, *args, index=None, binary=False, bare_init=False, check=True):
        env = self._env(index)
        if bare_init:
            env.pop('GIT_DIR')
            env.pop('GIT_WORK_TREE')
        result = subprocess.run([self.git_cmd, '-c', 'core.bare=false', *args], env=env,
                                capture_output=True, check=False)
        if check and result.returncode != 0:
            raise RuntimeError('git %s failed: %s' % (args[0], result.stderr.decode(errors='replace')))
        return result.stdout if binary else result.stdout.decode(errors='replace')

    def head(self):
        out = self._git('rev-parse', '--verify', '-q', 'refs/heads/main', check=False).strip()
        return out or None

    def snapshot(self, message):
        """Commits the managed scope; returns the commit id."""
        paths = [p for p in SCOPE if os.path.lexists(os.path.join(self.root, p))]
        fd, index = tempfile.mkstemp(prefix='index-', dir=self.state_dir)
        os.close(fd)
        os.unlink(index)
        try:
            if paths:
                self._git('add', '-A', '-f', '--', *paths, *EXCLUDES, index=index)
            tree = self._git('write-tree', index=index).strip()
        finally:
            if os.path.exists(index):
                os.unlink(index)
        parent = self.head()
        if parent and self._git('rev-parse', parent + '^{tree}').strip() == tree:
            return parent
        args = ['commit-tree', tree, '-m', message]
        if parent:
            args[2:2] = ['-p', parent]
        commit = self._git(*args).strip()
        self._git('update-ref', 'refs/heads/main', commit)
        return commit

    def changed(self, a, b):
        out = self._git('diff-tree', '-r', '--no-renames', '--name-only', a, b)
        return [line for line in out.splitlines() if line]

    def blob(self, commit, path):
        """(mode, bytes) of path in commit, or None when absent."""
        out = self._git('ls-tree', commit, '--', path)
        if not out.strip():
            return None
        mode = out.split()[0]
        data = self._git('cat-file', 'blob', '%s:%s' % (commit, path), binary=True)
        return mode, data

    def current(self, path):
        """(mode, bytes) of the live file, or None."""
        full = os.path.join(self.root, path)
        if os.path.islink(full):
            return '120000', os.readlink(full).encode()
        if not os.path.isfile(full):
            return None
        with open(full, 'rb') as f:
            data = f.read()
        return ('100755' if os.access(full, os.X_OK) else '100644'), data

    def restore(self, path, entry):
        full = os.path.join(self.root, path)
        if os.path.lexists(full):
            os.unlink(full)
        if entry is None:
            return
        mode, data = entry
        os.makedirs(os.path.dirname(full), mode=0o755, exist_ok=True)
        if mode == '120000':
            os.symlink(data.decode(), full)
            return
        with open(full, 'wb') as f:
            f.write(data)
        os.chmod(full, 0o755 if mode == '100755' else 0o644)

    # --- journal ---

    def journal_path(self, txn):
        return os.path.join(self.journal_dir, txn + '.json')

    def write_journal(self, entry):
        path = self.journal_path(entry['id'])
        tmp = path + '.tmp'
        with open(tmp, 'w', encoding='utf-8') as f:
            json.dump(entry, f, indent=1, sort_keys=True)
            f.write('\n')
        os.chmod(tmp, 0o644)
        os.replace(tmp, path)

    def read_journal(self, txn):
        with open(self.journal_path(txn), encoding='utf-8') as f:
            return json.load(f)

    def list_journal(self):
        out = []
        for name in sorted(os.listdir(self.journal_dir), reverse=True):
            if name.endswith('.json'):
                try:
                    out.append(self.read_journal(name[:-5]))
                except (OSError, ValueError):
                    continue
        return out


def now():
    return time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
