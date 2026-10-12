"""Find a photographed screen's corners: flood-fill from a seed on the
screen's own colour, then fit straight lines to the region's four edges and
intersect them. With a native size per screen it also prints the CSS
matrix3d() that maps a screen drawn at that size onto the photo.

    python3 -I screenfit.py photo.png SPEC out.json

SPEC is JSON, one entry per screen:
  "kind": "dark" with "max" (largest channel value counted as screen), or
  "kind": "color" with "rgb" and "tol" (sum of channel differences);
  "seed": [x, y] inside the screen; "size": [width, height] in 3DS pixels;
  "rotate": n to rotate the found corners so the first is the screen's own
  top-left (needed when the console is turned in the photo).
"""
import sys, json, math
from collections import deque
sys.path.insert(0, __file__.rsplit("/", 1)[0])
import pngio
from homography import homography

def flood(rows, w, h, seed, accept):
    sx, sy = seed
    seen = bytearray(w * h)
    q = deque([(sx, sy)]); seen[sy*w + sx] = 1
    region = []
    while q:
        x, y = q.popleft(); region.append((x, y))
        for nx, ny in ((x-1, y), (x+1, y), (x, y-1), (x, y+1)):
            if 0 <= nx < w and 0 <= ny < h and not seen[ny*w + nx]:
                o = nx * 4
                px = rows[ny][o:o+4]
                if px[3] > 200 and accept(px):
                    seen[ny*w + nx] = 1; q.append((nx, ny))
    return region, seen

def fill_holes(region, w, h):
    """Close small holes (reflections, glyphs) so the boundary is the outline."""
    inside = bytearray(w * h)
    for x, y in region: inside[y*w + x] = 1
    outside = bytearray(w * h)
    q = deque()
    for x in range(w):
        for y in (0, h-1):
            if not inside[y*w+x] and not outside[y*w+x]: outside[y*w+x] = 1; q.append((x, y))
    for y in range(h):
        for x in (0, w-1):
            if not inside[y*w+x] and not outside[y*w+x]: outside[y*w+x] = 1; q.append((x, y))
    while q:
        x, y = q.popleft()
        for nx, ny in ((x-1, y), (x+1, y), (x, y-1), (x, y+1)):
            if 0 <= nx < w and 0 <= ny < h and not inside[ny*w+nx] and not outside[ny*w+nx]:
                outside[ny*w+nx] = 1; q.append((nx, ny))
    return outside  # 0 = region or enclosed hole

def boundary(outside, w, h, bbox):
    x0, y0, x1, y1 = bbox
    pts = []
    for y in range(max(1, y0-1), min(h-1, y1+2)):
        for x in range(max(1, x0-1), min(w-1, x1+2)):
            if not outside[y*w+x] and (outside[y*w+x-1] or outside[y*w+x+1] or outside[(y-1)*w+x] or outside[(y+1)*w+x]):
                pts.append((x + 0.5, y + 0.5))
    return pts

def fit_line(pts):
    n = len(pts); mx = sum(p[0] for p in pts)/n; my = sum(p[1] for p in pts)/n
    sxx = sum((p[0]-mx)**2 for p in pts); syy = sum((p[1]-my)**2 for p in pts)
    sxy = sum((p[0]-mx)*(p[1]-my) for p in pts)
    ang = 0.5 * math.atan2(2*sxy, sxx - syy)
    return (mx, my, math.cos(ang), math.sin(ang))

def intersect(l1, l2):
    x1, y1, dx1, dy1 = l1; x2, y2, dx2, dy2 = l2
    den = dx1*dy2 - dy1*dx2
    t = ((x2-x1)*dy2 - (y2-y1)*dx2) / den
    return (x1 + t*dx1, y1 + t*dy1)

