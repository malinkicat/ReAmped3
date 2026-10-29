"""Compare rider_dump.bin captures at two frame rates (src/rider_probe.cpp).

For each 4-byte field of the rider (offset < 3072) and its actor (3072+), per
rider: total |change| per second of game time (sum of steps). Time-based state
changes as much per second at both rates; state advanced per frame changes
(rate_hi / rate_lo) times more per second at the higher rate.

usage: rider_dump.py low.bin high.bin fps_low fps_high
"""
import collections, math, struct, sys

REC = 12 + 3072 + 4096

def load(path):
    data = open(path, 'rb').read()
    riders = collections.defaultdict(list)
    for i in range(0, len(data) - REC + 1, REC):
        frame, rider, step = struct.unpack_from('<IIf', data, i)
        riders[rider].append((frame, step, data[i + 12:i + REC]))
    return riders

def rates(riders):
    out = {}
    for rider, frames in riders.items():
        frames.sort(key=lambda x: x[0])
        t = sum(s for _, s, _ in frames[1:])
        if t <= 0:
            continue
        for off in range(0, 3072 + 4096, 4):
            fsum = isum = 0.0
            changes = 0
            for (_, _, a), (_, _, b) in zip(frames, frames[1:]):
                va, vb = a[off:off + 4], b[off:off + 4]
                if va == vb:
                    continue
                changes += 1
                fa, fb = struct.unpack('>f', va)[0], struct.unpack('>f', vb)[0]
                if math.isfinite(fa) and math.isfinite(fb) and abs(fa) < 1e7 and abs(fb) < 1e7:
                    fsum += abs(fb - fa)
                ia, ib = struct.unpack('>i', va)[0], struct.unpack('>i', vb)[0]
                isum += abs(ib - ia) if abs(ib - ia) < 1000 else 0
            out[(rider, off)] = (fsum / t, isum / t, changes / max(1, len(frames) - 1))
    return out

lo, hi = load(sys.argv[1]), load(sys.argv[2])
flo, fhi = float(sys.argv[3]), float(sys.argv[4])
expect = fhi / flo
rlo, rhi = rates(lo), rates(hi)
# Riders matched by order of address.
lo_r, hi_r = sorted(lo), sorted(hi)
print('riders', [hex(r) for r in lo_r], [hex(r) for r in hi_r])
per_off = collections.defaultdict(list)
for a, b in zip(lo_r, hi_r):
    for off in range(0, 3072 + 4096, 4):
        x, y = rlo.get((a, off)), rhi.get((b, off))
        if not x or not y:
            continue
        per_off[off].append((x, y))
rows = []
for off, pairs in per_off.items():
    # Float interpretation unless it looks like an integer counter.
    fl = [(x[0], y[0]) for x, y in pairs if x[0] > 1e-4 and y[0] > 1e-4]
    it = [(x[1], y[1]) for x, y in pairs if x[1] > 0.5 and y[1] > 0.5 and x[0] < 1e-4]
    chg = sum(y[2] for _, y in pairs) / len(pairs)
    for kind, vals in (('f', fl), ('i', it)):
        if len(vals) >= 2:
            ratios = sorted(y / x for x, y in vals)
            med = ratios[len(ratios) // 2]
            rows.append((off, kind, med, len(vals), chg, sum(x for x, _ in vals) / len(vals)))
print(f'expected ratio for per-frame state: {expect:.1f}')
print('per-frame suspects (ratio > 2):')
for off, kind, med, n, chg, rate in sorted(rows, key=lambda r: -r[2]):
    if med > 2:
        where = f'rider+{off}' if off < 3072 else f'actor+{off - 3072}'
        print(f'  {where:14s} {kind} ratio {med:5.2f}  riders {n}  changes/frame {chg:.2f}  rate@lo {rate:.3g}/s')
print('time-based (ratio 0.7-1.4):', sum(1 for r in rows if 0.7 < r[2] < 1.4), 'fields')
