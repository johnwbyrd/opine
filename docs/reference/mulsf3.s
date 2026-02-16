; ===========================================================================
; mulsf3.s - Optimized IEEE 754 Single-Precision Multiplication for LLVM-MOS
; ===========================================================================
;
; WARNING! This code is extremely unlikely to be correct as currently written.
; DO NOT USE IT FOR ANY PURPOSE except purely pedagogical ones.
; 
; float __mulsf3(float a, float b)
;
; A space-optimized implementation that reduces code size through unified
; routines and register usage while maintaining full IEEE 754 compliance.
;
; ---------------------------------------------------------------------------
; Key optimizations
; ---------------------------------------------------------------------------
; 1. Operand A copied to contiguous memory, operand B used in place
; 2. Unified classify/normalize routines using indexed addressing
; 3. Aggressive register reuse with clear phase documentation
; 4. State table dispatch for clean special case handling
; 5. Dr Jefyll's 3-shift multiplication algorithm
;
; ---------------------------------------------------------------------------
; Calling convention
; ---------------------------------------------------------------------------
; Input:  a = A:X:RC2:RC3 (LSB to MSB)
;         b = RC4:RC5:RC6:RC7 (LSB to MSB)
; Output: result = A:X:RC2:RC3
; Preserved: RS0, RS10-RS15 (RC20-RC31)
;
; ---------------------------------------------------------------------------
; IEEE 754 algorithm overview
; ---------------------------------------------------------------------------
; The IEEE 754 standard defines floating-point multiplication as:
;   result = (-1)^(s1 XOR s2) * (m1*m2) * 2^(e1+e2)
;
; Where:
;   s1,s2 = sign bits (XORed for result sign)
;   m1,m2 = significands (1.fraction for normals, 0.fraction for denormals)
;   e1,e2 = unbiased exponents
;
; Our implementation follows these phases:
;
; PHASE 1: Unpacking and classification
;   - Extract sign, exponent, and mantissa from packed format
;   - Classify each operand (zero/denormal/normal/infinity/NaN)
;   - Handle special cases via dispatch table
;   IEEE 754 mandates specific behaviors for each operand type
;   combination (e.g., 0*infinity=NaN, NaN*anything=NaN)
;
; PHASE 2: Multiplication
;   - Add biased exponents and subtract bias
;   - Multiply mantissas (24*24 -> 48 bits)
;   - Normalize result (ensure MSB=1)
;   - Round to 24 bits using round-to-nearest-even
;   - Pack result back to IEEE format
;   This sequence ensures exact results for all normal cases
;   and proper handling of overflow/underflow
;
; ---------------------------------------------------------------------------

.include "imag-regs.inc"

; ---------------------------------------------------------------------------
; Constants
; ---------------------------------------------------------------------------
; These states allow us to use a 5×5 lookup table instead of nested branches
STATE_ZERO    = 0   ; Exponent and mantissa both zero
STATE_DENORM  = 1   ; Exponent zero, mantissa non-zero (gradual underflow)
STATE_NORMAL  = 2   ; Exponent 1-254 (vast majority of numbers)
STATE_INF     = 3   ; Exponent 255, mantissa zero (infinity)
STATE_NAN     = 4   ; Exponent 255, mantissa non-zero (Not a Number)

; Dispatch table actions - high bit indicates special handling needed
RETURN_ZERO = $80   ; Return signed zero immediately
RETURN_INF  = $81   ; Return signed infinity immediately
RETURN_NAN  = $82   ; Return quiet NaN immediately
DO_CALC     = $00   ; Perform actual multiplication

; ---------------------------------------------------------------------------
; Register allocation - Phase 1: Unpacking and classification
; ---------------------------------------------------------------------------
; We need both operands accessible during classification, plus working space
; for extracted components. Operand A must be copied to contiguous memory
; because it arrives scattered across A, X, RC2, RC3. Operand B is already
; contiguous in RC4-RC7, so we leave it there.