def seg_dist(p, a, b):
    ax, ay = a; bx, by = b; px, py = p
    vx, vy = bx-ax, by-ay; L = vx*vx + vy*vy
    t = max(0, min(1, ((px-ax)*vx + (py-ay)*vy) / L))
    return math.hypot(px - (ax + t*vx), py - (ay + t*vy)), t

def area(q):
    return abs(sum(q[i][0]*q[(i+1) % 4][1] - q[(i+1) % 4][0]*q[i][1] for i in range(4))) / 2

def initial_corners(pts):
    """Extreme points in 16 directions; keep the 4 enclosing the most area,
    ordered TL, TR, BR, BL around their centre."""
    from itertools import combinations
    cands = []
    for k in range(16):
        a = k * math.pi / 8; dx, dy = math.cos(a), math.sin(a)
        p = max(pts, key=lambda q: q[0]*dx + q[1]*dy)
        if p not in cands: cands.append(p)
    best = None
    for combo in combinations(cands, 4):
        cx = sum(p[0] for p in combo) / 4; cy = sum(p[1] for p in combo) / 4
        ordered = sorted(combo, key=lambda p: math.atan2(p[1]-cy, p[0]-cx))
        a = area(ordered)
        if best is None or a > best[0]: best = (a, ordered)
    q = best[1]
    # atan2 ordering starts at the left-most-up point going clockwise in
    # screen coordinates; rotate so the first is the top-left one.
    start = min(range(4), key=lambda i: q[i][0] + q[i][1])
    return q[start:] + q[:start]

def fit_quad(pts):
    c = initial_corners(pts)
    sides = [[], [], [], []]
    for p in pts:
        best = min(range(4), key=lambda i: seg_dist(p, c[i], c[(i+1) % 4])[0])
        d, t = seg_dist(p, c[best], c[(best+1) % 4])
        if 0.08 < t < 0.92 and d < 6:  # skip rounded corners and strays
            sides[best].append(p)
    lines = [fit_line(s) for s in sides]
    corners = [intersect(lines[(i-1) % 4], lines[i]) for i in range(4)]
    # corners[i] is where side i-1 meets side i: TL, TR, BR, BL
    return {"initial": c, "corners": corners, "side_points": [len(s) for s in sides]}

if __name__ == "__main__":
    path = sys.argv[1]; spec = json.loads(sys.argv[2])
    w, h, bpp, rows = pngio.decode(path); assert bpp == 4
    out = {"image": [w, h]}
    for name, s in spec.items():
        seed = tuple(s["seed"])
        if s["kind"] == "dark":
            lim = s["max"]
            accept = lambda px, lim=lim: max(px[0], px[1], px[2]) < lim
        else:
            ref, tol = s["rgb"], s["tol"]
            accept = lambda px, ref=ref, tol=tol: abs(px[0]-ref[0]) + abs(px[1]-ref[1]) + abs(px[2]-ref[2]) < tol
        region, _ = flood(rows, w, h, seed, accept)
        xs = [p[0] for p in region]; ys = [p[1] for p in region]
        bbox = (min(xs), min(ys), max(xs), max(ys))
        outside = fill_holes(region, w, h)
        pts = boundary(outside, w, h, bbox)
        q = fit_quad(pts)
        k = s.get("rotate", 0) % 4
        corners = q["corners"][k:] + q["corners"][:k]
        entry = {"region_px": len(region), "bbox": bbox, "side_points": q["side_points"], "corners": corners}
        if "size" in s:
            sw, sh = s["size"]
            m = homography([(0, 0), (sw, 0), (sw, sh), (0, sh)], corners)
            entry["matrix3d"] = "matrix3d(" + ", ".join(f"{v:.6f}" for v in m) + ")"
        out[name] = entry
        print(name, "region", len(region), "side points", q["side_points"])
        print("   corners TL TR BR BL:", [tuple(round(v, 1) for v in c) for c in corners])
        if "matrix3d" in entry:
            print("  ", entry["matrix3d"])
    json.dump(out, open(sys.argv[3], "w"))
