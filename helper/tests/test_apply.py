# SPDX-License-Identifier: GPL-3.0-or-later
"""qindaportage-apply against a fake root, a fake emerge and a fake ebuild.

Run: python3 -m unittest discover -s helper/tests   (also a ctest: helper_apply)
Nothing here touches the live system or needs root: every path is under a
temporary --root, and emerge/ebuild are small scripts that log their args.
"""

import json
import os
import subprocess
import sys
import tempfile
import textwrap
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
HELPER = os.path.join(os.path.dirname(HERE), 'qindaportage-apply')
sys.path.insert(0, os.path.dirname(HERE))
import qportage_validate as v  # noqa: E402

FAKE_EMERGE = textwrap.dedent('''\
    #!/bin/sh
    echo "$@" >> "$FAKE_LOG"
    case " $* " in
      *" --depclean "*) echo ">>> Unmerging (1 of 1) app-misc/old-1.0..." ;;
      *" --deselect "*) echo ">>> Removing app-misc/old from \\"world\\" favorites file..." ;;
      *" app-misc/broken"*) echo "!!! build failed"; exit 1 ;;
      *) echo ">>> Emerging (1 of 1) app-misc/tool-2.0::gentoo"
         echo ">>> Installing (1 of 1) app-misc/tool-2.0::gentoo"
         echo ">>> Completed (1 of 1) app-misc/tool-2.0::gentoo" ;;
    esac
''')
FAKE_EBUILD = textwrap.dedent('''\
    #!/bin/sh
    echo "ebuild $@" >> "$FAKE_LOG"
    echo "DIST fake 1 BLAKE2B 0 SHA512 0" > "$(dirname "$1")/Manifest"
''')


class HelperTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = self.tmp.name
        self.root = os.path.join(t, 'root')
        self.state = os.path.join(t, 'state')
        self.log = os.path.join(t, 'calls.log')
        for d in ('etc/portage/package.use', 'etc/portage/repos.conf', 'etc/portage/gnupg',
                  'var/lib/portage', 'var/db/pkg/app-misc/tool-1.0', 'var/db/pkg/app-misc/old-1.0'):
            os.makedirs(os.path.join(self.root, d))
        self.write('etc/portage/make.conf', 'USE="X"\n')
        self.write('etc/portage/package.accept_keywords', '# existing single file\n')
        self.write('etc/portage/package.use/50-mine', 'app-misc/foo bar\n')
        self.write('etc/portage/gnupg/secret', 'do not snapshot\n')
        self.write('var/lib/portage/world', 'app-misc/tool\n')
        os.symlink('../../var/db/repos/gentoo/profiles/default', os.path.join(self.root, 'etc/portage/make.profile'))
        self.emerge = self.script('emerge', FAKE_EMERGE)
        self.ebuild = self.script('ebuild', FAKE_EBUILD)

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, rel, text):
        with open(os.path.join(self.root, rel), 'w') as f:
            f.write(text)

    def read(self, rel):
        with open(os.path.join(self.root, rel)) as f:
            return f.read()

    def script(self, name, body):
        path = os.path.join(self.tmp.name, name)
        with open(path, 'w') as f:
            f.write(body)
        os.chmod(path, 0o755)
        return path

    def helper(self, *args, stdin=''):
        env = dict(os.environ, FAKE_LOG=self.log)
        proc = subprocess.run([sys.executable, HELPER, '--root', self.root, '--state-dir', self.state,
                               '--emerge-cmd', self.emerge, '--ebuild-cmd', self.ebuild, *args],
                              input=stdin, capture_output=True, text=True, env=env)
        events = [json.loads(line) for line in proc.stdout.splitlines() if line.startswith('{')]
        return proc.returncode, events, proc

    def apply(self, steps, **extra):
        plan = dict(title='test', goal='a test', steps=steps, **extra)
        return self.helper('apply', stdin=json.dumps(plan))

    def journal(self, txn):
        with open(os.path.join(self.state, 'journal', txn + '.json')) as f:
            return json.load(f)

    def result(self, events):
        return [e for e in events if e['event'] == 'result'][-1]

    def test_config_steps_and_emerge(self):
        code, events, proc = self.apply([
            {'kind': 'use', 'atom': 'app-misc/tool', 'values': ['+gui', '-ssl'], 'rationale': 'want the GUI\nplease'},
            {'kind': 'keyword', 'atom': '=app-misc/tool-2.0', 'values': ['~amd64']},
            {'kind': 'emerge', 'values': ['app-misc/tool'], 'options': ['--oneshot']},
        ])
        self.assertEqual(code, 0, proc.stderr)
        res = self.result(events)
        self.assertTrue(res['ok'])
        self.assertEqual(res['merged'], ['app-misc/tool-2.0'])
        use = self.read('etc/portage/package.use/90-qindaportage')
        self.assertIn('— want the GUI please (txn %s)\napp-misc/tool gui -ssl\n' % res['txn'], use)
        kw = self.read('etc/portage/package.accept_keywords')
        self.assertIn('# BEGIN qindaportage %s' % res['txn'], kw)
        self.assertIn('=app-misc/tool-2.0 ~amd64', kw)
        self.assertTrue(kw.startswith('# existing single file\n'))
        with open(self.log) as f:
            call = f.read().strip()
        self.assertEqual(call, '--color=n --nospinner --oneshot app-misc/tool')
        steps = [e for e in events if e['event'] == 'step']
        self.assertEqual([s['index'] for s in steps], [0, 1, 2])
        entry = self.journal(res['txn'])
        self.assertEqual(oct(os.stat(os.path.join(self.state, 'journal', res['txn'] + '.json')).st_mode & 0o777), '0o644')
        self.assertIn('etc/portage/package.use/90-qindaportage', entry['touched'])
        self.assertEqual(entry['previous'], {'app-misc/tool': ['1.0']})
        self.assertNotEqual(entry['pre'], entry['post'])

    def test_ebuild_creates_overlay_and_manifest(self):
        code, events, proc = self.apply([
            {'kind': 'ebuild', 'path': 'app-misc/newthing/newthing-0.1.ebuild', 'content': 'EAPI=8\n'},
            {'kind': 'ebuild', 'path': 'app-misc/newthing/files/fix.patch', 'content': '--- a\n+++ b\n'},
            {'kind': 'emerge', 'values': ['app-misc/newthing']},
        ])
        self.assertEqual(code, 0, proc.stderr)
        overlay = 'var/db/repos/qinda-local'
        self.assertEqual(self.read(overlay + '/profiles/repo_name'), 'qinda-local\n')
        self.assertIn('masters = gentoo', self.read(overlay + '/metadata/layout.conf'))
        self.assertEqual(self.read(overlay + '/profiles/categories'), 'app-misc\n')
        self.assertIn('location = /var/db/repos/qinda-local', self.read('etc/portage/repos.conf/qinda-local.conf'))
        self.assertTrue(os.path.exists(os.path.join(self.root, overlay, 'app-misc/newthing/Manifest')))
        with open(self.log) as f:
            calls = f.read().splitlines()
        self.assertTrue(calls[0].startswith('ebuild ') and calls[0].endswith('newthing-0.1.ebuild manifest'))

    def test_patch_step(self):
        code, _, proc = self.apply([{'kind': 'patch', 'atom': '=app-misc/tool-2.0', 'path': 'gcc15.patch',
                                     'content': '--- a/x\n+++ b/x\n'}])
        self.assertEqual(code, 0, proc.stderr)
        self.assertIn('+++ b/x', self.read('etc/portage/patches/app-misc/tool-2.0/gcc15.patch'))

    def test_unmerge_is_deselect_then_depclean(self):
        code, events, _ = self.apply([{'kind': 'unmerge', 'values': ['app-misc/old']}])
        self.assertEqual(code, 0)
        with open(self.log) as f:
            calls = f.read().splitlines()
        self.assertEqual(calls, ['--color=n --nospinner --deselect app-misc/old',
                                 '--color=n --nospinner --depclean app-misc/old'])
        self.assertEqual(self.result(events)['removed'], ['app-misc/old-1.0'])

    def test_failure_is_journaled(self):
        code, events, _ = self.apply([
            {'kind': 'use', 'atom': 'app-misc/broken', 'values': ['x']},
            {'kind': 'emerge', 'values': ['app-misc/broken']},
            {'kind': 'emerge', 'values': ['app-misc/never']},
        ])
        self.assertEqual(code, 1)
        res = self.result(events)
        self.assertFalse(res['ok'])
        self.assertIn('failed', res['error'])
        with open(self.log) as f:
            self.assertNotIn('app-misc/never', f.read())

    def test_rejections(self):
        bad = [
            {'kind': 'use', 'atom': 'app-misc/x; rm -rf /', 'values': ['a']},
            {'kind': 'use', 'atom': 'app-misc/x', 'values': ['$(id)']},
            {'kind': 'use', 'atom': 'app-misc/x[foo]', 'values': ['a']},
            {'kind': 'ebuild', 'path': '../../etc/shadow', 'content': 'x'},
            {'kind': 'ebuild', 'path': 'app-misc/x/other-1.0.ebuild', 'content': 'x'},
            {'kind': 'patch', 'atom': 'app-misc/x', 'path': 'sub/x.patch', 'content': 'x'},
            {'kind': 'emerge', 'values': ['app-misc/x'], 'options': ['--ask']},
            {'kind': 'emerge', 'values': ['app-misc/x'], 'options': ['--jobs=64']},
            {'kind': 'emerge', 'values': ['--nodeps']},
            {'kind': 'sync', 'values': ['../gentoo']},
            {'kind': 'shell', 'values': ['ls']},
        ]
        for step in bad:
            code, events, _ = self.apply([step])
            self.assertEqual(code, 2, step)
            self.assertTrue(self.result(events)['error'].startswith('rejected'), step)
        self.assertFalse(os.path.exists(self.log))
        self.assertFalse(os.path.exists(os.path.join(self.root, 'etc/portage/package.use/90-qindaportage')))

    def test_snapshot_excludes_keyring(self):
        code, events, _ = self.apply([{'kind': 'mask', 'atom': '>=app-misc/tool-3'}])
        self.assertEqual(code, 0)
        entry = self.journal(self.result(events)['txn'])
        git = ['git', '--git-dir', os.path.join(self.state, 'state.git')]
        files = subprocess.run(git + ['ls-tree', '-r', '--name-only', entry['pre']],
                               capture_output=True, text=True).stdout.split()
        self.assertIn('etc/portage/make.conf', files)
        self.assertIn('etc/portage/make.profile', files)
        self.assertIn('var/lib/portage/world', files)
        self.assertNotIn('etc/portage/gnupg/secret', files)

    def test_revert_restores_only_touched_files(self):
        code, events, _ = self.apply([
            {'kind': 'use', 'atom': 'app-misc/tool', 'values': ['gui']},
            {'kind': 'keyword', 'atom': 'app-misc/tool', 'values': ['~amd64']},
            {'kind': 'emerge', 'values': ['app-misc/tool']},
        ])
        txn = self.result(events)['txn']
        # An unrelated change after the transaction must survive the revert.
        self.write('etc/portage/make.conf', 'USE="X wayland"\n')
        code, events, proc = self.helper('revert', txn)
        self.assertEqual(code, 0, proc.stdout)
        self.assertFalse(os.path.exists(os.path.join(self.root, 'etc/portage/package.use/90-qindaportage')))
        self.assertEqual(self.read('etc/portage/package.accept_keywords'), '# existing single file\n')
        self.assertEqual(self.read('etc/portage/make.conf'), 'USE="X wayland"\n')
        self.assertEqual(self.read('etc/portage/package.use/50-mine'), 'app-misc/foo bar\n')
        undo = [e for e in events if e['event'] == 'undo'][0]['commands']
        self.assertEqual(undo, ['emerge --ask --oneshot --usepkg =app-misc/tool-1.0'])
        code, events, _ = self.helper('revert', txn)
        self.assertEqual(code, 2)
        self.assertIn('already reverted', self.result(events)['error'])

    def test_revert_leaves_files_changed_since(self):
        _, events, _ = self.apply([{'kind': 'use', 'atom': 'app-misc/tool', 'values': ['gui']}])
        txn = self.result(events)['txn']
        with open(os.path.join(self.root, 'etc/portage/package.use/90-qindaportage'), 'a') as f:
            f.write('app-misc/hand edited\n')
        code, events, _ = self.helper('revert', txn)
        self.assertEqual(code, 1)
        self.assertEqual(self.result(events)['conflicts'], ['etc/portage/package.use/90-qindaportage'])
        self.assertIn('hand edited', self.read('etc/portage/package.use/90-qindaportage'))

    def test_list_and_show(self):
        _, events, _ = self.apply([{'kind': 'unmask', 'atom': '=app-misc/tool-2.0'}])
        txn = self.result(events)['txn']
        code, events, _ = self.helper('list')
        self.assertEqual(code, 0)
        self.assertEqual([e['id'] for e in events], [txn])
        code, _, proc = self.helper('show', txn)
        self.assertEqual(json.loads(proc.stdout)['id'], txn)

    def test_plan_id_is_used_when_well_formed(self):
        _, events, _ = self.apply([{'kind': 'mask', 'atom': 'app-misc/x'}], id='20260928-120000-abcd')
        self.assertEqual(self.result(events)['txn'], '20260928-120000-abcd')


class ValidateTest(unittest.TestCase):
    def test_atoms(self):
        for good in ('app-misc/jq', '>=dev-libs/openssl-3.0.1-r2:0/3=::gentoo',
                     '=dev-lang/python-3.12*', '~a/b-1.0', 'dev-libs/icu:=', 'x11-libs/gtk+:3'):
            self.assertTrue(v.is_atom(good), good)
        for bad in ('jq', '>=dev-libs/openssl', '~a/b-1.0-r1', '!a/b', 'a/b c', 'a/b[', ''):
            self.assertFalse(v.is_atom(bad), bad)

    def test_options(self):
        self.assertTrue(v.allowed_emerge_option('--with-bdeps=y'))
        self.assertTrue(v.allowed_emerge_option('--exclude=dev-lang/rust'))
        self.assertFalse(v.allowed_emerge_option('--exclude=rust'))
        self.assertFalse(v.allowed_emerge_option('--load-average=3'))


if __name__ == '__main__':
    unittest.main()
