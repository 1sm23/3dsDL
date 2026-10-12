# Mapping 3dsDL's screens onto a console photograph

The website shows real New Nintendo 3DS photos whose two screens are live
HTML, projected onto the photographed screens with CSS `matrix3d()`. These
scripts compute those matrices using only the Python standard library.

`screenfit.py` finds each screen and prints its matrix:

1. it flood-fills from a seed point inside the screen, accepting pixels that
   match the screen's own colour (dark for a switched-off LCD, or a given
   RGB with a tolerance for a pale touch screen);
2. it takes the region's outline and picks four starting corners as the
   largest-area quadrilateral among its extreme points in 16 directions;
3. it fits a straight line through each side's outline pixels, skipping the
   rounded ends, and intersects neighbouring lines for sub-pixel corners;
4. it solves the projective transform from the screen's native rectangle to
   that quadrilateral and writes it in CSS's column-major `matrix3d` form.

The matrices apply with `transform-origin: 0 0`, preceded by
`scale(var(--k))`, where `--k` is the rendered width divided by the photo's
pixel width (`--photo-w`); the site's script keeps `--k` current.

The commands that produced the site's matrices:

    # Hero (site/assets/new-3ds-angled.png). The console is turned, so the
    # touch screen's corners are rotated by one to start at its own
    # top-left, on the hinge side.
    python3 -I screenfit.py ../../site/assets/new-3ds-angled.png '{
      "top":    {"kind": "dark",  "seed": [811, 390], "max": 75, "size": [400, 240]},
      "bottom": {"kind": "color", "seed": [565, 767], "rgb": [137, 146, 156], "tol": 75,
                 "rotate": 1, "size": [320, 240]}}' angled.json

    # Install section (site/assets/new-3ds.png).
    python3 -I screenfit.py ../../site/assets/new-3ds.png '{
      "top":    {"kind": "dark", "seed": [809, 391], "max": 75, "size": [400, 240]},
      "bottom": {"kind": "dark", "seed": [584, 812], "max": 75, "size": [320, 240]}}' flat.json

Check a new fit by drawing translucent rectangles with the printed matrices
over the photo before using it: a wrong `rotate` shows up as text running
the wrong way.

`pngio.py` decodes and encodes 8-bit RGB and RGBA PNGs; `homography.py`
holds the transform solver.