; Operand A (copied to contiguous block):
.set op_a,          __rc8      ; Full 32-bit operand A (RC8-RC11)
.set mant_a_lo,     __rc8      ; Mantissa low byte
.set mant_a_mid,    __rc9      ; Mantissa mid byte
.set mant_a_hi,     __rc10     ; Mantissa high byte (with exp bit 0)
.set sign_exp_a,    __rc11     ; Sign + 7 bits of exponent

; Operand B (used in place):
.set op_b,          __rc4      ; Full 32-bit operand B (RC4-RC7)
.set mant_b_lo,     __rc4      ; Mantissa low byte
.set mant_b_mid,    __rc5      ; Mantissa mid byte
.set mant_b_hi,     __rc6      ; Mantissa high byte (with exp bit 0)
.set sign_exp_b,    __rc7      ; Sign + 7 bits of exponent

; Working registers during unpacking:
.set exp_a,         __rc12     ; Extracted exponent A (8 bits)
.set exp_b,         __rc13     ; Extracted exponent B (8 bits)
.set state_a,       __rc14     ; Classification state A (5 values)
.set state_b,       __rc15     ; Classification state B (5 values)
.set sign_result,   __rc16     ; Result sign (XOR of input signs)
.set exp_result_lo, __rc17     ; Result exponent low (16-bit for overflow detection)
.set exp_result_hi, __rc18     ; Result exponent high
.set temp,          __rc19     ; Scratch register

; ---------------------------------------------------------------------------
; Register allocation - Phase 2: Multiplication
; ---------------------------------------------------------------------------
; After classification, we no longer need the original packed operands. The
; 48-bit product can overwrite them, saving registers. We must save the
; multiplicand in callee-saved registers because the multiplication loop
; needs it repeatedly while the multiplier shifts to zero.

; Product storage (reuses RC8-RC13):
.set product,       __rc8      ; 48-bit product (RC8-RC13)
.set product_0,     __rc8      ; Explicit names for documentation
.set product_1,     __rc9
.set product_2,     __rc10
.set product_3,     __rc11
.set product_4,     __rc12
.set product_5,     __rc13

; During multiplication, we need to preserve the multiplicand:
.set saved_mant_lo, __rc20     ; Saved multiplicand (mantissa A)
.set saved_mant_mid,__rc21     ; These are the only callee-saved
.set saved_mant_hi, __rc22     ; registers we actually use!

; ---------------------------------------------------------------------------
; IEEE 754 multiplication dispatch table
; ---------------------------------------------------------------------------
; IEEE 754 defines exact behavior for all 25 combinations of operand types.
; A 5*5 lookup table is smaller and faster than nested conditionals. Each
; entry encodes whether to return a special value immediately or perform
; the calculation.5×5 lookup table is smaller and faster than nested conditionals. Each
; entry encodes whether to return a special value immediately or perform
; the calculation.

.text
ieee_mult_table:
    ; Format: Each row is one state_a value, columns are state_b values
    ;         ZERO       DENORM     NORMAL     INF        NAN
    .byte RETURN_ZERO, RETURN_ZERO, RETURN_ZERO, RETURN_NAN,  RETURN_NAN  ; ZERO * ...
    .byte RETURN_ZERO, DO_CALC,     DO_CALC,     RETURN_INF,  RETURN_NAN  ; DENORM * ...
    .byte RETURN_ZERO, DO_CALC,     DO_CALC,     RETURN_INF,  RETURN_NAN  ; NORMAL * ...
    .byte RETURN_NAN,  RETURN_INF,  RETURN_INF,  RETURN_INF,  RETURN_NAN  ; INF * ...
    .byte RETURN_NAN,  RETURN_NAN,  RETURN_NAN,  RETURN_NAN,  RETURN_NAN  ; NAN * ...

; ---------------------------------------------------------------------------
; Macros for inlined operations
; ---------------------------------------------------------------------------
; These operations are called once each, so JSR/RTS overhead (4 bytes,
; 12 cycles per call) is pure waste. Macros give us logical separation
; without runtime cost. They also make it easy to implement alternative
; rounding modes in the future.

