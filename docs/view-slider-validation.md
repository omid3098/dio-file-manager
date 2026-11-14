# View slider validation — final worker checks passed

Base main: `7f805a855d66e6599cf248e1e53e90fe081da931` (unchanged).
Runtime repair: `cd36c03` restores the draw-driven Trash refresh from `e4cccb7`.
The persistent worker waits indefinitely between requests; it has no timer and
posts no redraw messages. Startup polling and file-operation invalidations remain.
No slider behavior, input rate or display pacing changed. No merge, push or install.

## Fixed acceptance rules

- Idle: at most **0.5% of one core in every one of five 60-second windows**.
- Scrolling: five alternating main/branch 30-second pairs. Branch mean CPU must
  be at most main mean plus **0.5 percentage points**. The former fixed 5.0% line
  is dropped because main itself exceeds it.
- Mean frames and mean Trash queries per 30-second scrolling window must each
  be within **2% of main**, across five alternating pairs with 600 wheel events
  per window. CPU uses clean binaries; counters use separate instrumented builds.
- MinSizeRel executable: at most **819,200 bytes**. Both requested use suites pass.

## Final clean CPU samples

All percentages are of one core. Each row ran main first, then branch; five scroll
pairs ran before five idle pairs. No samples were excluded. Only one clean CPU
comparison was run for this repair, through the v2 `cpu_final.py --final` driver
with a task-local adapter applying the parent's revised scrolling rule.

| Pair | Scroll main (30 s) | Scroll branch (30 s) | Idle main (60 s) | Idle branch (60 s) |
|---|---:|---:|---:|---:|
| 1 | 4.580946 | 6.089749 | 0.078097 | 0.026034 |
| 2 | 7.339328 | 4.841037 | 0.078105 | 0.026034 |
| 3 | 6.038210 | 7.495854 | 0.052069 | 0.026035 |
| 4 | 3.385178 | 2.290302 | 0.078101 | 0.156197 |
| 5 | 6.038471 | 7.130631 | 0.078104 | 0.078101 |

Scroll means: main **5.476427%**, branch
**5.569515%**; difference
**0.093088 points**, below 0.5. All five
branch idle samples pass; maximum **0.156197%**.
The wheel driver sent 600 or 601 events per CPU scrolling window; every window
advanced the viewport. No input pacing or rate was changed.

## Final frame and Trash-query samples

Same v2 counter instrumentation: Interlocked counters only, in disposable
git-archive builds. No captures or file output during each measured window.

| Pair | Main queries | Branch queries | Main frames | Branch frames |
|---|---:|---:|---:|---:|
| 1 | 15 | 15 | 803 | 795 |
| 2 | 15 | 15 | 818 | 816 |
| 3 | 15 | 15 | 797 | 821 |
| 4 | 15 | 15 | 812 | 792 |
| 5 | 15 | 15 | 801 | 819 |

Mean queries: **15.0 / 15.0** (main / branch), no difference.
Mean frames: **806.2 / 808.6**, branch
**0.297693%** higher, within 2%.

The first counter driver reset immediately after scrolling stopped and recorded
13 branch frames with zero Trash queries over its next 30 seconds. Its strict
zero-total-frame assertion failed. A separate settled-idle comparison then measured
both builds after three seconds with no input: **main 0 queries / 2 frames,
branch 0 queries / 2 frames**, each over 30 seconds. An initial baseline-only
attempt also found 0 / 2 on main. Thus total idle frames are not absolutely zero
even on main; Trash performs no queries and branch adds no idle frames. All raw
attempts are retained. `record.py` asserts zero queries and equal idle frames.

## Final use checks and executable

- `input_smoke.py`: **20/20 passed**, once. Startup badge read **58 items at
  0.594 seconds**, within its two-second case, before subsequent Trash operations.
- `view_smoke.py`: **11/11 passed**, once. All six message-delay checks passed;
  maximum **47.765 ms** against 50 ms.
- MinSizeRel build passed: **448,000 bytes**, below 819,200, at
  `build/accept/MinSizeRel/dio-file-manager.exe` in the slider worktree.

Every UI run used the private desktop and `--test-state`, with the gaming check
before launch. Existing clipboard cases retain `Local\DioFileManagerClipboardTest`.
The three other use suites were not rerun in this worker; their earlier 51/51
full-suite evidence and visual inspection are preserved below. No new visual
captures or independent review were requested for this timer rollback.

## Evidence

