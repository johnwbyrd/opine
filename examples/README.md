# examples/

Short, self-contained programs that show off what OPINE can do.
Each is one `.cpp` file that only needs the header library — no
MPFR, no SoftFloat, no test framework — and prints a small
narrative to standard output.

Build them together with the rest of the project (`cmake --build
build`) or one at a time (`cmake --build build --target hello`,
etc.).

| # | Example | What it shows |
|---|---------|---------------|
| 01 | `hello` | Minimal program: define a Type, evaluate `add`, print via the native bridge. |
| 02 | `same_code_diff_type` | One templated helper evaluates `(0.1 + 0.2) − 0.3` under fp8_e5m2, fp8_e4m3, fp8_e4m3fnuz, rbj FP8, FastType FP8, bfloat16, float16, float32, float64. The `add`/`sub` calls are identical; only the Type template parameter changes. |
| 03 | `rbj_sort` | The rbj differentiator: sort an array of floats by `std::sort`ing the storage as signed `int8_t`. The output is in float order for `RbjType` and reversed among the negatives for IEEE. |
| 04 | `rounding_modes` | The FP32 halfway value `1.0 + 2^−24` under all six rounding modes. Watch the directed modes swap sides between the +1.0 and −1.0 rows, the tie split between TiesToEven and TiesAway, and ToOdd jam the last bit. |
| 05 | `quantize` | Quantization tour: convert π, 1/3, e, 1e−5, 1e5 into every supported small format and report bit pattern, decoded value, and relative error. |
| 06 | `overflow` | IEEE 754 §7.4 overflow in FP8 under each rounding mode: `inf_encoding=ReservedExponent` goes to ±Inf when the mode carries upward and saturates otherwise; `inf_encoding=None` always saturates. A final `exceptions::ReturnStatus` run shows the overflow and inexact flags riding back with the result. |
| 07 | `custom_format` | Roll your own: define a 12-bit float (5 exp / 6 mant / 1 sign) in one `using` and compare its precision against fp8_e5m2 (same range, fewer bits) and float16 (same range, more bits). |
| 08 | `introspection` | A table of compile-time axis properties across every predefined Type: total bits, exponent width, semantic significand digits, exponent bias, sign method, NaN / Inf / denormal encodings. Every column is `constexpr`. |
| 09 | `rump` | Rump's polynomial at (77617, 33096). True value ≈ −0.827…. IEEE double confidently returns the wrong magnitude, float128 famously returns the *wrong sign* (+1.17), and float256 is the first precision that gets the right answer. float1024 lands ~300 correct decimal digits. |
| 10 | `pi_bbp` | Bailey-Borwein-Plouffe computation of π at every precision from float32 through float1024, using only `+ − × /`. Same code at every width; the "correct bits" column climbs 24 → 54 → 112 → 235 → 488 → 997 in lockstep with each format's precision. |
| 11 | `fma_fusion` | Why fused multiply-add exists: `(1+ε)² − (1+2ε)` comes out 0 unfused and exactly ε² fused, then `fma(x, y, −x·y)` recovers the exact rounding error of a multiply (−2⁻⁵⁴ for `3 × nearest(1/3)`) — the identity behind double-double arithmetic. |
| 12 | `number_line` | Floating-point values as a walkable set of points: a `nextUp` census of every fp8_e4m3 value from −240 to +240 (239 points: 224 normal, 14 subnormal, one zero), ulp gaps at 1.0 across five formats, the NaN and signed-zero rules of `minimum`/`maximumNumber`, and `copySign`. |
| 13 | `exact_decimal` | Correctly rounded text both ways: what "0.1" *really* stores at each width (every digit exact — 55 of them for float64), the toString→fromString round-trip guarantee, and sqrt(2) computed and printed from float32 up to binary1024, 100 correct digits at the top. |

Examples 09 and 10 exercise formats past 128 bits (float256, float512,
float1024) on every compiler: past 128 bits the `storage_type` is a
limb array rather than a scalar, so GCC runs binary1024 just like
Clang.
