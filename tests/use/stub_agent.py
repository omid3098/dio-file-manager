"""A stand-in agent for the checks: reads the prompt on stdin, answers like a real agent in the mode named in the file
DFM_STUB_MODE (ok, broken, slow, collide, illegal). It writes its pid to DFM_STUB_PID so a check can see that Cancel
and the timeout really ended it."""
import json
import os
import re
import sys
import time

prompt = sys.stdin.buffer.read().decode('utf-8', 'replace')
pid_file = os.environ.get('DFM_STUB_PID')
if pid_file:
    open(pid_file, 'w').write(str(os.getpid()))
if pid_file:
    open(pid_file + '.prompt', 'wb').write(prompt.encode('utf-8'))
mode_file = os.environ.get('DFM_STUB_MODE')
mode = open(mode_file).read().strip() if mode_file and os.path.exists(mode_file) else 'ok'
files = []   # (name, snippet)
for m in re.finditer(r'^\d+\. (.+)\n(?:   the file starts with: (.*)\n)?', prompt, re.M):
    files.append((m.group(1), m.group(2) or ''))
if mode == 'slow':
    time.sleep(600)
if mode == 'broken':
    sys.stdout.write('Here you go: {"names":[{"old":"' + (files[0][0] if files else 'x') + '","new":"Beach')
    sys.exit(0)
out = []
for i, (name, snip) in enumerate(files):
    stem, dot, ext = name.rpartition('.')
    ext = dot + ext if dot else ''
    if re.search(r'[؀-ۿ]', snip):
        new = 'خلاصه گزارش فروش' + ext
    elif mode == 'collide':
        new = 'existing' + ext   # a name taken on disk (existing.jpg), and the same name asked for many files
    elif mode == 'illegal':
        new = '..\evil/dir\Trip: "Rome" <1> | what?*' + ext if i == 0 else 'CON' + ext if i == 1 else 'Holiday ' + str(i) + ' . ' + ext
    else:
        new = 'Sample %d' % (i + 1) + ext
    out.append({'old': name, 'new': new})
sys.stdout.buffer.write(('```json\n' + json.dumps({'names': out}, ensure_ascii=False) + '\n```\n').encode('utf-8'))
