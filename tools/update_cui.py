"""The one supported way to move Dio File Manager to a newer CUI.

    python tools/update_cui.py <upstream-commit>

The app builds against omid3098's fork of yappologistic/CUI, branch dfm-rtl-input: upstream plus pull request #12, which makes
text boxes join and edit Persian right to left. Pointing the build at plain upstream drops that fix, so the pin
(cmake/cui-pin.cmake) is written only by this script. It:

1. stops, changing nothing, when the fork or upstream cannot be reached;
2. rebases dfm-rtl-input onto the upstream commit (stops on a conflict and leaves the fork as it was);
3. stops when the rebased tree lacks `cui__paint_edit_shaped` (src/cui_textedit.c);
4. pushes the branch to the fork with --force-with-lease;
5. writes cmake/cui-pin.cmake;
6. refreshes the read-only tree <main checkout>/.agent-work/cui-src-<7 characters> that the merge queue builds from.

Exit codes: 0 done, 2 no network, 3 rebase conflict, 4 marker missing, 5 nothing left to carry, 1 other failure.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

FORK = 'https://github.com/omid3098/CUI-yappologistic.git'
UPSTREAM = 'https://github.com/yappologistic/CUI.git'
BRANCH = 'dfm-rtl-input'
MARKER = 'cui__paint_edit_shaped'
MARKER_FILE = 'src/cui_textedit.c'
REPO = Path(__file__).resolve().parents[1]

EXIT_NO_NETWORK, EXIT_CONFLICT, EXIT_NO_MARKER, EXIT_NOTHING_TO_CARRY = 2, 3, 4, 5


class Stop(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


def git(args, cwd=None, check=True):
    """Runs git with no console window; returns the finished process (text output)."""
    flags = getattr(subprocess, 'CREATE_NO_WINDOW', 0)
    env = dict(os.environ, GIT_TERMINAL_PROMPT='0')
    done = subprocess.run(['git', *map(str, args)], cwd=cwd, capture_output=True, text=True, encoding='utf-8',
                          errors='replace', creationflags=flags, env=env)
    if check and done.returncode:
        raise Stop(1, f'git {" ".join(map(str, args))} failed: {(done.stderr or done.stdout).strip()}')
    return done


def remove_tree(path):
    """Deletes a folder that holds a git clone: git writes its pack files read-only, which Windows refuses to delete."""
    def writable(func, target, _error):
        os.chmod(target, 0o700)
        func(target)
    shutil.rmtree(path, onexc=writable)


def main_checkout(repo):
    """The folder that holds the repository's .git: the parent of the common git dir."""
    common = git(['rev-parse', '--path-format=absolute', '--git-common-dir'], cwd=repo).stdout.strip()
    return Path(common).parent


def pin_text(repository, commit):
    return (
        '# WRITTEN BY tools/update_cui.py. Do not edit by hand.\n'
        '# This pins CUI to omid3098\'s fork, branch dfm-rtl-input: upstream plus yappologistic/CUI pull request #12, the right-to-left\n'
        '# text box editing. A hand edit that points this at upstream drops the Persian text box fix: Persian typed into a text box\n'
        '# draws unjoined and left to right, and CMakeLists.txt stops the configure.\n'
        '# To move CUI to a newer upstream commit, run:   python tools/update_cui.py <upstream-commit>\n'
        f'set(DFM_CUI_REPOSITORY "{repository}")\n'
        f'set(DFM_CUI_COMMIT "{commit}")\n')


def read_pin(pin_file):
    text = Path(pin_file).read_text(encoding='utf-8')
    return {key: re.search(rf'set\(DFM_CUI_{key}\s+"([^"]*)"\)', text).group(1) for key in ('REPOSITORY', 'COMMIT')}


def export_tree(clone, commit, target):
    """A plain copy of the commit's files (no .git) at target, made through a temp folder and renamed into place."""
    target = Path(target)
    part = target.with_name(target.name + '.part')
    if part.exists():
        shutil.rmtree(part)
    part.mkdir(parents=True)
    archive = part.with_suffix('.zip')
    git(['archive', '--format=zip', f'--output={archive}', commit], cwd=clone)
    with zipfile.ZipFile(archive) as bundle:
        bundle.extractall(part)
    archive.unlink()
    part.rename(target)


