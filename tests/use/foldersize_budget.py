"""Fails (exit 1) when a measure.py result is over a budget of docs/design.md. Usage: python budget_check.py measure-v2w8.json"""
import json, sys
d = json.load(open(sys.argv[1]))['w-folder']
fails = []
def need(ok, what):
    if not ok:
        fails.append(what)
need(d['seconds_to_all_in'] <= 30, 'E:\\w all in within 30 s: %s' % d['seconds_to_all_in'])
need(d['idle_cpu_pct_of_one_core_over_30s'] < 0.1, 'idle CPU below 0.1 %%: %s' % d['idle_cpu_pct_of_one_core_over_30s'])
need(d['writer_20_per_s_cpu_pct_of_one_core_over_30s'] < 0.1, 'CPU below 0.1 %% with a writer: %s' % d['writer_20_per_s_cpu_pct_of_one_core_over_30s'])
need(d['working_set_mb'] <= 175 and d['peak_working_set_mb'] <= 175, 'working set at most 175 MB: %s' % d['peak_working_set_mb'])
need(d['max_message_ms_during_walk'] < 50, 'window message delay below 50 ms: %s' % d['max_message_ms_during_walk'])
need(not d['ground_truth_mismatch'], 'child sizes correct: %s' % d['ground_truth_mismatch'])
print('FAIL ' + '; '.join(fails) if fails else 'budgets met')
sys.exit(1 if fails else 0)