; ---------------------------------------------------------------------------
; MACRO: MULTIPLY_24X24
; ---------------------------------------------------------------------------
; Dr Jefyll's insight reduces the inner loop from 4 shifts (traditional)
; to 3 shifts, saving 24 cycles on a 24-bit multiply.
; Traditional: shift product right, shift multiplicand left, test, add
; This method: shift product right into space freed by multiplier shifting
;
; The multiplicand must be saved because we need it up to 24 times,
; but the multiplier naturally shifts to zero, freeing space for the
; product low bytes - a classic 6502 space optimization.

.macro multiply_24x24
    ; Clear product high bytes (low bytes accumulate in old operand A space)
    lda #0
    sta product_3       ; RC11
    sta product_4       ; RC12  
    sta product_5       ; RC13
    
    ; Save multiplicand - it's needed throughout the loop
    ; This is why we need callee-saved registers RC20-RC22
    lda mant_a_lo
    sta saved_mant_lo
    lda mant_a_mid
    sta saved_mant_mid
    lda mant_a_hi
    sta saved_mant_hi
    
    ldx #24             ; 24-bit multiplication
    
.mult_loop\@:           ; \@ makes label unique per macro expansion
    ; Test LSB of multiplier (mantissa B)
    lsr mant_b_hi       ; Shift multiplier right
    ror mant_b_mid
    ror mant_b_lo
    bcc .no_add\@       ; Skip if bit was 0
    
    ; Add saved multiplicand to product high bytes
    ; The product grows from the top down, eventually filling the
    ; space freed by the shifting multiplier
    clc
    lda product_3
    adc saved_mant_lo
    sta product_3
    lda product_4
    adc saved_mant_mid
    sta product_4
    lda product_5
    adc saved_mant_hi
    sta product_5
    
.no_add\@:
    ; Shift entire 48-bit product right
    ; This is the key optimization - only 3 shifts instead of 4
    lsr product_5
    ror product_4
    ror product_3
    ror product_2       ; Was mant_a_hi
    ror product_1       ; Was mant_a_mid
    ror product_0       ; Was mant_a_lo
    
    dex
    bne .mult_loop\@
.endmacro

; ---------------------------------------------------------------------------
; MACRO: ROUND_TO_NEAREST_EVEN
; ---------------------------------------------------------------------------
; This is the IEEE 754 default rounding mode, also called "banker's
; rounding". It avoids systematic bias by rounding ties (exactly halfway
; cases) to the nearest even number. This ensures that over many
; operations, rounding errors tend to cancel out rather than accumulate
; in one direction.
;
; The 48-bit product must be rounded to 24 bits. We examine:
;   - Guard bit (bit 23): The first bit we're discarding
;   - Round bit (bit 22): Helps detect the halfway case
;   - Sticky bit (bits 21-0): OR of all remaining bits

.macro round_to_nearest_even
    ; Check guard bit (bit 23 of 48-bit product)
    lda product_2
    and #$80
    beq .round_done\@   ; Guard = 0, round down
    
    ; Guard = 1, check if exactly halfway
    ; If we're exactly halfway, we need to round to even
    lda product_2
    and #$7F            ; Remaining bits after guard
    ora product_1       ; OR with lower bytes
    ora product_0
    bne .round_up\@     ; Not halfway, round up
    
    ; Exactly halfway - round to nearest even
    ; This prevents systematic bias
    lda product_3
    and #$01
    beq .round_done\@   ; Already even, keep it
    
.round_up\@:
    ; Increment mantissa
    ; We're rounding up, so add 1 to the LSB we're keeping
    inc product_3
    bne .round_done\@
    inc product_4
    bne .round_done\@
    inc product_5
    bne .round_done\@
    
    ; Mantissa overflowed (became 2.0)
    ; The increment rippled all the way up, giving us 10...0
    ; We need to shift right and increment the exponent
    ror product_5
    ror product_4
    ror product_3
    inc exp_result_lo
    bne .round_done\@
    inc exp_result_hi   ; Handle exponent overflow
    
