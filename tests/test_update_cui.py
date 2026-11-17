"""Console test of tools/update_cui.py. No network: upstream and the fork are local repositories.

    python -m unittest discover -s tests -p test_update_cui.py
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import update_cui  # noqa: E402

REAL_FORK = update_cui.FORK


def git(cwd, *args):
    done = subprocess.run(['git', '-c', 'user.name=t', '-c', 'user.email=t@invalid', *args], cwd=cwd, capture_output=True,
                          text=True, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    assert done.returncode == 0, done.stderr
    return done.stdout.strip()


def commit(repo, name, text, message):
    path = Path(repo) / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8', newline='\n')
    git(repo, 'add', '-A')
    git(repo, 'commit', '-q', '-m', message)
    return git(repo, 'rev-parse', 'HEAD')


class UpdateCuiTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix='update-cui-'))
        self.addCleanup(shutil.rmtree, self.tmp, True)
        # Upstream: two commits of a tiny CUI; the fork's branch is upstream's first commit plus the right-to-left fix.
        self.up = self.tmp / 'upstream-work'
        self.up.mkdir()
        git(self.up, 'init', '-q', '-b', 'master')
        commit(self.up, 'src/cui_textedit.c', 'int plain(void);\n', 'first')
        self.base = commit(self.up, 'src/keep.c', 'int keep;\n', 'second')
        self.fork_work = self.tmp / 'fork-work'
        git(self.tmp, 'clone', '-q', self.up, self.fork_work)
        git(self.fork_work, 'checkout', '-q', '-b', update_cui.BRANCH)
        commit(self.fork_work, 'src/cui_textedit.c', 'int plain(void);\nint cui__paint_edit_shaped(void);\n', 'the fix')
        self.upstream = self.tmp / 'upstream.git'
        self.fork = self.tmp / 'fork.git'
        git(self.tmp, 'clone', '-q', '--bare', self.up, self.upstream)
        git(self.tmp, 'clone', '-q', '--bare', self.fork_work, self.fork)
        self.fork_head = git(self.fork, 'rev-parse', update_cui.BRANCH)
        # The app checkout: only its pin file matters here.
        self.repo = self.tmp / 'app'
        (self.repo / 'cmake').mkdir(parents=True)
        self.pin = self.repo / 'cmake' / 'cui-pin.cmake'
        self.pin.write_text(update_cui.pin_text(REAL_FORK, self.fork_head), encoding='utf-8', newline='\n')
        self.before = self.pin.read_bytes()
        self.main_root = self.tmp / 'main'
        self.main_root.mkdir()

    def run_update(self, commit_arg, **over):
        args = dict(repo=self.repo, fork=str(self.fork), upstream=str(self.upstream), work=self.tmp / 'clone',
                    main_root=self.main_root, log=lambda *_: None)
        args.update(over)
        return update_cui.update(commit_arg, **args)

    def assert_nothing_changed(self):
        self.assertEqual(self.pin.read_bytes(), self.before)
        self.assertEqual(git(self.fork, 'rev-parse', update_cui.BRANCH), self.fork_head)
        self.assertFalse((self.main_root / '.agent-work').exists())

    def test_without_network_it_stops_before_changing_anything(self):
        for broken in ('fork', 'upstream'):
            over = {broken: str(self.tmp / 'nowhere.git')}
            with self.assertRaises(update_cui.Stop) as stop:
                self.run_update(self.base, **over)
            self.assertEqual(stop.exception.code, update_cui.EXIT_NO_NETWORK)
            self.assertIn('Nothing was changed', str(stop.exception))
            self.assert_nothing_changed()
        self.assertFalse((self.tmp / 'clone').exists())

    def test_main_returns_the_offline_exit_code(self):
        done = subprocess.run([sys.executable, str(ROOT / 'tools' / 'update_cui.py'), self.base, '--fork', str(self.tmp / 'nowhere.git'),
                               '--upstream', str(self.upstream), '--repo', str(self.repo), '--main-root', str(self.main_root)],
                              capture_output=True, text=True, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        self.assertEqual(done.returncode, update_cui.EXIT_NO_NETWORK)
        self.assertIn('Nothing was changed', done.stderr)
        self.assert_nothing_changed()

    def test_marker_missing_after_a_clean_rebase_is_refused(self):
        # A fork branch whose commit does not carry the marker at all.
        commit(self.fork_work, 'src/cui_textedit.c', 'int plain(void);\n', 'drop the fix')
        git(self.fork_work, 'push', '-q', '-f', str(self.fork), update_cui.BRANCH)
        self.fork_head = git(self.fork, 'rev-parse', update_cui.BRANCH)
        self.pin.write_text(update_cui.pin_text(REAL_FORK, self.fork_head), encoding='utf-8', newline='\n')
        self.before = self.pin.read_bytes()
        newer = commit(self.up, 'src/other.c', 'int other;\n', 'upstream moves on')
        git(self.up, 'push', '-q', str(self.upstream), 'master')
        with self.assertRaises(update_cui.Stop) as stop:
            self.run_update(newer)
        self.assertEqual(stop.exception.code, update_cui.EXIT_NO_MARKER)
        self.assertIn(update_cui.MARKER, str(stop.exception))
        self.assert_nothing_changed()

    def test_conflict_is_refused_with_a_clear_message(self):
        # The fix appends to src/cui_textedit.c; upstream changes the same place.
        newer = commit(self.up, 'src/cui_textedit.c', 'int something_else(void);\n', 'upstream edits the same place')
        git(self.up, 'push', '-q', str(self.upstream), 'master')
        with self.assertRaises(update_cui.Stop) as stop:
            self.run_update(newer)
        self.assertEqual(stop.exception.code, update_cui.EXIT_CONFLICT)
        self.assertIn('does not rebase', str(stop.exception))
        self.assert_nothing_changed()

    def test_a_commit_that_is_not_upstreams_is_refused(self):
        with self.assertRaises(update_cui.Stop) as stop:
            self.run_update(self.fork_head)
        self.assertEqual(stop.exception.code, 1)
        self.assert_nothing_changed()

    def test_happy_path_rebases_pushes_pins_and_exports_the_tree(self):
        newer = commit(self.up, 'src/other.c', 'int other;\n', 'upstream moves on')
        git(self.up, 'push', '-q', str(self.upstream), 'master')
        head = self.run_update(newer)
        # The fork's branch is upstream's new commit plus the fix, and the fix is still in it.
        self.assertEqual(git(self.fork, 'rev-parse', update_cui.BRANCH), head)
        git(self.fork, 'merge-base', '--is-ancestor', newer, head)
        self.assertEqual(git(self.fork, 'rev-list', '--count', f'{newer}..{head}'), '1')
        self.assertIn(update_cui.MARKER, git(self.fork, 'show', f'{head}:src/cui_textedit.c'))
        # The pin is the new commit, in the file format the build and the queue read.
        self.assertEqual(update_cui.read_pin(self.pin), {'REPOSITORY': str(self.fork), 'COMMIT': head})
        self.assertTrue(self.pin.read_text(encoding='utf-8').startswith('# WRITTEN BY tools/update_cui.py'))
        # The queue's tree: files of the commit, no .git, folder named by the first 7 characters.
        tree = self.main_root / '.agent-work' / f'cui-src-{head[:7]}'
        self.assertEqual((tree / 'src/other.c').read_text(encoding='utf-8'), 'int other;\n')
        self.assertIn(update_cui.MARKER, (tree / 'src/cui_textedit.c').read_text(encoding='utf-8'))
        self.assertFalse((tree / '.git').exists())
        self.assertFalse((self.tmp / 'clone').exists())
        sys.path.insert(0, str(ROOT / 'tests'))
        import queue_check
        self.assertEqual(queue_check.cui_source(self.main_root, self.pin), tree)

    def test_the_committed_pin_is_the_fork_in_the_script_format(self):
        pin = update_cui.read_pin(ROOT / 'cmake' / 'cui-pin.cmake')
        self.assertEqual(pin['REPOSITORY'], update_cui.FORK)
        self.assertRegex(pin['COMMIT'], r'^[0-9a-f]{40}$')
        self.assertEqual((ROOT / 'cmake' / 'cui-pin.cmake').read_text(encoding='utf-8'),
                         update_cui.pin_text(pin['REPOSITORY'], pin['COMMIT']))


if __name__ == '__main__':
    unittest.main()
