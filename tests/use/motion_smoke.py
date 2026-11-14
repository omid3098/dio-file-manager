"""Continuous view size and animated reflow, driven by real Win32 input on the shared private-desktop driver.

The app (in --test-state mode) writes one line per drawn frame to <base>.frames:
time frame zoom-value icon-px columns gliding probe-index probe-x probe-y build-ms
The probe is the last built item; x and y are document pixels where it is drawn.
"""
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
# On the hidden desktop the default OpenGL path is emulated on the CPU and delivers a frame every 33 to 45 ms; the
# software renderer (what a PC without a GPU uses) delivers about 115 per second, so the frame counts below mean something.
os.environ.setdefault('CUI_RENDERER', 'software')
os.environ.setdefault('DFM_USE_WORK', str(ROOT / '.agent-work/use-checks' / Path(__file__).stem))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import input_smoke as base

WORK = base.WORK
U = base.U


def frames(a):
    rows = []
    path = Path(str(a.base) + '.frames')
    for line in path.read_text(encoding='utf-8').splitlines():
        p = line.split()
        if len(p) == 10 and p[0] != 'D':
            rows.append(dict(t=float(p[0]), n=int(p[1]), zv=float(p[2]), icon=float(p[3]), cols=int(p[4]), glide=int(p[5]),
                             idx=int(p[6]), x=float(p[7]), y=float(p[8]), ms=float(p[9])))
    return rows


def mark(a):
    return len(frames(a))


def draws(a):
    """The draw log of a glide: one entry per drawn frame, as `D frame view-height scroll items...`. Each item is
    m<idx>:x:y:alpha:w:h:lx:ly:lw:lh:la (an item at its gliding place) or g<idx>:... (a ghost fading out); y is relative to the view.
    lx..lh is the box of the item's name and la the opacity the name is drawn with (the item's own, less what a more opaque
    copy holds back); d['lab'] lists (item, lx, ly, lw, lh, la) for every drawn copy."""
    out = []
    path = Path(str(a.base) + '.frames')
    for line in path.read_text(encoding='utf-8').splitlines():
        p = line.split()
        if not p or p[0] != 'D':
            continue
        d = dict(n=int(p[1]), vh=float(p[2]), sy=float(p[3]), m={}, g=[], lab=[])
        for tok in p[6:]:
            kind, rest = tok[0], tok[1:].split(':')
            idx, x, y, al, w, h = int(rest[0]), *map(float, rest[1:6])
            if len(rest) >= 11:
                d['lab'].append((idx, *map(float, rest[6:11])))
            if kind == 'm':
                d['m'][idx] = (x, y, al, w, h)
            else:
                d['g'].append((idx, x, y, al, w, h))
        out.append(d)
    return out


def overlap_worst(ds):
    """Largest overlap, in px, of two drawn items that are both at full opacity (gliders and settled items), over all
    draws; with where. A fading ghost and a fading arrival share places with others while they are translucent: that
    is the crossfade, not a defect, so only items at 99.9% opacity or more count (an arrival that is 99% there still has up to 1% of a glider's way left to cover)."""
    worst = (0.0, None)
    for d in ds:
        items = [(i, x, y, w, h) for i, (x, y, al, w, h) in d['m'].items() if al >= .999]
        items += [(i, x, y, w, h) for i, x, y, al, w, h in d['g'] if al >= .999]
        for k, (i, x, y, w, h) in enumerate(items):
            for j, x2, y2, w2, h2 in items[k + 1:]:
                ox = min(x + w, x2 + w2) - max(x, x2)
                oy = min(y + h, y2 + h2) - max(y, y2)
                if ox > 0 and oy > 0 and min(ox, oy) > worst[0]:
                    worst = (min(ox, oy), (d['n'], i, j))
    return worst


