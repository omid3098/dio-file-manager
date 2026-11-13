# Current timer repair — acceptance pending

The e4cccb7 build passed view 11/11, input 20/20, actions 7/7, search 8/8 and
jobs 5/5. Alternating CPU scroll: main 1.665781, 5.101433, 4.580122, 6.041233,
2.810808; branch 5.934114, 2.342356, 3.695558, 2.342143, 4.164184. Idle main
.026033, .052070, .052068, .078104, .078097; branch .052068, .052065, .052066,
.078099, .026036. Fixed ceilings remain 5.0/.5 without exclusions.

A separate 30-second scroll probe counted 15 queries / 826 frames on main and
15 queries / 829 frames on the branch. Both refreshed via a two-second throttle
in drawing code. Worker completion polling could add frames. The repair moves
the 2.5-second interval into the event-waiting worker, preserves immediate refresh
after file operations, and posts a UI notification only when the count changes.
Drawing reads cached state only. No pacing, input rate or budget change.
Runtime assertions cover scroll/idle query counts and no idle drawing for an
unchanged badge. Acceptance reruns pending.

---

# View slider validation — incomplete

## Acceptance worker v2 — NOT READY TO LAND

Rebased onto main `7f805a855d66e6599cf248e1e53e90fe081da931`, preserving
concurrent jobs, Recent removal and removable-drive eject. Only tests/README.md
conflicted; both acceptance sections were retained. No merge, push or install.

The fixed budgets are scrolling **5.0%** and idle **0.5%** of one core, in every
one of five windows. These supersede the older largest-main-sample rule below.

### Before the worker reuse repair

Measurements alternate main then branch for each window, same 20,000 files,
1080 x 720 logical window, 64 px grid icons, 7 columns, 128 px rows, light theme,
private desktop, 601 wheel messages per 30 s scrolling window (20 Hz).
Idle windows last 60 s. The controller imports the unmodified shared
cpu_budget.py ProcessTree and measure functions and checks all five windows.
No screenshot occurs inside a window. Its source is cpu_alternating.py in the task
folder. Main is an instrumented archive of a181303 (per-frame diagnostic file
writes); branch is the inherited 6a25ecc executable. Therefore these are diagnosis
samples, not final acceptance of the rebased repair. One main sample also exceeds
the fixed budget; the budget has not been raised.

| Build | Scenario | All five samples (% of one core) |
|---|---|---|
| main | scroll | 2.602421, 4.060275, 4.008167, 5.986616, 1.926060 |
| main | idle | 0.104130, 0.078099, 0.026033, 0.026034, 0.156185 |
| branch | scroll | 5.361122, 5.153258, 5.204778, 5.777777, 4.892551 |
| branch | idle | 0.052069, 0.026035, 0.026034, 0.078098, 0.026033 |

### Per-frame comparison and cause

The first frame probe wrote each frame to disk and could disturb timing. Its
raw files are retained but its frame rates are not acceptance evidence. The second
probe buffers counters in memory and writes only on exit; it compares current
main 7f805a8 with the rebased branch before the worker reuse repair. Same folder,
view, geometry and 20 Hz wheel rate, alternating main/branch twice; 400 wheel
messages each, after a 3 s settle, with startup/settle frames included in totals.

- counter-branch-1794563972307716700.profile: 591 frames; 3134465 UI-thread cycles/frame; 184.30 nodes/frame; 79.48 hits/frame; 88.23 grid text measures/frame
- counter-branch-1794564020308870600.profile: 570 frames; 3110365 UI-thread cycles/frame; 184.35 nodes/frame; 79.53 hits/frame; 88.33 grid text measures/frame
- counter-main-1794563948240998100.profile: 574 frames; 3670385 UI-thread cycles/frame; 184.41 nodes/frame; 80.55 hits/frame; 88.38 grid text measures/frame
- counter-main-1794563996352509100.profile: 572 frames; 3610702 UI-thread cycles/frame; 184.33 nodes/frame; 80.50 hits/frame; 88.27 grid text measures/frame

The List view creates only visible columns, using fixed 240 px width; no scan of
all row text exists. The closed popover returns before building its contents.
The comparison does not establish extra UI work per frame as the CPU cause.

