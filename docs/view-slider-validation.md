# View slider validation — incomplete

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