def never_empty(ds, s):
    """The grid never blinks: in every drawn frame of a glide, the opacities of everything drawn (items and ghosts) add
    up to at least 75% of the number of items whose final place is in view. A wrapping item's old copy leaves in the first
    half of the glide while its new copy fades in over all of it, so the two add up to about 82% at the thinnest moment
    (the floor was 90% while both copies shared the whole glide, 75% leaves room for frame timing). Returns the lowest sum seen, as a share."""
    cols, h, count = s['cols'], s['cell_h'], s['count']
    d = ds[-1]
    want = sum(1 for idx in range(count) if (idx // cols) * h - d['sy'] + h > 0 and (idx // cols) * h - d['sy'] < d['vh'])
    assert want > 0, 'No item should be visible'
    low = (9.0, None)
    for f in ds:
        total = sum(v[2] for v in f['m'].values()) + sum(g[3] for g in f['g'])
        if total / want < low[0]:
            low = (total / want, f['n'])
    assert low[0] >= .75, f'The grid empties during the glide: at frame {low[1]} the drawn items add up to {low[0] * 100:.0f}% of the {want} that should show'
    return low[0]


def blank_and_ghosts(ds, s, fresh=True):
    """The last drawn frame of a glide: every item whose final place is in view is drawn there (a blank spot otherwise),
    nothing outside the view is drawn on screen as a stray, and no ghost is left. A last record that is not fresh (see
    wait_ended) is an early sample: it is not judged, and False is returned."""
    if not fresh:
        return False
    d = ds[-1]
    cols, h, count = s['cols'], s['cell_h'], s['count']
    cw = (s['content_w'] - 32) / cols
    missing = []
    for idx in range(count):
        top = (idx // cols) * h - d['sy']
        if top + h <= 0 or top >= d['vh']:
            continue
        got = d['m'].get(idx)
        if not got or got[2] < .5 or abs(got[0] - (16 + (idx % cols) * cw)) > cw * .35 or abs(got[1] - top) > h * .35:
            missing.append(idx)
    assert not missing, f'Blank spots at the end of the glide, items {missing[:8]}'
    ghosts = [g for g in d['g'] if g[3] > .02]
    assert not ghosts, f'Ghosts left at the end of the glide: {ghosts[:3]}'
    return True


def label_overprint(ds):
    """No two names touch or print over each other in any drawn frame: of two drawn copies whose name boxes are less than
    3 px apart, at most one is drawn with more than 5% opacity (the less opaque copy holds its name back, the glider's
    stays whole). Returns the smallest gap, in px, between two names that were both visible."""
    low = (99.0, None)
    for d in ds:
        vis = [t for t in d['lab'] if t[5] > .05 and t[3] > 0]   # an empty cell past the last item has no name
        for k, (i, x, y, w, h, la) in enumerate(vis):
            for j, x2, y2, w2, h2, la2 in vis[k + 1:]:
                gap = max(max(x2 - (x + w), x - (x2 + w2)), max(y2 - (y + h), y - (y2 + h2)))
                if gap < low[0]:
                    low = (gap, (d['n'], i, j, round(la, 2), round(la2, 2)))
    assert low[0] >= 3, f'Two names are only {low[0]:.1f} px apart, or print over each other (frame, items, name opacities {low[1]})'
    assert any(d['lab'] for d in ds), 'The draw log has no name boxes: the check would see nothing'
    return round(low[0], 1)


def wait_ended(a, limit=3.0):
    """Wait, bounded, for the frame after the last glide frame: the glide has then ended and drew nothing more. Returns
    whether the last draw record is fresh, i.e. at most 40 ms before that frame. Under load a draw gap can make the last
    record an early sample (a 196 ms gap was seen) that says nothing about the end: then it is not judged, and the caller
    says so. A glide that never ends (a ghost that stays) keeps drawing records and times out here."""
    deadline = time.monotonic() + limit
    while True:
        ds, rows = draws(a), frames(a)
        last = ds[-1]['n'] if ds else None
        after = [r for r in rows if last is not None and r['n'] > last]
        end = next((r for r in after if not r['glide']), None)
        if end:
            t_last = next((r['t'] for r in rows if r['n'] == last), None)
            return t_last is not None and end['t'] - t_last <= .04
        assert time.monotonic() < deadline, f'The glide did not end within {limit:.0f} s: the draw log still had records at frame {last}'
        time.sleep(.02)


def crossfade_timing(ds, a, m):
    """How the crossfade of one 150 ms glide looks in time: an item's old copy (ghost) is gone by the middle of the glide,
    and a ghost and the same item's new copy are both visible (15% or more) only in the first third. Frame times come
    from the frames log. Returns (frames where both copies show, last progress at which a ghost still showed)."""
    t = {r['n']: r['t'] for r in frames(a)[m:]}
    ds = [d for d in ds if d['n'] in t]
    assert ds, 'No draw frame has a time'
    t0 = t[ds[0]['n']]
    both = last_ghost = 0.0
    dup_frames = 0
    for d in ds:
        p = (t[d['n']] - t0) / .15
        for i, x, y, al, w, h in d['g']:
            if al > .02:
                last_ghost = max(last_ghost, p)
                assert p <= .52, f'A ghost (item {i}) is still drawn at {al * 100:.0f}% at {p * 100:.0f}% of the glide, frame {d["n"]}'
            if al >= .15 and i in d['m'] and d['m'][i][2] >= .15:
                dup_frames += 1
                both = max(both, p)
                assert p <= .36, f'Item {i} shows twice at {p * 100:.0f}% of the glide (old copy {al * 100:.0f}%, new {d["m"][i][2] * 100:.0f}%), frame {d["n"]}'
    return dup_frames, round(last_ghost, 2)


def ghost_start_bound(glide_px, dt, glide_s=.15):
    """The farthest a new ghost can start from where its item was drawn one frame earlier. A new ghost starts where its item
    would be drawn now under the glide it was in, so the step is one frame of that glider's travel: a cubic ease-out
    moves at most 3 x its distance per glide time, so dt seconds cost at most 3 * distance * dt / glide time (plus 1.5 px
    of rounding). Long frames allow more, a short glide allows less; a ghost that jumps beyond it is a bug."""
    return min(glide_px, 3 * glide_px * dt / glide_s) + 1.5


def no_jumps(ds, t=None):
    """No item drawn in the view moves more than its glide distance between two drawn frames. A ghost stays where it is on the screen,
    so it must start at the place where its item was drawn the frame before (a ghost the grid carries into a second
    change moves with the scroll, not with the document) and then not move; a glider moves at most its whole glide distance (the farthest it was drawn from the place where it ended up). Returns the largest step of any glider and of any ghost, in px."""
    t = t or {}
    last = {}
    for d in ds:
        for i, (x, y, al, w, h) in d['m'].items():
            last[i] = (x, y)
    far = {}   # how far each item was drawn from the place where it ended, at most: its glide distance
    for d in ds:
        for i, (x, y, al, w, h) in d['m'].items():
            far[i] = max(far.get(i, 0.0), abs(x - last[i][0]), abs(y - last[i][1]))
    worst_g = worst_h = 0.0

    def shown(d, y, h):
        return y + h > 0 and y < d['vh']   # at least partly in the view

    for a_, b in zip(ds, ds[1:]):
        for i, (x, y, al, w, h) in b['m'].items():
            if i in a_['m'] and al >= .95 and a_['m'][i][2] >= .95 and (shown(b, y, h) or shown(a_, a_['m'][i][1], h)):   # a fading arrival sits at its place; a second change moves that place
                step = max(abs(x - a_['m'][i][0]), abs(y - a_['m'][i][1]))
                worst_g = max(worst_g, step)
                assert step <= far[i] + 1.5, f'Item {i} jumped {step:.0f} px between frames {a_["n"]} and {b["n"]}, but its whole glide is {far[i]:.0f} px'
        for i, x, y, al, w, h in b['g']:
            if not shown(b, y, h) and not any(shown(a_, g[2], g[5]) for g in a_['g'] if g[0] == i):
                continue
            carried = [(px, py) for j, px, py, pal, pw, ph in a_['g'] if j == i]
            drawn = [a_['m'][i][:2]] if i in a_['m'] else []
            if not carried and not drawn:
                continue   # the first frame of a glide: nothing before it
            # A ghost is either one the grid carries into a second change (it stays where it is on the screen, so it must not
            # move at all), or a new one that starts where its item was drawn (one frame of glider travel at most).
            step_c = min((max(abs(x - px), abs(y - py)) for px, py in carried), default=1e9)
            step_m = min((max(abs(x - px), abs(y - py)) for px, py in drawn), default=1e9)
            if step_c <= 1.5:
                worst_h = max(worst_h, step_c)
            else:
                dt = t[b['n']] - t[a_['n']] if b['n'] in t and a_['n'] in t else 1 / 60
                bound = ghost_start_bound(far.get(i, 0.0), dt)
                assert step_m <= bound, f'A ghost of item {i} jumped between frames {a_["n"]} and {b["n"]}: now at ({x:.0f}, {y:.0f}), {min(step_c, step_m):.0f} px from where it was, more than {bound:.0f} px (its glide is {far.get(i, 0):.0f} px, {dt * 1000:.0f} ms between the frames)'
    return round(worst_g, 1), round(worst_h, 1)


def times(a):
    return {r['n']: r['t'] for r in frames(a)}


def slider(a):
    a.click(*a.header_icon(0))
    s = a.snap()
    x, y, w, h = s['slider']
    assert w > 100, f'No slider track {s["slider"]}'
    return x, y, w, h


def thumb_x(box, v):
    x, y, w, h = box
    return x + 9 + (w - 18) * v / 5


def drag(a, box, v0, v1, steps, pause=.02):
    """Press on the thumb at v0, drag in small steps to v1 and release."""
    y = box[1] + box[3] / 2
    x0, x1 = thumb_x(box, v0), thumb_x(box, v1)
    a.msg(0x200, 0, a.point(x0, y)); time.sleep(.04)
    a.msg(0x201, 1, a.point(x0, y)); time.sleep(.05)
    for n in range(1, steps + 1):
        a.msg(0x200, 1, a.point(x0 + (x1 - x0) * n / steps, y))
        time.sleep(pause)
    a.msg(0x202, 0, a.point(x1, y))


def set_value(a, v):
    """Move the thumb to v (from wherever it is) and close the popover."""
    box = slider(a)
    cur = a.snap()['zv']
    drag(a, box, cur, v, 8, .03)
    time.sleep(.35)
    a.key(27)
    time.sleep(.2)


def resize(a, width, height=720):
    dpi = U.GetDpiForWindow(a.hwnd) / 96
    assert U.SetWindowPos(a.hwnd, None, 0, 0, int(width * dpi), int(height * dpi), 0x16)


def grid_xy(i, s):
    cw = (s['content_w'] - 32) / s['cols']
    return 16 + (i % s['cols']) * cw, (i // s['cols']) * s['cell_h']


def inside():
    results = []
    FINAL = WORK / 'final'

    def check(name, fn, env=None, folder=None):
        if os.environ.get('USE_ONLY') and not name.startswith(os.environ['USE_ONLY']):
            return
        saved = {k: os.environ.get(k) for k in (env or {})}
        os.environ.update(env or {})
        a = None
        # Every case starts from the default size: the saved view of an earlier case must not leak in.
        (WORK / 'appdata' / 'dio-file-manager' / 'view.txt').unlink(missing_ok=True)
        try:
            a = base.App(name, folder or WORK / 'views', 'grid', 'light')
            fn(a)
            results.append(dict(check=name, result='passed'))
        except Exception as e:
            if a and a.proc.poll() is None:
                a.snap(name + '-failed')
            results.append(dict(check=name, result='FAILED', reason=str(e)))
        finally:
            if a:
                a.close()
            for k, v in saved.items():
                if v is None:
                    os.environ.pop(k, None)
                else:
                    os.environ[k] = v
        print(json.dumps(results[-1]), flush=True)

    def slider_drag(a):
        a.snap()
        box = slider(a)
        start = a.snap()['zv']
        assert abs(start - 3) < .01, start
        m = mark(a)
        drag(a, box, 3, 4.37, 40)
        time.sleep(.5)
        s = a.snap()
        assert 4.30 < s['zv'] < 4.45, f'Thumb released at 4.37, value {s["zv"]}'
        assert abs(s['zv'] - round(s['zv'])) > .15, f'Value snapped to a stop: {s["zv"]}'
        rows = frames(a)[m:]
        sizes = sorted({r['icon'] for r in rows})
        assert len(sizes) >= 12, f'Only {len(sizes)} icon sizes during the drag: {sizes}'
        assert sizes[0] >= 62 and sizes[-1] <= 144, sizes
        steps = [b - a_ for a_, b in zip(sizes, sizes[1:])]
        assert max(steps) < 12, f'Icon size jumped by {max(steps):.1f} px'
        worst = max(r['ms'] for r in rows)
        gaps = [b['t'] - a_['t'] for a_, b in zip(rows, rows[1:]) if b['t'] - a_['t'] < .2]
        print(json.dumps(dict(drag_frames=len(rows), max_build_ms=round(worst, 3), max_frame_gap_ms=round(max(gaps) * 1000, 2))), flush=True)
        assert worst <= 16, f'A frame took {worst:.1f} ms'
        time.sleep(.4)
        assert abs(a.snap()['zv'] - s['zv']) < .001, 'Value moved after release'
        # The drag changed the value in about 40 steps; it is written once it settles, with no write failure.
        time.sleep(.6)
        saved = (WORK / 'appdata' / 'dio-file-manager' / 'view.txt').read_text().split()
        assert saved[0] == 'view-v2' and abs(float(saved[1]) - s['zv']) < .011, f'Saved view {saved} for value {s["zv"]}'
        a.key(27)
        a.view = 'saved'
        a.restart()
        z = a.snap()['zv']
        assert abs(z - s['zv']) < .02, f'Restart lost the value: {z} vs {s["zv"]}'

    def midpoints(a):
        box = slider(a)
        drag(a, box, 3, 1.25, 12); time.sleep(.3)
        s = a.snap(); assert s['zoom'] == 1 and s['view'] == 0 and s['column_rows'] >= 10 and abs(s['icon_px'] - 16) < .1, f'List at 1.25: {s["zoom"]}'
        drag(a, box, 1.25, 0.3, 8); time.sleep(.3)
        s = a.snap(); assert s['zoom'] == 0 and s['view'] == 1, f'Details at 0.3: {s["zoom"]}'
        drag(a, box, 0.3, 1.4, 8); time.sleep(.3)
        assert a.snap()['zoom'] == 1, 'List at 1.4'
        m = mark(a)
        drag(a, box, 1.4, 1.65, 6); time.sleep(.3)
        s = a.snap(); assert s['zoom'] == 2 and s['view'] == 0, f'Small icons past 1.5: {s["zoom"]}'
        assert abs(s['icon_px'] - 32) < .1, 'Small stays 32 px below Small'
        drag(a, box, 1.65, 4.4, 20); time.sleep(.3)
        assert a.snap()['zoom'] == 4

    def wheel_glide(a):
        a.snap()
        for direction, target in ((120, 4.0), (-120, 3.0)):
            m = mark(a)
            a.keys((17,))
            a.msg(0x200, 0, a.point(700, 400))
            a.msg(0x20A, ((direction & 0xffff) << 16) | 8)
            a.keys(())
            time.sleep(.5)
            rows = frames(a)[m:]
            vals = [r['zv'] for r in rows]
            mid = [v for v in vals if 3.01 < v < 3.99]
            assert len(mid) >= 3, f'Ctrl+wheel did not glide: {vals}'
            seq = vals if direction > 0 else vals[::-1]
            assert all(b >= a_ - 1e-4 for a_, b in zip(seq, seq[1:])), f'Not monotonic: {vals}'
            moving = [r for r in rows if 3.0005 < r['zv'] < 3.9995]
            span = moving[-1]['t'] - moving[0]['t']
            assert abs(vals[-1] - target) < .001, f'Ended at {vals[-1]}'
            assert .05 <= span <= .3, f'Glide lasted {span * 1000:.0f} ms'
        # One notch moves one stop; two quick notches add up; Ctrl+0 glides back to Medium.
        a.key(187, (17, 16)); time.sleep(.5); assert abs(a.snap()['zv'] - 4) < .001
        a.key(48, (17,)); time.sleep(.5); assert abs(a.snap()['zv'] - 3) < .001, 'Ctrl 0'

    def idle_check(a, what):
        """Idle stays idle: once the glide has ended, not one frame is drawn until something happens.
        The test's own state request (a.snap) asks the window for frames for about 0.3 s after it (it invalidates the
        window and posts messages until the dump is written; frames at 40 ms spacing right after the last snap), so the
        count starts after that tail; a redraw the app keeps asking for would show in the 1.2 s that follow."""
        time.sleep(.4)
        n1 = len(frames(a)); time.sleep(1.2); rows = frames(a)
        late = rows[n1:]
        assert not late, f'{len(late)} frames drawn while idle after {what}: frames {[r["n"] for r in late]} at {[round(r["t"], 3) for r in late]}'

    def glide_checks(ds, s1, label, fresh, wide=False):
        """What the eye would see: items glide, none overlap, no names print over each other, no blank spot or ghost remains."""
        assert ds, f'{label}: no draw log, the glide drew nothing'
        gliders = {}
        for d in ds:
            for i, (x, y, al, w, h) in d['m'].items():
                if al > .99:
                    gliders.setdefault(i, set()).add((round(x, 1), round(y, 1)))
        moving = {i: p for i, p in gliders.items() if len(p) >= 3}
        assert moving, f'{label}: no item glided through 3 places'
        worst, where = overlap_worst(ds)
        assert worst <= 2, f'{label}: items overlap by {worst:.1f} px (frame, items {where})'
        label_overprint(ds)
        blank_and_ghosts(ds, s1, fresh)
        if wide:
            never_empty(ds, s1)
        return len(moving), worst

    def reflow(a, animated=True, double=False):
        s0 = a.wait_ready()
        assert s0['count'] == 200 and abs(s0['zv'] - 3) < .01
        m = mark(a)
        d0 = len(draws(a))
        resize(a, 900)
        if double:
            time.sleep(.06)   # a second change while the first glide runs
            resize(a, 760)
        time.sleep(.6)
        s1 = a.snap()
        assert s1['cols'] < s0['cols'], f'Columns did not change: {s0["cols"]} -> {s1["cols"]}'
        rows = frames(a)[m:]
        gl = [r for r in rows if r['glide']]
        i = s1['probe']
        end_x, end_y = grid_xy(i, s1)
        last = rows[-1]
        assert last['idx'] == i and abs(last['x'] - end_x) < .6 and abs(last['y'] - end_y) < .6, \
            f'Ends at {last["x"], last["y"]}, grid says {end_x, end_y}'
        if not animated:
            assert not gl, 'Animations are off, but items glided'
            assert len(draws(a)) == d0, 'Animations are off, but the glide overlay drew'
            return
        fresh = wait_ended(a)
        ds = draws(a)[d0:]
        movers, worst = glide_checks(ds, s1, 'reflow', fresh, wide=True)
        span = gl[-1]['t'] - gl[0]['t']
        assert .06 <= span <= (.45 if double else .3), f'Glide lasted {span * 1000:.0f} ms'
        assert not any(r['glide'] for r in rows[-3:]), 'Still gliding at the end'
        ghosted = len({g[0] for d in ds for g in d['g']})
        assert ghosted >= 1, 'Wrapping items did not fade out as ghosts'
        jump_g, jump_h = no_jumps(ds, times(a))
        dups, last_ghost = (None, None) if double else crossfade_timing(ds, a, m)
        worst_ms = max(r['ms'] for r in gl)
        print(json.dumps(dict(glide_frames=len(gl), gliders=movers, ghosted_items=ghosted, worst_overlap_px=round(worst, 2), duplicate_item_frames=dups, last_ghost_progress=last_ghost, max_step_px=jump_g, glide_ms=round(span * 1000), max_build_ms=round(worst_ms, 3), final_sample='fresh' if fresh else 'early (not judged)')), flush=True)
        assert worst_ms <= 16, f'A glide frame took {worst_ms:.1f} ms'
        idle_check(a, 'the glide')

    def scroll_down(a):
        a.wait_ready()
        for _ in range(70):
            a.msg(0x200, 0, a.point(700, 400))
            a.msg(0x20A, (-120 & 0xffff) << 16)
            time.sleep(.02)
        time.sleep(.5)
        s0 = a.snap()
        assert s0['first'] >= 12, f'Did not scroll: first row {s0["first"]}'
        return s0

    def retarget_scrolled(a):
        """Scrolled down, 1080 to 900, and 60 ms later to 760: the ghosts of the first glide are carried into the second
        while the scroll anchor moves. No drawn item may jump between two frames."""
        s0 = scroll_down(a)
        d0 = len(draws(a))
        resize(a, 900)
        time.sleep(.06)
        resize(a, 760)
        time.sleep(.7)
        s1 = a.snap()
        assert s1['cols'] < s0['cols'], f'Columns did not change: {s0["cols"]} -> {s1["cols"]}'
        fresh = wait_ended(a)
        ds = draws(a)[d0:]
        assert ds, 'No glide'
        carried = sum(1 for a_, b in zip(ds, ds[1:]) for g in b['g'] if any(h[0] == g[0] for h in a_['g']))
        assert carried >= 3, f'Only {carried} ghosts were carried into the second glide: the check would not see a jump'
        jump_g, jump_h = no_jumps(ds, times(a))
        worst, where = overlap_worst(ds)
        assert worst <= 2, f'Items overlap by {worst:.1f} px (frame, items {where})'
        label_overprint(ds)
        blank_and_ghosts(ds, s1, fresh)
        print(json.dumps(dict(carried_ghost_frames=carried, max_glider_step_px=jump_g, max_ghost_step_px=jump_h)), flush=True)
        idle_check(a, 'the retarget')

    def reflow_scrolled(a):
        """Scrolled down, so items leave and enter the visible area while the columns change, both ways."""
        s0 = scroll_down(a)
        changes = judged = 0
        for width in (900, 760, 1080, 600, 1080):
            d0 = len(draws(a))
            resize(a, width)
            time.sleep(.7)
            s1 = a.snap()
            fresh = wait_ended(a)
            ds = draws(a)[d0:]
            if s1['cols'] != s0['cols']:
                changes += 1
                assert ds, f'{width}: columns {s0["cols"]} -> {s1["cols"]} with no glide'
                worst, where = overlap_worst(ds)
                assert worst <= 2, f'{width}: items overlap by {worst:.1f} px (frame, items {where})'
                label_overprint(ds)
                judged += blank_and_ghosts(ds, s1, fresh)
            s0 = s1
        assert changes >= 3, f'Only {changes} column changes'
        assert judged >= 2, f'Only {judged} of {changes} column changes had a final draw record to judge (a draw gap under load made the others early samples)'
        idle_check(a, 'the last glide')

    def click_during_glide(a):
        """A click and a right click land on the item drawn there, not on the item whose final place is there. Each one
        gets its own slowed glide (2.5 s). The target is an item drawn 0.5 to 1.5 cells away from its own final place
        (so the click point is outside that place, where another item ends up) in the first 1.4 s of the glide, when the
        ease-out is still far from the end; its place is read from the newest draw frame right before the click, never
        from an earlier state dump, which has aged by the time it is written."""
        s0 = a.wait_ready()
        names = s0['names']

        def one_glide(width, right):
            m = mark(a)
            d0 = len(draws(a))
            resize(a, width)
            s = a.snap()   # the geometry is final at once; only the items move
            cw = (s['content_w'] - 32) / s['cols']
            org_x, org_y = s['row'][0], s['row'][1] - s['first'] * s['cell_h']
            deadline = time.monotonic() + 2.5
            while time.monotonic() < deadline:
                rows = frames(a)[m:]
                gl = [r for r in rows if r['glide']]
                ds = draws(a)[d0:]
                if gl and ds and .2 <= rows[-1]['t'] - gl[0]['t'] <= 1.4 and ds[-1]['n'] >= rows[-1]['n'] - 2:
                    d = ds[-1]
                    best = None
                    for i, (x, y, al, w, h) in d['m'].items():
                        gap = abs(x - (16 + (i % s['cols']) * cw))
                        if i < len(names) and al > .95 and cw * .5 < gap < cw * 1.5 and 4 < y and y + h < d['vh'] - 4:
                            if best is None or abs(gap - cw) < best[0]:
                                best = (abs(gap - cw), i, x, y, w, h)
                    if best:
                        _, i, x, y, w, h = best
                        px, py = org_x + x + w / 2, org_y + d['sy'] + y + h / 2
                        (a.rclick if right else a.click)(px, py)
                        sel = a.snap()
                        what = 'Right click' if right else 'Click'
                        assert sel['selection'] == [names[i]], f'{what} on item {i} drawn at ({x:.0f}, {y:.0f}) selected {sel["selection"]}'
                        if right:
                            assert sel['ctx'], f'Right click on item {i} opened no menu'
                            a.key(27)
                        else:
                            assert sel['view'] == 0 and sel['path'] == s0['path'], 'The click opened something'
                        return i
                time.sleep(.01)
            raise AssertionError('No item drawn 0.5 to 1.5 cells from its final place in the first 1.4 s of the glide')

        first = one_glide(880, False)
        time.sleep(2.7)   # the glide ends
        second = one_glide(1080, True)
        print(json.dumps(dict(clicked_item=first, right_clicked_item=second)), flush=True)

    def reflow_slider(a):
        """Columns change because the size changes: still one glide per change, ending on the exact grid."""
        box = slider(a)
        m = mark(a); d0 = len(draws(a))
        drag(a, box, 3, 4.6, 30, .03)
        time.sleep(.6)
        s = a.snap()
        rows = frames(a)[m:]
        assert len({r['cols'] for r in rows}) >= 2, 'Columns never changed while dragging'
        last = rows[-1]
        ex, ey = grid_xy(s['probe'], s)
        assert last['idx'] == s['probe'] and abs(last['x'] - ex) < .6 and abs(last['y'] - ey) < .6, 'Did not end on the grid'
        fresh = wait_ended(a)
        ds = draws(a)[d0:]
        worst, where = overlap_worst(ds)
        assert worst <= 2, f'Items overlap by {worst:.1f} px while the slider moved (frame, items {where})'
        label_overprint(ds)
        blank_and_ghosts(ds, s, fresh)
        idle_check(a, 'the drag')

    def sidebar_toggle(a):
        s0 = a.wait_ready()
        m = mark(a)
        resize(a, 600)   # below 640 px the sidebar leaves and the grid widens
        time.sleep(.6)
        resize(a, 1080)
        time.sleep(.6)
        rows = frames(a)[m:]
        assert any(r['glide'] for r in rows), 'No glide when the sidebar came and went'
        assert a.snap()['cols'] == s0['cols']

    check('1-slider-drag', slider_drag)
    check('2-midpoints', midpoints)
    check('3-wheel-glide', wheel_glide)
    check('4-reflow-glide', reflow)
    check('5-reflow-retarget', lambda a: reflow(a, double=True))
    check('6-reflow-animations-off', lambda a: reflow(a, animated=False), env={'DFM_TEST_ANIMATIONS': 'off'})
    check('7-reflow-by-slider', reflow_slider)
    check('8-sidebar-width', sidebar_toggle)
    check('9-click-during-glide', click_during_glide, env={'DFM_TEST_GLIDE_MS': '2500'})
    check('10-reflow-scrolled', reflow_scrolled)
    check('11-retarget-scrolled', retarget_scrolled)
    (WORK / 'motion-results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    return int(any(r['result'] != 'passed' for r in results))


def main():
    for d in ('tmp', 'appdata', 'final', 'views'):
        (WORK / d).mkdir(parents=True, exist_ok=True)
    os.environ.update(TEMP=str(WORK / 'tmp'), TMP=str(WORK / 'tmp'))
    if '--inside' in sys.argv:
        return inside()
    gaming = r'C:\Users\Omid\.agents\tools\gaming.py'
    rc = subprocess.run([sys.executable, gaming, 'check'], creationflags=subprocess.CREATE_NO_WINDOW).returncode
    if rc == 3:
        subprocess.run([sys.executable, gaming, 'wait', '--cap', '60'], check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    elif rc:
        return rc
    for i in range(200):
        (WORK / 'views' / f'file-{i:05}.txt').touch(exist_ok=True)
    sys.path.insert(0, r'C:\Users\Omid\.agents\source\correct')
    from hidden import popen
    with (WORK / 'motion.log').open('wb') as out, (WORK / 'motion.err').open('wb') as err:
        run = popen([sys.executable, str(Path(__file__).resolve()), '--inside'], ROOT, dict(os.environ), b'', out, err)
        try:
            return run.wait(timeout=300)
        finally:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())
