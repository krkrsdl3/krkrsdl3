# PSB parser regression tests

Run from the core repository with Python 3 and a C++17 compiler (`CXX`, default `g++`):

```sh
python3 tests/psb-parser/run.py --sanitize
```

The sanitizer command targets Linux GCC/Clang with AddressSanitizer and UBSan.
Omit `--sanitize` for a normal build. No game assets or renderer are needed.

The runner extracts production `parsePSBArray`, both readers' v1 name loops and
`refreshListInfo`, their numeric `readAllObjs` cases, and E-mote's `parseNumber`
and `parseReal`. It compiles those unchanged bodies with a checked memory-stream
test double and a scalar variant stand-in. This tests parser behavior, not the
full TJS object tree, decompression, or renderer integration.

Coverage: 1–8 byte array counts, wide entries and high-bit values, oversized
counts, invalid widths, truncated payloads, empty arrays, signed N3/N5/N6/N7
boundaries, v1 name loading, 32-bit name indices and value offsets.

For a negative control, point the same runner at an unpatched checkout:

```sh
python3 tests/psb-parser/run.py --source /path/to/upstream --sanitize
python3 tests/psb-parser/run.py --source /path/to/upstream --compat-only
```

At upstream `1b8b679`, the first command reports a stack-buffer-overflow when
reading an ArrayN5 count. The second skips arrays so the numeric and v1
regressions can complete independently (58 failed checks).
