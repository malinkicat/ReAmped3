"""Which decoded XMA streams make up the game's mix (build/audio_levels.csv from
--audio_levels): per 0.5 s window, mix power ~= sum(gain_i * stream_i power),
solved with non-negative least squares.  usage: audio_levels.py [seconds=30]"""
import collections, csv, sys
import numpy as np

secs = float(sys.argv[1]) if len(sys.argv) > 1 else 30.0
rows = [(float(r[0]), r[1], float(r[2])) for r in csv.reader(open('build/audio_levels.csv')) if len(r) == 3]
end = rows[-1][0]; start = end - secs; W = 0.5; nwin = int(secs / W)
pw = collections.defaultdict(lambda: collections.defaultdict(list))
for t, s, r in rows:
    if t >= start:
        pw[s][min(nwin - 1, int((t - start) / W))].append(r * r)
series = lambda s: np.array([np.mean(pw[s][k]) if pw[s][k] else 0.0 for k in range(nwin)])
srcs = [s for s in pw if s != 'mix' and len(pw[s]) > nwin * 0.3]
mix = series('mix'); X = np.stack([series(s) for s in srcs], 1)
active = list(range(len(srcs)))
while True:  # NNLS: refit without negative coefficients
    g = np.zeros(len(srcs))
    sol = np.linalg.lstsq(X[:, active], mix, rcond=None)[0]
    g[active] = sol
    if (sol >= 0).all():
        break
    active = [a for a, v in zip(active, sol) if v >= 0]
res = np.linalg.norm(X @ g - mix) / np.linalg.norm(mix)
print(f"mix RMS {np.sqrt(mix.mean()):.4f}, fit residual {100 * res:.1f}%")
for s, gi in sorted(zip(srcs, g), key=lambda x: -x[1] * series(x[0]).mean()):
    print(f"  {s}: stream RMS {np.sqrt(series(s).mean()):.4f}  active {len(pw[s])}/{nwin} windows  "
          f"gain {np.sqrt(max(gi, 0)):.3f}  share of mix {100 * gi * series(s).mean() / mix.mean():5.1f}%")