def update(commit_arg, repo=REPO, fork=FORK, upstream=UPSTREAM, work=None, main_root=None, log=print):
    repo = Path(repo)
    main_root = Path(main_root) if main_root else main_checkout(repo)
    work = Path(work) if work else main_root / '.agent-work' / 'cui-update-clone'
    pin_file = repo / 'cmake' / 'cui-pin.cmake'
    if not pin_file.is_file():
        raise Stop(1, f'{pin_file} is missing')

    # 1. Both remotes must answer before any file changes.
    for name, url in (('fork', fork), ('upstream', upstream)):
        if git(['ls-remote', '--heads', url], check=False).returncode:
            raise Stop(EXIT_NO_NETWORK, f'Cannot reach the {name} ({url}). Nothing was changed. '
                                        'Run this again when the network works.')
    old_fork_head = git(['ls-remote', '--heads', fork, BRANCH]).stdout.split()
    if not old_fork_head:
        raise Stop(1, f'The fork {fork} has no branch {BRANCH}. Nothing was changed.')
    old_fork_head = old_fork_head[0]

    # 2. A clone of the fork that also knows upstream; every run starts it clean.
    if work.exists():
        remove_tree(work)
    work.parent.mkdir(parents=True, exist_ok=True)
    git(['clone', '--quiet', '--no-checkout', fork, work])
    git(['remote', 'add', 'upstream', upstream], cwd=work)
    git(['fetch', '--quiet', 'upstream'], cwd=work)
    resolved = git(['rev-parse', '--verify', '--quiet', f'{commit_arg}^{{commit}}'], cwd=work, check=False)
    if resolved.returncode:
        raise Stop(1, f'{commit_arg} is not a commit of upstream {upstream}. Nothing was changed.')
    new_base = resolved.stdout.strip()
    if not git(['for-each-ref', '--contains', new_base, 'refs/remotes/upstream'], cwd=work).stdout.strip():
        raise Stop(1, f'{commit_arg} is not on any branch of upstream {upstream} (it may come from the fork). Nothing was changed.')
    git(['checkout', '--quiet', '-B', BRANCH, f'origin/{BRANCH}'], cwd=work)

    # 3. Rebase. A conflict leaves the fork and this checkout as they were.
    log(f'Rebasing {BRANCH} onto {new_base[:7]} ...')
    rebased = git(['-c', 'user.name=update_cui', '-c', 'user.email=update_cui@invalid', 'rebase', new_base],
                  cwd=work, check=False)
    if rebased.returncode and not ((work / '.git' / 'rebase-merge').exists() or (work / '.git' / 'rebase-apply').exists()):
        raise Stop(1, f'git rebase failed: {(rebased.stderr or rebased.stdout).strip()}. Nothing was changed.')
    if (work / '.git' / 'rebase-merge').exists() or (work / '.git' / 'rebase-apply').exists():
        git(['rebase', '--abort'], cwd=work, check=False)
        raise Stop(EXIT_CONFLICT, f'{BRANCH} does not rebase onto {new_base[:7]}: the fix touches code that upstream changed. '
                                  f'Nothing was pushed and no file changed. Rebase {BRANCH} by hand in {work} (git rebase {new_base[:7]}), '
                                  'then push it to the fork and put the commit in cmake/cui-pin.cmake.')
    head = git(['rev-parse', 'HEAD'], cwd=work).stdout.strip()
    carried = git(['rev-list', '--count', f'{new_base}..{head}'], cwd=work).stdout.strip()

    # 4. The fix must still be in the rebased tree.
    shown = git(['show', f'{head}:{MARKER_FILE}'], cwd=work, check=False)
    if MARKER not in shown.stdout:
        raise Stop(EXIT_NO_MARKER, f'The rebased tree has no {MARKER} in {MARKER_FILE}: the right-to-left text field fix is gone '
                                   f'(the rebase dropped it or upstream changed it). Nothing was pushed and no file changed.')
    if carried == '0':
        raise Stop(EXIT_NOTHING_TO_CARRY, f'{BRANCH} carries no commit of its own on {new_base[:7]}: upstream already holds the fix. '
                                          'The fork is no longer needed; pin upstream by hand and drop the fork check in CMakeLists.txt. '
                                          'Nothing was pushed and no file changed.')

    # 5. Push, then pin.
    log(f'Pushing {BRANCH} ({head[:7]}) to the fork ...')
    pushed = git(['push', '--quiet', f'--force-with-lease={BRANCH}:{old_fork_head}', fork, f'HEAD:refs/heads/{BRANCH}'],
                 cwd=work, check=False)
    if pushed.returncode:
        raise Stop(1, f'The push to the fork failed (someone moved {BRANCH}, or no write access): '
                      f'{(pushed.stderr or pushed.stdout).strip()}. No file changed.')
    pin_file.write_text(pin_text(fork, head), encoding='utf-8', newline='\n')

    # 6. The read-only tree the merge queue builds from.
    tree = main_root / '.agent-work' / f'cui-src-{head[:7]}'
    if tree.exists():
        log(f'{tree} already exists; left as it is.')
    else:
        export_tree(work, head, tree)
    remove_tree(work)
    log(f'CUI is now {fork} at {head}.\nPin: {pin_file}\nQueue tree: {tree}\n'
        'Next: build, run tests/use/persian_input_smoke.py, commit cmake/cui-pin.cmake and the docs that name the commit.')
    return head


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('commit', help='the upstream (yappologistic/CUI) commit to move to')
    ap.add_argument('--fork', default=FORK, help=argparse.SUPPRESS)
    ap.add_argument('--upstream', default=UPSTREAM, help=argparse.SUPPRESS)
    ap.add_argument('--repo', default=REPO, help=argparse.SUPPRESS)
    ap.add_argument('--work', help=argparse.SUPPRESS)
    ap.add_argument('--main-root', help=argparse.SUPPRESS)
    args = ap.parse_args(argv)
    try:
        update(args.commit, args.repo, args.fork, args.upstream, args.work, args.main_root)
    except Stop as stop:
        print(f'update_cui: {stop}', file=sys.stderr)
        return stop.code
    return 0


if __name__ == '__main__':
    sys.exit(main())
