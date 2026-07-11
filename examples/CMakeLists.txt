# Examples build one executable per .cpp file. All they need is the
# header-only opine target — no test dependencies.

set(OPINE_EXAMPLES
    01_hello
    02_same_code_diff_type
    03_rbj_sort
    04_rounding_modes
    05_quantize
    06_overflow
    07_custom_format
    08_introspection
    09_rump
    10_pi_bbp
    11_fma_fusion
    12_number_line
    13_exact_decimal
)

foreach(ex ${OPINE_EXAMPLES})
    add_executable(${ex} ${ex}.cpp)
    target_link_libraries(${ex} PRIVATE opine)
endforeach()
