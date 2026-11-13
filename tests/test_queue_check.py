"""Guard the release configuration and the offline CUI override."""
from pathlib import Path
import os
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import queue_check


class QueueBuildTest(unittest.TestCase):
    def test_use_checks_run_and_remove_only_their_own_folder(self):
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ):
            root = Path(directory)
            owned = root / '.agent-work/merge-queue-checks'
            active = owned / 'other-active-run'
            active.mkdir(parents=True)
            (active / 'keep').touch()
            folders, checks = [], []

            def build(path, *_):
                folders.append(path.parent)
                self.assertEqual(path.parent.parent, owned)
                self.assertTrue(path.parent.is_dir())

            def run(argv, **_):
                if argv[0] == 'git':
                    return SimpleNamespace(stdout=str(root / '.git'))
                checks.append(Path(argv[1]).name)
                self.assertEqual(Path(os.environ['DFM_EXE']),
                                 folders[-1] / 'build/MinSizeRel/dio-file-manager.exe')

            with patch.object(queue_check, 'run', side_effect=run), \
                    patch.object(queue_check, 'build_candidate', side_effect=build), \
                    patch('sys.argv', ['queue_check.py', 'changed-safe', '--use-checks',
                                       'input_smoke.py', 'search_smoke.py', 'actions_smoke.py']):
                queue_check.main()
                queue_check.main()
            self.assertEqual(checks, list(queue_check.USE_CHECKS) * 2)
            self.assertNotEqual(folders[0], folders[1])
            self.assertTrue(all(not path.exists() for path in folders))
            self.assertTrue((active / 'keep').exists())

            with patch.object(queue_check, 'run', side_effect=run), \
                    patch.object(queue_check, 'build_candidate', side_effect=RuntimeError('build failed')), \
                    patch('sys.argv', ['queue_check.py', 'typecheck']):
                with self.assertRaisesRegex(RuntimeError, 'build failed'):
                    queue_check.main()
            self.assertEqual(list(owned.iterdir()), [active])

    def test_build_uses_local_cui_and_release_size_guard(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'CMakeLists.txt').touch()
            with patch.object(queue_check, 'run') as run:
                queue_check.build_candidate(root / 'build', root, root / 'console')
            configure, build = [call.args[0] for call in run.call_args_list]
            self.assertIn(f'-DFETCHCONTENT_SOURCE_DIR_CUI={root}', configure)
            self.assertIn('-DFETCHCONTENT_FULLY_DISCONNECTED=ON', configure)
            self.assertIn(f'-DDFM_TEST_WORK={root / "console"}', configure)
            self.assertEqual(build[build.index('--config') + 1], 'MinSizeRel')
            self.assertNotIn('--target', build)  # app's POST_BUILD size guard and console tests

    def test_missing_cache_refuses_download(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with patch.object(queue_check, 'run') as run:
                with self.assertRaises(FileNotFoundError):
                    queue_check.build_candidate(root / 'build', root, root / 'console')
                run.assert_not_called()


if __name__ == '__main__':
    unittest.main()
