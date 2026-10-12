import zlib, struct, json, sys
from collections import deque

def decode_png(path):
    data = open(path, "rb").read()
    pos, idat, w, h, ct = 8, [], 0, 0, 0
    while pos < len(data):
        ln = struct.unpack(">I", data[pos:pos+4])[0]
        typ = data[pos+4:pos+8]; body = data[pos+8:pos+8+ln]
        if typ == b"IHDR":
            w, h, bd, ct = struct.unpack(">IIBB", body[:10]); assert bd == 8
        elif typ == b"IDAT": idat.append(body)
        pos += 12 + ln
    raw = zlib.decompress(b"".join(idat))
    bpp = {2: 3, 6: 4}[ct]; stride = w * bpp
    rows, prev, i = [], bytearray(stride), 0
    for y in range(h):
        f = raw[i]; i += 1
        line = bytearray(raw[i:i+stride]); i += stride
        if f == 1:
            for x in range(bpp, stride): line[x] = (line[x] + line[x-bpp]) & 255
        elif f == 2:
            for x in range(stride): line[x] = (line[x] + prev[x]) & 255
        elif f == 3:
            for x in range(stride):
                a = line[x-bpp] if x >= bpp else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 255
        elif f == 4:
            for x in range(stride):
                a = line[x-bpp] if x >= bpp else 0
                b = prev[x]; c = prev[x-bpp] if x >= bpp else 0
                p = a + b - c; pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
                line[x] = (line[x] + (a if (pa <= pb and pa <= pc) else (b if pb <= pc else c))) & 255
        rows.append(bytes(line)); prev = line
    return w, h, bpp, rows

path, thresh = sys.argv[1], int(sys.argv[2])
w, h, bpp, rows = decode_png(path)
mask = bytearray(w * h)
for y in range(h):
    r = rows[y]
    for x in range(w):
        o = x * bpp
        if bpp == 4 and r[o+3] < 128: continue
        if r[o] + r[o+1] + r[o+2] < 3 * thresh: mask[y*w + x] = 1

erode = int(sys.argv[3]) if len(sys.argv) > 3 else 0
for _ in range(erode):
    nm = bytearray(w * h)
    for y in range(1, h-1):
        for x in range(1, w-1):
            i = y*w + x
            if mask[i] and mask[i-1] and mask[i+1] and mask[i-w] and mask[i+w]: nm[i] = 1
    mask = nm
print("eroded", erode, "passes; dark px now", sum(mask))

# Connected components (4-neighbour) over the dark mask.
seen = bytearray(w * h); comps = []
for start in range(w * h):
    if not mask[start] or seen[start]: continue
    q = deque([start]); seen[start] = 1; pts = []
    while q:
        i = q.popleft(); pts.append((i % w, i // w))
        x, y = i % w, i // w
        for nx, ny in ((x-1,y),(x+1,y),(x,y-1),(x,y+1)):
            if 0 <= nx < w and 0 <= ny < h:
                j = ny*w + nx
                if mask[j] and not seen[j]: seen[j] = 1; q.append(j)
    comps.append(pts)
comps.sort(key=len, reverse=True)
print("components by size:", [len(c) for c in comps[:6]])

def corners(pts):
    e = erode
    return {"tl": (lambda p: [p[0]-e, p[1]-e])(min(pts, key=lambda p: p[0]+p[1])), "tr": (lambda p: [p[0]+e, p[1]-e])(max(pts, key=lambda p: p[0]-p[1])),
            "br": (lambda p: [p[0]+e, p[1]+e])(max(pts, key=lambda p: p[0]+p[1])), "bl": (lambda p: [p[0]-e, p[1]+e])(min(pts, key=lambda p: p[0]-p[1]))}
big = sorted(comps[:2], key=lambda c: min(p[1] for p in c))  # top screen first
res = {"image": [w, h], "top": corners(big[0]), "bottom": corners(big[1]),
       "sizes": [len(big[0]), len(big[1])]}
print(json.dumps(res))
open(path.replace(".png", ".corners.json"), "w").write(json.dumps(res))