The branch starts a fresh COM-initialized thread for each Trash badge query;
main queries on its already-initialized UI thread. A direct hidden-desktop probe
(trash_profile.py and trash-profile.log) isolated that lifetime cost. After the
first cold call, fresh-thread queries took 13.7385–14.8795 ms; subsequent queries
on one initialized thread took 3.9237–4.8005 ms. Query CPU cycles dropped from
30.13–33.36 million to 10.53–10.80 million. Returned counts matched (57) in all
10 calls. The new app code keeps one read-only worker and auto-reset event for
the process lifetime, retaining the shell's per-thread cache. Creation/signal
failures release/reset state so later refreshes can retry. No pacing was changed.
This identifies avoidable work; its effect on the full app budget is UNCONFIRMED.

### Checks and continuation

Before rebase/worker reuse: focused view_smoke.py 1-stops-open **1/1 passed**,
including selection, real folder double-click and Back at all six stops. The
inherited rename anchor repair already addresses the Details failure; neither the
check nor its timing was loosened. Full view suite has not run in this worker.

After rebase/worker reuse: MinSizeRel build **passed**, executable **448000 bytes**
at build/accept/MinSizeRel/dio-file-manager.exe. New clean main baseline built at
.agent-work/viewzoom-v2/clean-main/build/MinSizeRel/dio-file-manager.exe. No complete
use suite or post-repair CPU budget has run; fresh final dark/light capture and
inspection are pending. The worker reached its approximately 40-step cap and is
handing off rather than claiming readiness.

Task artifacts and continuation scripts: main checkout .agent-work/viewzoom-v2/.
Set DFM_EXE to the accept executable and DFM_USE_WORK to this task folder. Run
suites.py once (all five *_smoke.py, each with isolated artifacts), cpu_final.py
--final (alternating clean main and repaired branch, fixed ceilings), capture.py
and popover.py, then inspect every requested picture. These continuation scripts
are prepared and syntax-checked, not executed. Ensure main is still the base first.

---

# Historical validation before v2 (superseded budget and status)

# View slider validation â€” incomplete

The branch was rebased onto `4b197cb` (safe field keys and no-launch test mode).
No merge, push, install, or default file handler launch was performed.

## CPU

Percentages are of one core from the shared `cpu_budget.py` process-tree meter.
Same 64 px grid, 20,000 sandbox text files, private desktop; idle is five 60 s
windows, scrolling is five 30 s windows at 20 wheel messages/s. Every sample
must pass. Idle ceiling 0.5%; branch scrolling ceiling is main's largest sample,
4.736321%. The baseline's scroll meter had a 100% collection ceiling; that is
not the branch acceptance limit.

| Build/pacing | Idle, five samples (%) | Scrolling, five samples (%) |
|---|---|---|
| main 4b197cb | .052044, 0, 0, 0, .156079 | 3.018995, 3.695544, 3.586709, 4.736321, 3.591298 |
| branch, original no-vsync | 0, 0, .052051, .052054, 0 | 16.757388, 14.625813, 16.080924, 16.082548, 15.872737 |
| branch, restored normal display pacing | .104108, 0, .052051, 0, .104094 | 6.610891, 3.695945, 4.164496, 4.790560, 6.767006 |
| branch, experimental 30 fps cap | .052053, 0, 0, 0, .156190 | 4.997016, 5.049393, 4.632715, 4.840683, 4.736985 |

All idle windows passed. Scrolling failed on all three branch attempts. Repairs
stopped at the requested three-failure limit. A fixed frame cap alone has not
proved equivalent to main's event/render workload. The 30 fps experiment also
missed slider endpoints, and has been reverted to normal display pacing.

## Real input

All checks used `--test-state`; file opens write `.opened` logs and actions use
explicit stand-ins for Open With/Terminal. Clipboard preservation uses the
machine-shared named mutex through `clipboard_guard.py`.

- Normal display pacing: view 10/11, input 20/20, search 8/8, actions 7/7.
  The Details folder double-click failed; all six 50 ms performance checks passed.
- Last measured 30 fps experiment: view 9/11, input 20/20, search 8/8, actions 7/7.
  Two view checks failed dragging to Details (stopped at List). All six 50 ms
  performance checks passed with maximum messages under 1.3 ms.
- Startup Trash badge was present in .534 s on the last input run; Extra-large
  overscan wheel assertion, field-key ownership and no-launch checks passed.

The geometry-only rename anchor no longer registers as an interactive hit target:
it covered the selected Details row. That change passed input/actions in the
30 fps run, but the failing Details double-click has not been proved again: that
run failed at the slider before reaching it. The attempted driver timing change
was reverted. The final rollback build/captures are checked; use checks and CPU
were not rerun after rollback, respecting the repair stop. Do not merge yet.

Artifacts remain in `.agent-work/viewzoom-v1` of the main checkout: CPU JSON/logs,
view/input/search/actions JSON, failure captures, and `final/` light/dark captures.
