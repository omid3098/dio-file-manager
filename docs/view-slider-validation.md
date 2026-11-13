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
