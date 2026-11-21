#!/usr/bin/env python3
"""Dev tool: print the strongest pitches over time in a WAV captured by the harness."""
import sys, wave, struct, math
w = wave.open(sys.argv[1]); sr = w.getframerate(); n = w.getnframes()
d = struct.unpack("<%dh" % n, w.readframes(n))
step = int(sys.argv[2]) if len(sys.argv) > 2 else sr // 6
win = 1536
names = ["c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b"]
out = []
for st in range(0, n - win, step):
    seg = d[st:st + win]
    rms = math.sqrt(sum(v * v for v in seg) / win)
    best = []
    for note in range(24, 96):          # C1..B6 (midi)
        f = 440.0 * 2 ** ((note - 69) / 12.0)
        k = 2 * math.cos(2 * math.pi * f / sr); s1 = s2 = 0.0
        for v in seg:
            s0 = v + k * s1 - s2; s2 = s1; s1 = s0
        best.append((s1 * s1 + s2 * s2 - k * s1 * s2, note))
    best.sort(reverse=True)
    top = [names[nn % 12] + str(nn // 12 - 1) for p, nn in best[:2]]
    out.append("%5.2fs rms %5d  %s" % (st / sr, rms, " ".join(top)))
print("\n".join(out))
