# Vendored quirc

These files are a copy of the `lib/` directory of
[dlbeer/quirc](https://github.com/dlbeer/quirc) at commit
`927d680904dc95fdff4cd9d022eb374b438ff8f2` (2025-05-20). quirc is distributed
under the ISC license, reproduced in `LICENSE-quirc.md` at the repository root.

This revision bounds the grid size before `quirc_extract` fills its bitmap and
replaces the recursive flood fill with an explicit work stack. The copy that
shipped up to 3dsDL 0.2.0 had neither fix.

One local change: `quirc_resize()` works out the old and new image sizes in
`size_t`, where upstream multiplies `int`s (a CodeQL alert).

To update, copy `lib/*.c` and `lib/*.h` from a newer upstream commit, carry
the change above over unless upstream has it, update the hash above and run
`make -C tests check`, which includes QR decoding tests.

Known upstream quirk: `quirc_resize()` calls `memcpy(dst, NULL, 0)` the first
time it runs. It is harmless in practice, but UBSan reports it, so the host
tests relax only that check (`nonnull-attribute`) for the quirc objects.