.round_done\@:
.endmacro

; ---------------------------------------------------------------------------
; MACRO: PACK_NORMAL_RESULT
; ---------------------------------------------------------------------------
; Normal and denormal numbers pack differently. Normal numbers have an
; implicit leading 1 bit that must be removed, while denormals have an
; explicit leading 0 and exponent 0.

.macro pack_normal_result
    ; Bytes 0 and 1 are the low 16 bits of mantissa
    lda product_3       ; A = byte 0 (mantissa bits 7-0)
    pha                 ; Save for final return
    ldx product_4       ; X = byte 1 (mantissa bits 15-8)
    
    ; Byte 2: exp bit 0 + mantissa bits 22-16
    lda exp_result_lo
    lsr                 ; Bit 0 of exponent to carry
    lda product_5
    and #$7F            ; Remove implicit 1 bit
    ror                 ; Rotate in exponent bit 0
    sta __rc2
    
    ; Byte 3: sign + exp bits 7-1
    lda exp_result_lo
    lsr                 ; Already shifted once above
    sta __rc3
    lda sign_result
    beq .no_sign\@
    lda __rc3
    ora #$80            ; Set sign bit
    sta __rc3
.no_sign\@:
    pla                 ; Restore A = mantissa bits 7-0
    ; X already contains mantissa bits 15-8
.endmacro

; ---------------------------------------------------------------------------
; MACRO: PACK_DENORMAL_RESULT
; ---------------------------------------------------------------------------
; Denormal numbers have exponent 0 and no implicit bit. They represent
; gradual underflow - numbers too small for normal representation but
; not zero.

.macro pack_denormal_result
    lda product_3       ; A = byte 0 (mantissa bits 7-0)
    pha                 ; Save for final return
    ldx product_4       ; X = byte 1 (mantissa bits 15-8)
    
    ; Byte 2: mantissa bits 22-16 (no exponent bit)
    lda product_5
    and #$7F            ; Ensure no implicit bit
    asl                 ; Shift into position
    sta __rc2
    
    ; Byte 3: just sign (exponent is 0)
    lda sign_result
    beq .denorm_pos\@
    lda #$80
    bne .denorm_store\@ ; Always branch
.denorm_pos\@:
    lda #0
.denorm_store\@:
    sta __rc3
    
    pla                 ; Restore A = mantissa bits 7-0
    ; X already contains mantissa bits 15-8
.endmacro

