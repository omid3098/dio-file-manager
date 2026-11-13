"""Proof for recycle_guard: it removes a real Recycle Bin item the check made, and nothing that is not its own."""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import recycle_guard as g


def info(path, when, version=2):
    ft = int((when + g.EPOCH_DIFF) * 1e7)
    if version == 2:
        return struct.pack('<qqqi', 2, 5, ft, len(path) + 1) + path.encode('utf-16-le') + b'\0\0'
    return struct.pack('<qqq', 1, 5, ft) + path.encode('utf-16-le').ljust(520, b'\0')


class Matching(unittest.TestCase):
    def test_only_own_items_under_the_work_folder_and_after_the_start(self):
        with tempfile.TemporaryDirectory() as t:
            now = time.time()
            work = r'E:\w\x\.agent-work\suite'
            items = {
                'own': (work + r'\sandbox\a.txt', now, 2),
                'ownv1': (work + r'\sandbox\b.txt', now, 1),
                'sibling': (work + r'-other\a.txt', now, 2),       # shares the text prefix, not the folder
                'elsewhere': (r'E:\Users\Omid\Documents\keep.txt', now, 2),
                'before': (work + r'\old.txt', now - 3600, 2),      # an earlier run's item
            }
            for name, (path, when, version) in items.items():
                Path(t, '$I' + name).write_bytes(info(path, when, version))
                Path(t, '$R' + name).write_text('data')
            removed = g.purge([t], work, now - 5)
            self.assertCountEqual(removed, [items['own'][0], items['ownv1'][0]])
            left = sorted(p.name for p in Path(t).iterdir())
            self.assertEqual(left, sorted(f'${k}{n}' for n in ('sibling', 'elsewhere', 'before') for k in 'IR'))

    def test_real_recycle_bin_round_trip(self):
        work = Path(tempfile.mkdtemp(prefix='dfm-recycle-', dir=os.environ.get('DFM_USE_WORK') or None))
        victim = work / 'trashed.txt'
        victim.write_text('x')
        since = time.time() - 2
        subprocess.run(['powershell', '-NoProfile', '-Command',
                        'Add-Type -AssemblyName Microsoft.VisualBasic; '
                        f"[Microsoft.VisualBasic.FileIO.FileSystem]::DeleteFile('{victim}', 'OnlyErrorDialogs', 'SendToRecycleBin')"],
                       check=True, creationflags=subprocess.CREATE_NO_WINDOW)
        self.assertFalse(victim.exists())
        dirs = g.bin_dirs_for(work)

        def found():
            return [p for d in dirs for p in d.iterdir() if p.name.startswith('$I')
                    and (g.read_original(p.read_bytes()) or ('',))[0].lower() == str(victim).lower()]
        self.assertEqual(len(found()), 1, 'the test item is in the Recycle Bin')
        self.assertEqual(g.purge(dirs, work, since), [str(victim)])
        self.assertEqual(found(), [])
        work.rmdir()


if __name__ == '__main__':
    unittest.main()
