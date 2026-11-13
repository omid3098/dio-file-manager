"""Candidate-local merge checks with disposable, queue-owned build folders."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HIDDEN = Path(r'C:\Users\Omid\.agents\source\correct')
USE_CHECKS = ('input_smoke.py', 'search_smoke.py', 'actions_smoke.py')


def run(argv, **kwargs):
    print(subprocess.list2cmdline(list(map(str, argv))), flush=True)
    if not kwargs.get('capture_output'):
        kwargs.update(stdout=sys.stdout, stderr=sys.stderr)
    return subprocess.run(list(map(str, argv)), cwd=ROOT, check=True,
                          creationflags=subprocess.CREATE_NO_WINDOW, **kwargs)


def build_candidate(build, cui, console):
    if not (cui / 'CMakeLists.txt').is_file():
        raise FileNotFoundError(f'Local CUI v1.4.0 source required: {cui}')
    run(['cmake', '-S', ROOT, '-B', build, '-G', 'Visual Studio 17 2022', '-A', 'x64',
         '-DCMAKE_BUILD_TYPE=MinSizeRel', f'-DFETCHCONTENT_SOURCE_DIR_CUI={cui}',
         '-DFETCHCONTENT_FULLY_DISCONNECTED=ON', f'-DDFM_TEST_WORK={console}'])
    # Reused MSBuild nodes can recreate MSBuildTemp after the run folder is removed.
    run(['cmake', '--build', build, '--config', 'MinSizeRel', '--parallel', '2', '--', '/nodeReuse:false'])


def console_checks(build):
    sys.path.insert(0, str(ROOT / 'tests/use'))
    from input_smoke import App, WORK
    from clipboard_guard import preserved_clipboard
    (WORK / 'fixture').mkdir(parents=True, exist_ok=True)
    (WORK / 'appdata').mkdir(exist_ok=True)
    app = App('clipboard-owner', WORK / 'fixture')
    try:
        with preserved_clipboard(app.hwnd):
            run(['ctest', '--test-dir', build, '-C', 'MinSizeRel', '--output-on-failure'])
    finally:
        app.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['typecheck', 'changed-safe', 'full-safe', 'console-inside'])
    parser.add_argument('base', nargs='?')
    parser.add_argument('head', nargs='?')
    parser.add_argument('--use-checks', nargs='+', choices=USE_CHECKS, default=USE_CHECKS)
    parser.add_argument('--build', type=Path)
    args = parser.parse_args()
    if args.mode == 'console-inside':
        if args.build is None:
            parser.error('console-inside requires --build')
        console_checks(args.build)
        return
    common = run(['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'],
                 capture_output=True, text=True).stdout.strip()
    main_root = Path(common).parent
    task = main_root / '.agent-work/merge-queue-checks'
    task.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=args.mode + '-', dir=task) as directory:
        check_candidate(args, main_root, Path(directory))


def check_candidate(args, main_root, work):
    (work / 'tmp').mkdir()
    os.environ.update(TEMP=str(work / 'tmp'), TMP=str(work / 'tmp'), TMPDIR=str(work / 'tmp'))
    build = work / 'build'
    build_candidate(build, main_root / '.agent-work/cui-src', work / 'console')
    if args.mode == 'typecheck':
        return
    os.environ['DFM_EXE'] = str(build / 'MinSizeRel/dio-file-manager.exe')
    for name in args.use_checks:
        smoke_work = work / args.mode / Path(name).stem
        os.environ['DFM_USE_WORK'] = str(smoke_work)
        try:
            # Each driver's App passes --test-state; default file opens only write .opened.
            run([sys.executable, ROOT / 'tests/use' / name])
        finally:
            for log in sorted(smoke_work.glob('*.log')) + sorted(smoke_work.glob('*.err')):
                print(f'{name}: {log.name}\n{log.read_text(encoding="utf-8", errors="replace")}', flush=True)
    if args.mode == 'full-safe':
        run([sys.executable, '-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_queue_check.py'])
        gaming = Path(r'C:\Users\Omid\.agents\tools\gaming.py')
        code = subprocess.run([sys.executable, str(gaming), 'check'],
                              creationflags=subprocess.CREATE_NO_WINDOW).returncode
        if code == 3:
            run([sys.executable, gaming, 'wait', '--cap', '60'])
        elif code:
            raise subprocess.CalledProcessError(code, 'gaming check')
        os.environ['DFM_USE_WORK'] = str(work / 'full/console-owner')
        sys.path.insert(0, str(HIDDEN))
        from hidden import popen
        with (work / 'console.log').open('wb') as out, (work / 'console.err').open('wb') as err:
            process = popen([sys.executable, str(Path(__file__).resolve()), 'console-inside', '--build', str(build)],
                            ROOT, dict(os.environ), b'', out, err)
            try:
                code = process.wait(timeout=1200)
                if code:
                    raise subprocess.CalledProcessError(code, 'hidden console checks')
            finally:
                process.close()
                for log in (work / 'console.log', work / 'console.err'):
                    print(log.read_text(encoding='utf-8', errors='replace'), flush=True)


if __name__ == '__main__':
    main()