Task folder: `E:/w/dio-file-manager/.agent-work/viewzoom-v3/`.
Clean CPU: `cpu-final.json`, `cpu-final.log`, `cpu-final.err`, `cpu-bounded.log`.
Counters: `counts.json`, `counts.log`, `counts.err`, `counts-summary.json`,
`idle-counts.json`, `idle-counts.log`, and bounded run logs. The initial counter
failure remains in `check-exits.json`; `acceptance.json` and `record.py` evaluate
the complete final evidence, including the corrected settled-idle comparison.
Use suites: `input/results.json`, `input/input.log`, `view/view-results.json`,
`view/view.log`. Scripts adapt the read-only v2 drivers; instrumented archives,
their builds, fixtures, screenshots and logs remain in this task folder.

The earlier observations below are historical, including their obsolete 5% scroll
limit and timer failure; they do not describe this repaired build.

---

## Historical: timer repair failed acceptance

Base main remains 7f805a855d66e6599cf248e1e53e90fe081da931.
Runtime change: e6fb51c1643c3db0d9368c2471a152b528c958e9. A later documentation-only
commit records these results. No merge, push, install or pacing/budget changes.

The persistent worker alone failed scroll window 1 beside passing main. The timer
repair removes periodic queries and completion-poll wakeups from drawing. The
worker now waits 2.5 seconds between read-only queries; file-operation invalidations
signal its event immediately. It posts VIEWS_TRASH_CHANGED only on a count change,
handled on the UI thread. Queries are independent of drawing, but acceptance fails.

## Current timer repair

| Scenario | Build | Windows 1–5 (% one core) |
|---|---|---|
| scroll | main | 7.234520, 5.882183, 2.654764, 7.130557, 3.331526 |
| scroll | branch | 1.093018, 6.403350, 1.457474, 5.205175, 2.967153 |
| idle | main | 0.026035, 0.052069, 0.078105, 0.052069, 0.000000 |
| idle | branch | 0.494665, 0.416560, 0.234312, 0.312418, 0.598800 |

Scroll windows 2 and 4 failed on both builds: main/branch 5.882183/6.403350 and
7.130557/5.205175. The parent decides these adjacent baseline failures. Idle window
5 is an independent failure: branch .598800 exceeds .5 beside main zero. No sample
was excluded and no budget or test assertion was loosened.

## Worker reuse, before timer repair

| Scenario | Build | Windows 1–5 (% one core) |
|---|---|---|
| scroll | main | 1.665781, 5.101433, 4.580122, 6.041233, 2.810808 |
| scroll | branch | 5.934114, 2.342356, 3.695558, 2.342143, 4.164184 |
| idle | main | 0.026033, 0.052070, 0.052068, 0.078104, 0.078097 |
| idle | branch | 0.052068, 0.052065, 0.052066, 0.078099, 0.026036 |

Branch scroll window 1 failed 5.934114 beside main 1.665781. Initial raw evidence
is in before-timer/. Older workers' samples are in the original handover history;
the diagnosis-only measurements do not qualify as clean acceptance.

## Query/frame counts

Disposable git-archive builds add only Interlocked counters to window_frame and
SHQueryRecycleBinW. After settling, the driver resets them, sends 600 wheel events
at 20 Hz over 30 seconds, then reads counters. No file output or capture occurs in
the window. Counter reads extend wall time by 30–76 ms. Clean, uninstrumented
executables were used for CPU acceptance.

| Build | Before timer: queries / frames | After timer: queries / frames |
|---|---|---|
| main | 15 / 826 | 15 / 816 |
| branch | 15 / 829 | 12 / 823 |

Branch then made 12 queries / 12 frames in 30 seconds idle. Queries really run
without scrolling. The runtime guard failed: an unchanged idle badge was expected
to draw at most four frames. Why the 12 frames occurred is UNCONFIRMED; count-value
and message traces have not been captured. Do not claim this fixed frame or CPU
regressions. Periodic shell queries also add idle CPU that main does not incur.
Actual Trash change notifications are a possible next repair; notification
coverage and retry behavior need checking before choosing that implementation.

## Checks and pictures

Both before and after the timer repair, all five real-input suites passed:
view 11/11, input 20/20, actions 7/7, search 8/8, jobs 5/5 (51/51).
All six 50 ms message checks passed. Clipboard checks use the shared named mutex.
Every app ran on the private desktop or hidden --shot mode with --test-state.
No stock default handler, real terminal or real Open With chooser was launched.

MinSizeRel build passed, 448000 bytes below 819200, at
build/accept/MinSizeRel/dio-file-manager.exe. The normal build/MinSizeRel exe is OLD.
Console suites were not run in this worker; full landing checks belong to the
parent's merge lane.

Capture and popover scripts passed after the repair. I opened and inspected all
17 PNGs below. No additional slider visual fault was observed. Stops/search/photo
are 1440x900; popovers are 1080x720. This is worker inspection, not the independent
Claude visual review required from the parent.

