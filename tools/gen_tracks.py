#!/usr/bin/env python3
"""Generates tracks_data.h (control points per track) and validates the layouts.
Usage: python3 tools/gen_tracks.py tracks_data.h
Tracks may contain hairpins (tight fillets) and one self-crossing (flyover, auto-detected in C)."""
import math, sys
NSEG = 800
def sg(x): return 1 if x >= 0 else -1
def polar(R, ex, ey, terms, n, sup=1.0):
    pts = []
    for i in range(n):
        t = 2 * math.pi * i / n
        r = 1 + sum(a * math.cos(k * t + ph) for a, k, ph in terms)
        c, s = math.cos(t), math.sin(t)
        if sup != 1.0: c = sg(c) * abs(c) ** sup; s = sg(s) * abs(s) ** sup
        pts.append((R * ex * r * c, R * ey * r * s))
    return pts
def gerono(a, b, n=60, start=0.0):
    return [(a * math.sin(t), b * math.sin(t) * math.cos(t)) for t in [start + 2 * math.pi * i / n for i in range(n)]]
def fillet(poly, step=20.0):
    """closed polygon [(x,y,r)] -> dense control points with circular-arc corners"""
    n = len(poly); arcs = []
    for i in range(n):
        P = poly[i - 1]; V = poly[i]; N = poly[(i + 1) % n]; r = V[2]
        u1 = (P[0] - V[0], P[1] - V[1]); l1 = math.hypot(*u1); u1 = (u1[0] / l1, u1[1] / l1)
        u2 = (N[0] - V[0], N[1] - V[1]); l2 = math.hypot(*u2); u2 = (u2[0] / l2, u2[1] / l2)
        th = math.acos(max(-1, min(1, u1[0] * u2[0] + u1[1] * u2[1])))
        d = r / math.tan(th / 2)
        dmax = 0.5 * min(l1, l2) * 1.0001
        if d > dmax: d = dmax; r = d * math.tan(th / 2)
        bis = (u1[0] + u2[0], u1[1] + u2[1]); bl = math.hypot(*bis); bis = (bis[0] / bl, bis[1] / bl)
        cd = r / math.sin(th / 2)
        C = (V[0] + bis[0] * cd, V[1] + bis[1] * cd)
        T1 = (V[0] + u1[0] * d, V[1] + u1[1] * d); T2 = (V[0] + u2[0] * d, V[1] + u2[1] * d)
        a1 = math.atan2(T1[1] - C[1], T1[0] - C[0]); a2 = math.atan2(T2[1] - C[1], T2[0] - C[0])
        dl = (a2 - a1 + math.pi) % (2 * math.pi) - math.pi
        m = max(3, math.ceil(abs(dl) * r / (step * 0.7)))
        arcs.append([(C[0] + r * math.cos(a1 + dl * k / m), C[1] + r * math.sin(a1 + dl * k / m)) for k in range(m + 1)])
    out = []
    for i in range(n):
        out += arcs[i]
        a = arcs[i][-1]; b = arcs[(i + 1) % n][0]
        L = math.hypot(b[0] - a[0], b[1] - a[1]); m = int(L // step)
        for k in range(1, m): out.append((a[0] + (b[0] - a[0]) * k / m, a[1] + (b[1] - a[1]) * k / m))
    return out
def roll_to(pts, target):
    i = min(range(len(pts)), key=lambda k: math.dist(pts[k], target))
    return pts[i:] + pts[:i]

old = [(0,-180),(120,-170),(220,-80),(200,40),(120,90),(140,170),(40,230),(-80,200),(-150,120),(-120,20),(-220,-40),(-150,-140)]
old = [(x * 1.6, y * 1.6) for x, y in old]
neon = roll_to(fillet([(-320,-240,70),(300,-240,70),(300,-90,50),(150,-90,50),(150,40,50),(330,40,60),(330,260,70),(-120,260,60),(-120,110,50),(-320,110,60)]), (-100,-240))
canyon = roll_to(fillet([(-420,-280,70),(380,-280,30),(380,-220,30),(-250,-220,30),(-250,-160,30),(380,-160,30),(380,-100,30),(-420,-100,70)]), (-100,-280))
harbor = roll_to(fillet([(-380,-220,60),(400,-220,60),(400,-60,45),(150,-60,30),(150,0,30),(400,0,50),(400,200,60),(-380,200,60)]), (0,-220))
death = roll_to(fillet([(-500,-250,45),(500,250,30),(500,-250,30),(-500,250,45)]), (-500,0))
jungle = roll_to(gerono(520, 250, 64), (520, 0))
# (name, points, half width, hairpins?, flyover?)
defs = [
 ("rust",   old, 15, False, False),
 ("neon",   neon, 15, False, False),
 ("frozen", polar(300,1.9,0.7,[(0.14,3,1.0),(0.06,5,0)],26), 16, False, False),
 ("swamp",  polar(330,1.0,1.0,[(0.22,3,0),(0.10,2,1.0)],26), 15, False, False),
 ("canyon", canyon, 15, True, False),
 ("volcano",polar(360,1.0,1.0,[(0.18,5,0.7),(0.08,2,0)],30), 16, False, False),
 ("harbor", harbor, 15, True, False),
 ("moon",   polar(340,1.3,0.9,[(0.28,1,0.4),(0.2,2,0.0)],26), 16, False, False),
 ("jungle", jungle, 15, False, True),
 ("death",  death, 15, True, True),
]
def spline(P, D=12000):
    M = len(P); out = []
    for i in range(D):
        u = i / D * M; s = int(u); f = u - s
        Q = [P[(s + k - 1) % M] for k in range(4)]
        f2 = f * f; f3 = f2 * f
        out.append([0.5 * ((2*Q[1][a]) + (-Q[0][a]+Q[2][a])*f + (2*Q[0][a]-5*Q[1][a]+4*Q[2][a]-Q[3][a])*f2 + (-Q[0][a]+3*Q[1][a]-3*Q[2][a]+Q[3][a])*f3) for a in (0, 1)])
    return out
def check(name, P, half, hairpin, flyover):
    d = spline(P); D = len(d); cum = [0]
    for i in range(D): cum.append(cum[-1] + math.dist(d[i], d[(i + 1) % D]))
    L = cum[D]; tr = []; j = 0
    for i in range(NSEG):
        t = L * i / NSEG
        while j < D - 1 and cum[j + 1] < t: j += 1
        tr.append(d[j])
    seg = L / NSEG; need = 2 * half + 8; close = []
    for i in range(NSEG):
        for k in range(i + 1, NSEG):
            cd = min(k - i, NSEG - (k - i))
            if cd * seg < 150: continue
            if math.dist(tr[i], tr[k]) < need: close.append((i, k))
    cross_ok = True; mind = 1e9
    if close:
        # all close pairs must belong to a single crossing neighbourhood
        c = tr[close[0][0]]
        cross_ok = flyover and all(math.dist(tr[i], c) < 120 and math.dist(tr[k], c) < 120 for i, k in close)
    elif flyover:
        cross_ok = False
    minr = 1e9
    for i in range(NSEG):
        a, b, c3 = tr[(i - 8) % NSEG], tr[i], tr[(i + 8) % NSEG]
        ab, bc, ca = math.dist(a, b), math.dist(b, c3), math.dist(c3, a)
        ar = abs((b[0]-a[0])*(c3[1]-a[1]) - (b[1]-a[1])*(c3[0]-a[0]))
        if ar > 1e-6: minr = min(minr, ab * bc * ca / (2 * ar))
    rneed = half * (1.05 if hairpin else 1.5)
    ok = cross_ok and minr > rneed
    print(f"{name:8s} len={L:6.0f} close_pairs={len(close):4d} cross_ok={cross_ok} minR={minr:5.1f} (need>{rneed:.0f}) pts={len(P):3d} {'OK' if ok else 'BAD'}", file=sys.stderr)
    return ok
if __name__ == "__main__":
    allok = all([check(*d) for d in defs])
    if len(sys.argv) > 1 and allok:
        with open(sys.argv[1], "w") as f:
            f.write("// generated by tools/gen_tracks.py - do not edit\n")
            for i, (n, p, h, hp, fl) in enumerate(defs):
                f.write(f"static const float TP{i}[][2] = {{" + ",".join("{%.1ff,%.1ff}" % (x, y) for x, y in p) + "};\n")