; ---------------------------------------------------------------------------
; Main entry point
; ---------------------------------------------------------------------------
.global __mulsf3
__mulsf3:
    ; -----------------------------------------------------------------------
    ; Prologue: Save only the callee-saved registers we actually use
    ; -----------------------------------------------------------------------
    ; We analyzed the entire algorithm and found we only need RC20-RC22
    ; during multiplication. Saving all 12 registers in RS10-RS15 would
    ; waste 36 bytes and 54 cycles.
    
    lda __rc20
    pha
    lda __rc21
    pha
    lda __rc22
    pha
    
    ; -----------------------------------------------------------------------
    ; Phase 1a: Copy operand A to contiguous memory
    ; -----------------------------------------------------------------------
    ; Operand A arrives scattered across multiple registers (A, X, RC2, RC3)
    ; making it hard to manipulate. Operand B is already contiguous in
    ; RC4-RC7. By copying only A, we save 12 bytes of code while making
    ; both operands equally accessible.
    
    sta mant_a_lo       ; A register → RC8
    stx mant_a_mid      ; X register → RC9
    lda __rc2
    sta mant_a_hi       ; RC2 → RC10
    lda __rc3
    sta sign_exp_a      ; RC3 → RC11
    
    ; Operand B remains in place at RC4-RC7
    
    ; -----------------------------------------------------------------------
    ; Phase 1b: Extract signs and compute result sign
    ; -----------------------------------------------------------------------
    ; IEEE 754 specifies that the sign of a product is the XOR of the
    ; operand signs. This implements the rule that multiplying numbers
    ; with the same sign gives positive, different signs give negative.
    
    lda sign_exp_a      ; Get sign of A (bit 31)
    asl                 ; Sign bit to carry
    lda #0
    rol                 ; Sign now in bit 0
    sta sign_result
    
    lda sign_exp_b      ; Get sign of B (bit 31)
    asl                 ; Sign bit to carry
    lda #0
    rol                 ; Sign now in bit 0
    eor sign_result     ; XOR signs
    sta sign_result     ; Final result sign
    
    ; -----------------------------------------------------------------------
    ; Phase 1c: Extract and classify operands
    ; -----------------------------------------------------------------------
    ; IEEE 754 requires different handling for each operand type. By
    ; classifying first, we can use a simple table lookup instead of
    ; complex nested conditionals. The unified routine saves ~30 bytes.
    
    ldx #0              ; X=0 selects operand A at RC8
    jsr extract_and_classify
    sta state_a
    
    ldx #4              ; X=4 selects operand B at RC4
    jsr extract_and_classify
    sta state_b
    
    ; -----------------------------------------------------------------------
    ; Phase 1d: Dispatch based on state table
    ; -----------------------------------------------------------------------
    ; With 5 states per operand, we have 25 possible combinations. The
    ; table lookup is smaller and faster than 25 branches. Computing
    ; (state_a * 5) + state_b gives us the table index.
    
    lda state_a
    asl                 ; ×2
    asl                 ; ×4
    clc
    adc state_a         ; ×5 (since 5 = 4 + 1)
    clc
    adc state_b         ; Add state_b for final index
    tax
    
    lda ieee_mult_table,x
    bmi handle_special  ; Bit 7 set = special case
    
    ; Fall through to multiplication

; ---------------------------------------------------------------------------
; Phase 2: Normal multiplication path
; ---------------------------------------------------------------------------
; For normal operands (and some denormal combinations), we must perform
; actual multiplication following IEEE 754 rules.

do_multiplication:
    ; -----------------------------------------------------------------------
    ; Phase 2a: Add exponents
    ; -----------------------------------------------------------------------
    ; Both exponents are biased by 127. When we add them, we get:
    ; (e1+127) + (e2+127) = e1+e2+254
    ; We need e1+e2+127, so we subtract 127 to correct the bias.
    
    lda exp_a
    clc
    adc exp_b
    sta exp_result_lo
    lda #0
    adc #0              ; Capture carry for 16-bit result
    sta exp_result_hi
    
    ; Subtract bias (127)
    lda exp_result_lo
    sec
    sbc #127
    sta exp_result_lo
    lda exp_result_hi
    sbc #0
    sta exp_result_hi
    
    ; Check for overflow/underflow
    ; Results outside the representable range need special handling
    bmi handle_underflow    ; Negative = too small
    beq .check_range
    jmp handle_overflow     ; High byte > 0 = too large
    
.check_range:
    ; We need to check if the BIASED exponent >= 255
    ; Currently exp_result has the unbiased value (e1+e2)
    ; Biased value would be (e1+e2+127)
    ; So check if (e1+e2) >= 128, which means biased >= 255
    lda exp_result_lo
    cmp #128                ; 255 - 127 = 128
    bcs handle_overflow     ; Biased exponent would be >= 255
    
    ; -----------------------------------------------------------------------
    ; Phase 2b: Multiply mantissas (INLINED)
    ; -----------------------------------------------------------------------
    ; This operation is called exactly once, so the JSR/RTS overhead
    ; (4 bytes, 12 cycles) is pure waste.
    
    multiply_24x24      ; Macro expansion - no JSR overhead
    
    ; -----------------------------------------------------------------------
    ; Phase 2c: Normalize result
    ; -----------------------------------------------------------------------
    ; The 48-bit product of two 24-bit numbers with MSB=1 can have its
    ; MSB in bit 47 or 46. IEEE 754 requires normalized representation
    ; (MSB in a fixed position), so we may need one shift.
    
    lda product_5       ; Check MSB of product
    bmi .normalized     ; Bit 47 set - already normalized
    
    ; Need to shift left once
    ; MSB is in bit 46, we need it in bit 47
    asl product_0
    rol product_1
    rol product_2
    rol product_3
    rol product_4
    rol product_5
    
    ; Adjust exponent to compensate
    ; Shifting the mantissa left by 1 is like multiplying by 2,
    ; so we must decrement the exponent to maintain the value
    dec exp_result_lo
    lda exp_result_lo
    cmp #$FF
    bne .normalized
    dec exp_result_hi   ; Handle borrow
    
