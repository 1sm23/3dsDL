import json, sys

def solve(A, b):
    n = len(A); M = [row[:] + [b[i]] for i, row in enumerate(A)]
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(M[r][c])); M[c], M[p] = M[p], M[c]
        for r in range(n):
            if r != c and M[r][c]:
                f = M[r][c] / M[c][c]
                M[r] = [M[r][k] - f * M[c][k] for k in range(n + 1)]
    return [M[i][n] / M[i][i] for i in range(n)]

def homography(src, dst):
    A, b = [], []
    for (x, y), (u, v) in zip(src, dst):
        A.append([x, y, 1, 0, 0, 0, -x*u, -y*u]); b.append(u)
        A.append([0, 0, 0, x, y, 1, -x*v, -y*v]); b.append(v)
    a, bb, c, d, e, f, g, h = solve(A, b)
    # CSS matrix3d is column-major; transform-origin must be 0 0.
    return [a, d, 0, g,  bb, e, 0, h,  0, 0, 1, 0,  c, f, 0, 1]

def build(corners_file, site_w, screens):
    data = json.load(open(corners_file)); img_w = data["image"][0]; s = site_w / img_w
    out = {}
    for name, (W, H) in screens.items():
        q = data[name]
        dst = [(q[k][0]*s, q[k][1]*s) for k in ("tl", "tr", "br", "bl")]
        m = homography([(0,0),(W,0),(W,H),(0,H)], dst)
        out[name] = {"w": W, "h": H, "matrix3d": "matrix3d(" + ", ".join(f"{v:.6f}" for v in m) + ")", "dst": dst}
    out["site_w"] = site_w; out["site_h"] = round(data["image"][1] * s)
    return out

if __name__ == "__main__":
    res = build(sys.argv[1], int(sys.argv[2]), {"top": (400, 240), "bottom": (320, 240)})
    print(json.dumps({k: (v if k.startswith("site") else v["matrix3d"]) for k, v in res.items()}, indent=1))
    json.dump(res, open(sys.argv[3], "w"))
