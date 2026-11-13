"""Candidate-local merge checks; keep artifacts in the main checkout's queue task."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
HIDDEN = Path(r'C:\Users\Omid\.agents\source\correct')
# Input/search can launch brokered default apps outside the hidden desktop.
SAFE_USE_CHECKS = ('actions_smoke.py',)


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
    run(['cmake', '--build', build, '--config', 'MinSizeRel', '--parallel', '2'])


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
    parser.add_argument('mode', choices=['typecheck', 'changed', 'full', 'console-inside'])
    parser.add_argument('base', nargs='?')
    parser.add_argument('head', nargs='?')
    args = parser.parse_args()
    common = run(['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'],
                 capture_output=True, text=True).stdout.strip()
    main_root = Path(common).parent
    task = main_root / '.agent-work/queue-v1'
    work = task / 'candidates' / ROOT.parent.name / ROOT.name
    (task / 'tmp').mkdir(parents=True, exist_ok=True)
    work.mkdir(parents=True, exist_ok=True)
    os.environ.update(TEMP=str(task / 'tmp'), TMP=str(task / 'tmp'), TMPDIR=str(task / 'tmp'))
    build = work / 'build'
    if args.mode == 'console-inside':
        console_checks(build)
        return
    build_candidate(build, main_root / '.agent-work/cui-src', work / 'console')
    if args.mode == 'typecheck':
        return
    os.environ['DFM_EXE'] = str(build / 'MinSizeRel/dio-file-manager.exe')
    for smoke in sorted((ROOT / 'tests/use').glob('*_smoke.py')):
        if smoke.name not in SAFE_USE_CHECKS:
            print(f'SKIPPED {smoke.name}: default-app launch isolation not proved', flush=True)
            continue
        os.environ['DFM_USE_WORK'] = str(work / args.mode / smoke.stem)
        run([sys.executable, smoke])
    if args.mode == 'full':
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
            process = popen([sys.executable, str(Path(__file__).resolve()), 'console-inside'],
                            ROOT, dict(os.environ), b'', out, err)
            try:
                code = process.wait(timeout=1200)
                if code:
                    raise subprocess.CalledProcessError(code, 'hidden console checks')
            finally:
                process.close()


if __name__ == '__main__':
    main()