.normalized:
    ; -----------------------------------------------------------------------
    ; Phase 2d: Round to nearest even (INLINED)
    ; -----------------------------------------------------------------------
    ; We have 48 bits but need 24. IEEE 754 specifies round-to-nearest-even
    ; as the default mode.
    
    round_to_nearest_even   ; Macro expansion
    
    ; -----------------------------------------------------------------------
    ; Phase 2e: Pack result (INLINED)
    ; -----------------------------------------------------------------------
    ; If underflow occurred during rounding, we might have a denormal
    ; result that needs special packing.
    
    lda exp_result_lo
    ora exp_result_hi
    bne .pack_normal
    
    pack_denormal_result    ; Macro expansion
    jmp restore_and_return
    
.pack_normal:
    pack_normal_result      ; Macro expansion
    jmp restore_and_return

; ---------------------------------------------------------------------------
; Unified extract and classify routine
; ---------------------------------------------------------------------------
; Both operands need identical processing. Using indexed addressing with
; X as the offset saves ~30 bytes versus separate routines.

extract_and_classify:
    ; Extract exponent (bits 30-23)
    ; IEEE 754 splits the exponent across bytes. Bits 30-23 span from
    ; bit 7 of byte 3 down to bit 7 of byte 2.
    lda sign_exp_a,x    ; Get high byte (offset by X)
    asl                 ; Remove sign bit, shift exp bits 30-24
    sta temp
    lda mant_a_hi,x     ; Get byte with exp bit 23
    rol                 ; Exp bit 23 to carry
    rol temp            ; Complete 8-bit exponent
    lda temp
    
    ; Save exponent to appropriate register
    ; We need both exponents for the add
    cpx #0
    beq .save_exp_a
    sta exp_b
    jmp .check_exp
.save_exp_a:
    sta exp_a
    
.check_exp:
    ; Classify based on exponent value
    ; IEEE 754 defines special meanings for exp=0 and exp=255
    beq .zero_or_denorm     ; exp = 0: zero or denormal
    cmp #255
    beq .inf_or_nan         ; exp = 255: infinity or NaN
    
    ; Normal number (exp = 1-254)
    ; IEEE 754 normal numbers have an implicit leading 1 bit that's
    ; not stored. We make it explicit for multiplication.
    lda mant_a_hi,x
    ora #$80            ; Set bit 23 (the implicit 1)
    sta mant_a_hi,x
    lda #STATE_NORMAL
    rts
    
.zero_or_denorm:
    ; Check if mantissa is zero
    ; exp=0 means zero if mantissa=0, denormal if mantissa!=0
    lda mant_a_hi,x
    and #$7F            ; Ignore implicit bit position
    ora mant_a_mid,x
    ora mant_a_lo,x
    beq .is_zero
    
    ; Denormal - normalize it
    ; Denormals have no implicit 1, so we shift left until we get
    ; a 1 in the MSB position. This simplifies multiplication.
    jsr normalize_mantissa
    lda #STATE_DENORM
    rts
    
.is_zero:
    lda #STATE_ZERO
    rts
    
