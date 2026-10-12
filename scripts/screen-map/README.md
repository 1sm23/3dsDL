# Mapping 3dsDL's screens onto a console photograph

The website's hero shows a real New Nintendo 3DS whose two screens are live
HTML, projected onto the photographed screens with CSS `matrix3d()`. These
scripts compute those matrices; they use only the Python standard library.

    python3 -I corners.py photo.png 70        # find the two screens' corners
    python3 -I homography.py photo.corners.json 1400 photo.matrices.json

`corners.py` decodes the PNG, thresholds dark pixels (the screens are the
only large dark regions on a white console), labels connected regions and
takes each of the two largest regions' four extreme corners. An optional
third argument erodes the mask first, which strips thin seams.

`homography.py` solves the projective transform from each screen's native
rectangle (400 x 240 and 320 x 240) to its photographed quadrilateral,
scaled to the width the site serves the photo at, and prints the two
`matrix3d()` values for `site/styles.css` (`--m-top`, `--m-bottom`). They
apply with `transform-origin: 0 0`, preceded by `scale(var(--k))` where
`--k` is the rendered width divided by that same serving width.

The extreme-corner step assumes each screen is a roughly diamond-shaped
quadrilateral in the photo. It fails on screens photographed nearly edge-on,
where two "corners" fall on one long edge; such a photo needs its corners
measured by hand.
