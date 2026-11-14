# Dio File Manager

A small native file manager for Windows, written in C with the CUI library
(github.com/yappologistic/CUI, MIT). It works and looks like GNOME Files
(Nautilus), in Dio's colours.

Build with MSVC 2022 and CMake:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config MinSizeRel --target dio-file-manager -j 8
build\MinSizeRel\dio-file-manager.exe
```

Pass `--path DIR` to start in a folder. The release build refuses an exe above
800 KB. The window supports grid and list views, folder navigation, selection,
path entry, hidden files, and the sidebar overlay and a bottom bar below 683 px.

Run the real-input smoke checks with `python tests/use/input_smoke.py` through
the shared `waitfor.py` runner. They create fixtures only under
`.agent-work/use-v1`, including 20,000 empty files, and launch the app on the
shared `correct/hidden.py` private desktop. Captures, JSON state and results go
there too. File opening with default apps is deliberately checked by source
inspection, so tests never launch other apps.

`--test-state BASE` enables an observation switch used by those checks:
creating `BASE.request` writes `BASE.json` and `BASE.png`. This mode ignores
automatic mouse-leave events on the inactive desktop, where there is no physical
cursor; mouse, wheel and key actions still enter through the Win32 window.
Normal runs do not install this test filter. Backspace does not navigate.

`--shot OUT.png` renders a hidden window. Optional arguments are `--path DIR`,
`--view grid|list`, `--theme light|dark`, `--size WxH`, and `--select NAME`.