.inf_or_nan:
    ; Check mantissa to distinguish infinity from NaN
    ; exp=255 means infinity if mantissa=0, NaN if mantissa!=0
    lda mant_a_hi,x
    and #$7F
    ora mant_a_mid,x
    ora mant_a_lo,x
    beq .is_inf
    
    ; NaN - set implicit bit
    ; Even though NaN has no numeric value, we set the bit
    ; to maintain consistent representation
    lda mant_a_hi,x
    ora #$80
    sta mant_a_hi,x
    lda #STATE_NAN
    rts
    
.is_inf:
    ; Infinity - set implicit bit  
    lda mant_a_hi,x
    ora #$80
    sta mant_a_hi,x
    lda #STATE_INF
    rts

; ---------------------------------------------------------------------------
; Unified mantissa normalization for denormals
; ---------------------------------------------------------------------------
; Denormal numbers have a leading zero instead of the implicit one. By
; shifting left until we get a 1 in the MSB, we can use the same
; multiplication algorithm. We adjust the exponent to compensate for
; the shifts.

normalize_mantissa:
    ldy #0              ; Shift counter
    
.shift_loop:
    lda mant_a_hi,x     ; Check MSB
    bmi .normalized     ; MSB set, we're done
    
    ; Shift mantissa left
    ; Move the leading 1 into MSB position
    asl mant_a_lo,x
    rol mant_a_mid,x
    rol mant_a_hi,x
    iny
    
    cpy #24             ; Prevent infinite loop
    bcc .shift_loop
    
.normalized:
    ; Calculate effective exponent (1 - shifts)
    ; Each left shift multiplies by 2, so we need to subtract from
    ; the exponent. Denormals start with effective exponent -126,
    ; but we represent this as 1-shifts for consistency.
    tya
    sta temp 
    lda #1
    sec
    sbc temp
    
    ; Store to appropriate exponent register
    cpx #0
    beq .store_exp_a
    sta exp_b
    rts
.store_exp_a:
    sta exp_a
    rts

; ---------------------------------------------------------------------------
; Special value handlers
; ---------------------------------------------------------------------------
; IEEE 754 mandates specific results for special operand combinations.
; These handlers implement those rules efficiently.

handle_special:
    and #$0F            ; Isolate return type from table
    cmp #(RETURN_ZERO & $0F)
    beq return_signed_zero
    cmp #(RETURN_INF & $0F)
    beq return_signed_infinity
    ; Fall through to NaN

return_nan:
    ; IEEE 754 quiet NaN: sign=0, exp=255, mantissa bit 22=1, other bits=1
    ; Bit 22 is the quiet bit - must be set to indicate quiet NaN
    ; Format: [mant_lo] [mant_mid] [exp_bit0 + mant_hi] [sign + exp_7-1]
    
    lda #$FF            ; Mantissa low byte = all 1s
    tax                 ; X = mantissa mid byte = all 1s
    
    ; Byte 2: exp bit 0 (=1) + mantissa bits 22-16
    ; Bit 7 = exp bit 0 = 1
    ; Bit 6 = mantissa bit 22 = 1 (quiet NaN bit)  
    ; Bits 5-0 = mantissa bits 21-16 = all 1s
    lda #$FF            ; 11111111b
    sta __rc2
    
    ; Byte 3: sign (=0) + exp bits 7-1 (all 1s)
    lda #$7F            ; 01111111b - positive quiet NaN
    sta __rc3
    
    ; Set return registers properly
    lda #$FF            ; A = mantissa low byte
    ; X already = #$FF from above
    jmp restore_and_return

return_signed_zero:
    ; IEEE 754 zero: sign + exp=0 + mantissa=0
    ; Must set A:X:RC2:RC3 per calling convention
    
    lda #0              ; A = mantissa bits 7-0
    tax                 ; X = mantissa bits 15-8
    sta __rc2           ; RC2 = exp bit 0 + mantissa bits 22-16 = 0
    
    ; RC3 = sign bit + exp bits 7-1 = sign only (exp=0)
    lda sign_result
    beq .pos_zero
    lda #$80            ; Negative zero
    bne .store_sign     ; Always branch
