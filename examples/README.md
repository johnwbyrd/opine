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
| 04 | `rounding_modes` | The FP32 halfway value `1.0 + 2^−24` under all four supported rounding modes. Watch the directed modes swap sides between the +1.0 and −1.0 rows. |
| 05 | `quantize` | Quantization tour: convert π, 1/3, e, 1e−5, 1e5 into every supported small format and report bit pattern, decoded value, and relative error. |
| 06 | `overflow` | IEEE 754 §7.4 overflow: `240 × 240` in FP8 under each rounding mode, first with `inf_encoding=ReservedExponent` (goes to ±Inf when the mode carries upward, saturates otherwise) and then with `inf_encoding=None` (always saturates). |
| 07 | `custom_format` | Roll your own: define a 12-bit float (5 exp / 6 mant / 1 sign) in one `using` and compare its precision against fp8_e5m2 (same range, fewer bits) and float16 (same range, more bits). |
| 08 | `introspection` | A table of compile-time axis properties across every predefined Type: total bits, exponent width, semantic significand digits, exponent bias, sign method, NaN / Inf / denormal encodings. Every column is `constexpr`. |
| 09 | `rump` | Rump's polynomial at (77617, 33096). True value ≈ −0.827…. IEEE double confidently returns the wrong magnitude, float128 famously returns the *wrong sign* (+1.17), and float256 is the first precision that gets the right answer. float1024 lands ~300 correct decimal digits. |
| 10 | `pi_bbp` | Bailey-Borwein-Plouffe computation of π at every precision from float32 through float1024, using only `+ − × /`. Same code at every width; the "correct bits" column climbs 24 → 54 → 112 → 235 → 488 → 997 in lockstep with each format's precision. |

Examples 09 and 10 exercise formats past 128 bits (float256, float512,
float1024) on every compiler: past 128 bits the `storage_type` is a
limb array rather than a scalar, so GCC runs binary1024 just like
Clang.
