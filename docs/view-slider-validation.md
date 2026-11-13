# View slider acceptance — NOT READY TO LAND

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
