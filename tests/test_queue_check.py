"""Guard the release configuration and the offline CUI override."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import queue_check


class QueueBuildTest(unittest.TestCase):
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