- `E:\w\dio-file-manager\.agent-work\viewzoom-v2\final\light-details.png`
- `E:\w\dio-file-manager\.agent-work\viewzoom-v2\final\light-list.png`
- `E:\w\dio-file-manager\.agent-work\viewzoom-v2\final\light-small.png`
- `E:\w\dio-file-manager\.agent-work\viewzoom-v2\final\light-medium.png`
- `E:\w\dio-file-manager\.agent-work\viewzoom-v2\final\light-large.png`
- `E:\w\dio-file-manager\.agent-work\viewzoom-v2\final\light-extra-large.png`
- `E:\w\dio-file-manager\.agent-work\viewzoom-v2\final\dark-details.png`
- `E:\w\dio-file-manager\.agent-work\viewzoom-v2\final\dark-list.png`
- `E:\w\dio-file-manager\.agent-work\viewzoom-v2\final\dark-small.png`
- `E:\w\dio-file-manager\.agent-work\viewzoom-v2\final\dark-medium.png`
- `E:\w\dio-file-manager\.agent-work\viewzoom-v2\final\dark-large.png`
- `E:\w\dio-file-manager\.agent-work\viewzoom-v2\final\dark-extra-large.png`
- `E:\w\dio-file-manager\.agent-work\viewzoom-v2\final\light-popover.png`
- `E:\w\dio-file-manager\.agent-work\viewzoom-v2\final\dark-popover.png`
- `E:\w\dio-file-manager\.agent-work\viewzoom-v2\final\photo-extra-large.png`
- `E:\w\dio-file-manager\.agent-work\viewzoom-v2\final\dark-search-details.png`
- `E:\w\dio-file-manager\.agent-work\viewzoom-v2\final\dark-search-large.png`

## Continuation

All scripts, logs, JSON and diagnostic builds are retained under the task folder
E:/w/dio-file-manager/.agent-work/viewzoom-v2/ for the parent or successor.
Latest CPU: cpu-final.json / cpu-final.log. Counters: counts.json / counts.err.
Suites: suite-exits.json, view-results.json and each *_smoke folder.
The idle-frame assertion fails before the script's branch <= main scroll-frame
assertion. Raw counts already show seven extra branch scroll frames.
No pending user question. No test/worker remains after the bounded runs returned.
The previous validation file used cp1252 punctuation; it was converted to UTF-8.

## Continuous slider and animated reflow (motion-v1)

Base `main` `72c5a34`. MinSizeRel exe 460,288 bytes (budget 819,200). Pictures and strips are in the task folder
`.agent-work/motion-v1/final/` (`pos-<dark|light>-<2.2|3.5|4.6>.png`; `strip-glide-<dark|light>.png` 10 frames of a
slowed resize glide, `strip-cross25-<dark|light>.png` 8 frames of a slow drag across 2.5, `strip-drag-<dark|light>.png`
12 frames of a slider drag, all of the whole window). Every number below was measured on the hidden desktop while three other workers
kept the machine at 20 to 100% CPU; each line says which load it ran under.

### Use suites (branch, final build)

| Suite | Result | Note |
|---|---|---|
| motion_smoke | 11 / 11 passed (fade-timing build) | eleven cases, including the draw-log checks below; the click check passed 5 / 5 repeat runs; it failed 3 / 3 with the earlier logic |
| select_smoke | 24 / 24 passed (fade-timing build) | earlier note: one run at 37% load; two earlier runs at 83 to 96% load had one to two `No state dump` / `Window stopped answering` stalls, which were not repeatable (4-starred alternating with main, four rounds each, all passed) |
| input_smoke | 20 / 20 passed | one run at 19% load; a run at 79% load lost 9-context to a 5 s stall |
| search_smoke | 8 / 8 passed | |
| actions_smoke | 7 / 7 passed | |
| jobs_smoke | 4-remove-recent and 5-eject pass; 1 to 3 fail with the known race ("Progress popover did not show both jobs", or the same race seen earlier or later: "One job finished before both were observed", "Cancel did not remove just one job") | the same check fails the same way on unmodified `main`; with the other branch's fix (test copy delay cap 500 ms, check uses 300 ms) applied to a scratch copy of this branch it passes 5 / 5 |
| view_smoke | 14 / 14 passed (fade-timing build) | the next section records the 50 ms limit failing under load on earlier builds |

### The reflow glide as a crossfade