.pos_zero:
    lda #0              ; Positive zero
.store_sign:
    sta __rc3
    
    ; Ensure A and X are properly set for return
    lda #0              ; A = mantissa bits 7-0
    ldx #0              ; X = mantissa bits 15-8
    jmp restore_and_return

return_signed_infinity:
    ; IEEE 754 infinity: sign + exp=255 + mantissa=0
    ; Bytes: [mant_lo=0] [mant_mid=0] [exp_bit0 + mant_hi=0] [sign + exp_bits7-1]
    
    lda #0              ; Mantissa low byte = 0
    tax                 ; Mantissa mid byte = 0 (in X)
    sta __rc2           ; Byte 2: exp bit 0 (=1) + mantissa bits = 0x80
    
    ; Byte 2 needs exp bit 0 set (exp=255 = 11111111b)
    lda #$80            ; Bit 7 = exp bit 0 = 1
    sta __rc2
    
    ; Byte 3: sign + exp bits 7-1 (1111111b = 0x7F)
    lda #$7F            ; Exp bits 7-1 all set
    ldx sign_result
    beq .pos_inf
    ora #$80            ; Set sign bit for negative infinity
.pos_inf:
    sta __rc3
    
    ; A already contains 0 from earlier LDA #0
    lda #0              ; Ensure A = mantissa low byte
    ldx #0              ; Ensure X = mantissa mid byte
    jmp restore_and_return

; ---------------------------------------------------------------------------
; Underflow handler
; ---------------------------------------------------------------------------
; IEEE 754 provides denormal numbers to represent values smaller than
; the minimum normal number. This gradual underflow prevents abrupt
; loss of precision at the bottom of the range.

handle_underflow:
    ; Check if gradual underflow possible
    lda exp_result_hi
    cmp #$FF            ; Should be -1 for mild underflow
    bne .total_underflow
    
    ; Calculate shift amount for denormal
    ; The negative exponent tells us how many positions to shift
    ; right to create a denormal representation
    lda exp_result_lo
    eor #$FF
    clc
    adc #1              ; Negate (two's complement)
    cmp #24
    bcs .total_underflow    ; Shift greater than or equal to 24 loses all precision
    
    ; Shift right to create denormal
    tax
.denorm_shift:
    lsr product_5
    ror product_4
    ror product_3
    ror product_2
    ror product_1
    ror product_0
    dex
    bne .denorm_shift
    
    ; Clear exponent for denormal
    ; Denormal numbers always have exponent field = 0
    lda #0
    sta exp_result_lo
    sta exp_result_hi
    
    ; Round the denormal result
    ; Even denormal results must be properly rounded
    round_to_nearest_even   ; Macro expansion
    
    ; Pack as denormal
    pack_denormal_result    ; Macro expansion
    jmp restore_and_return
    
.total_underflow:
    ; Results too small even for denormal representation become
    ; zero (with appropriate sign)
    jmp return_signed_zero

; ---------------------------------------------------------------------------
; Overflow handler
; ---------------------------------------------------------------------------
handle_overflow:
    ; Results too large for finite representation become infinity
    ; (with appropriate sign)
    jmp return_signed_infinity

; ---------------------------------------------------------------------------
; Epilogue: Restore and return
; ---------------------------------------------------------------------------
restore_and_return:
    ; Save return values
    pha                 ; Save A (return value)
    txa
    pha                 ; Save X (return value)
    
    ; Restore callee-saved registers in reverse order
    pla
    sta __rc22          ; Restore RC22 (was third push)
    pla  
    sta __rc21          ; Restore RC21 (was second push)
    pla
    sta __rc20          ; Restore RC20 (was first push)
    
    ; Restore return values
    pla
    tax                 ; Restore X
    pla                 ; Restore A
    rts

; ---------------------------------------------------------------------------
; End of implementation
; ---------------------------------------------------------------------------
