"""Build and run the sandbox-only operations checks without console windows."""
import os
from pathlib import Path
import subprocess
import sys

project = Path(__file__).resolve().parent.parent
task = project / '.agent-work' / 'ops-v1'
build = task / 'build'
build.mkdir(parents=True, exist_ok=True)
(task / 'tmp').mkdir(parents=True, exist_ok=True)
os.environ['TEMP'] = os.environ['TMP'] = str(task / 'tmp')
os.environ['VSCMD_SKIP_SENDTELEMETRY'] = '1'
vc = r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
sources = [project / 'tests' / 'test_ops.c']
sources += [project / 'src' / f'{name}.c' for name in ['ops', 'clip', 'trash', 'search', 'recent', 'shell']]
args = ['cl', '/nologo', '/std:c17', '/W4', '/WX', '/utf-8', '/O1', '/MD',
        '/DUNICODE', '/D_UNICODE', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX',
        '/I' + str(project / 'src'), *map(str, sources), '/Fe:ops-tests.exe', '/link',
        'user32.lib', 'shell32.lib', 'ole32.lib', 'oleaut32.lib', 'uuid.lib', 'shlwapi.lib', 'propsys.lib']
script = build / 'run.cmd'
script.write_text('@echo off\ncall "' + vc + '" >nul\nif errorlevel 1 exit /b %errorlevel%\n' +
                  subprocess.list2cmdline(args) + '\nif errorlevel 1 exit /b %errorlevel%\n.\\ops-tests.exe\n', encoding='utf-8')
result = subprocess.run(['cmd.exe', '/d', '/c', str(script)], cwd=build,
                        creationflags=subprocess.CREATE_NO_WINDOW, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
sys.stdout.buffer.write(result.stdout)
sys.exit(result.returncode)