When columns change, an item that stays in its row glides to its new place and its width and height glide with it. An
item whose row changes fades out at the place where it was while it fades in at the place where it goes: the old copy leaves quickly (squared, gone at half of the 150 ms) and the new copy fades in over all of it (cubic ease-out), so duplicates show only in the first third and the two opacities add up to at least about 82%. The first crossfade ran both over the whole glide and looked busy in the middle frames. The
earlier design faded wrapping items out first (0 to 30%), moved the gliders, and faded arrivals in last (from 72%);
in a wide wrap most items change row, so the grid was nearly empty from 30% to 72% of the glide, a blink. Checks
(`motion_smoke`, read from the draw log): items at full opacity never overlap (at most 2 px; the worst seen was 0.2 px); no two visible names (above 5% opacity) come within 3 px of each other (`label_overprint`; without the name mask it failed with names printed 17 px over each other);
translucent ghosts and arrivals may overlap them; `never_empty` needs the drawn opacities of every frame to add up to
at least 75% of the items that should show (floor lowered from 90%): the earlier design failed it (56% at the emptiest frame), the crossfade
passes. A click lands on the most opaque item under the pointer, so a translucent arrival never takes a click from a
glider and a ghost is never a target. Strips (retaken: duplicates in 2 of 7 dark and 2 of 8 light glide frames, 0 of 8 in both cross-2.5 strips, 1 of 11 in both drag strips; a glider crossing a half-faded arrival no longer prints its name over the arrival's: the less opaque copy holds its name back, and no two names touched in any frame of the four glide and drag strips): the whole window is full in every frame of
`strip-glide-<dark|light>.png`; mid-glide the old and new places show through each other for a few frames and no cell
is blank. Retargeting mid-flight (second resize at 60 ms), scrolled reflow and a changing row height keep these
properties; the checks cover each.

### 50 ms message limit (view_smoke performance)

On the hidden desktop the default renderer is OpenGL emulated on the CPU and one frame takes about 45 ms, so the limit
has about 3 ms of room even on a quiet machine, and `main` fails it the same way. Alternating `main` and branch,
`USE_ONLY=6-performance`, loads 31 to 57%: main failed 3 and 4 of 6 modes (up to 213 ms), branch failed 6 and 4 of 6
(up to 130 ms). A single run at 26% load measured 47.4 to 48.7 ms in four modes. The slider drag message latency over
1,000 files with the same driver, alternating main and branch at 90 to 100% load (3 rounds each): main max 48.1 / 47.7 /
47.8 ms, branch 47.5 / 47.3 / 47.9 ms (run once at high priority, before the coordinator's no-priority rule).

### Latency and frame time with 1,000 files (`tests/use/motion_measure.py`)

Message latency is the time `SendMessage` takes to be answered while the motion runs; build time is the app's own
time to build one frame. Three runs per renderer at 22 to 33% load:

| Renderer | Slider drag: latency max / mean | Resize glides: latency max | Build ms max (drag, glides) | Glide length and frames |
|---|---|---|---|---|
| default (CPU-emulated OpenGL here) | 47.5 / 31 ms | 44.6 ms | 0.34, 0.30 | 129 to 149 ms, 4 to 5 frames |
| software | 13.3 / 1.7 ms | 13.0 ms | 0.26, 0.79 | 142 to 148 ms, 17 to 18 frames |

Every message is within 50 ms and every frame builds in under 1 ms (budget 16 ms). The glide has only 4 to 5 frames
under the emulated renderer because each frame costs about 30 ms there; on the software renderer, which a PC without
a GPU uses, it has 17 to 18.

### CPU (`cpu_run.py`: `cpu_budget.py` meter, 20,000 files, 7 columns, alternating main and branch)

| Pair | Scroll main (30 s) | Scroll branch (30 s) | Idle main (60 s) | Idle branch (60 s) |
|---|---:|---:|---:|---:|
| 1 | 2.546 | 5.460 | 0.000 | 0.026 |
| 2 | 5.621 | 3.539 | 0.026 | 0.026 |
| 3 | 4.267 | 5.672 | 0.078 | 0.104 |
| 4 | 5.464 | 3.486 | 0.026 | 0.026 |
| 5 | 3.643 | 3.434 | 0.052 | 0.052 |

Scroll means: main **4.308%**, branch **4.318%**, difference **0.010 points** (budget +0.5). Branch idle maximum
**0.104%** (budget 0.5% in all five windows). 601 or 602 wheel events per window; every window advanced the view.
Glide frames are requested with `cui_request_redraw` and stop when the glide ends, so idle stays idle.

### Findings fixed on the way

- The glides first asked for frames with a 1 ms wakeup, which the window loop rounds up to 3 timer ticks: 22 frames per
  second. They now ask for a redraw, which runs at the display rate (software renderer: 115 per second).
- The test probe picked the last built item even when it was below the window, so it never moved; it now picks the last
  item that is on screen.
- A slider drag saved the view file on every frame; under load the rename failed and the app showed "Could not save the
  view". The writer now waits until the value has been still for 300 ms (and saves at once on exit) and retries a busy file.

