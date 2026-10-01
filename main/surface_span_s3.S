// Integer RGB565 glossy span. Xtensa windowed ABI, ESP32-S3 only.
// a2=row, a3=depth, a4=palette, a5=count, a6=z, a7=dz.
// Stack arguments: light, dl, ddl, phase. All recurrence state stays in registers.
// row/depth/palette are halfword aligned; phase is 0..3. Color and depth
// destinations do not overlap the immutable 2048-entry palette. Recurrences
// obey the same bounded int32 range as the reference C++ rasterizer.
// CLAMPS N followed by max(0) preserves unsigned saturation to N bits.
// LOOPGTZ consumes a5 before the body reuses it; nonpositive counts write nothing.
// The hardware loop and register window use the normal interrupt-safe ABI.
.section .iram1,"ax",@progbits
.align 4
.global scout_surface_span_s3
.type scout_surface_span_s3,@function
scout_surface_span_s3:
    entry a1, 32
    l32i a8, a1, 32
    l32i a9, a1, 36
    l32i a10, a1, 40
    l32i a11, a1, 44
    slli a11, a11, 1
    movi a14, 0
    movi a15, 2
    loopgtz a5, .Lspan_end
    srai a12, a6, 12
    srai a13, a8, 8
    l16ui a5, a3, 0
    clamps a12, a12, 16
    clamps a13, a13, 9
    max a12, a12, a14
    max a13, a13, a14
    addx8 a13, a13, a4
    add a13, a13, a11
    bgeu a5, a12, .Lspan_next
    l16ui a13, a13, 0
    s16i a12, a3, 0
    s16i a13, a2, 0
.Lspan_next:
    addi a2, a2, 2
    addi a3, a3, 2
    add a6, a6, a7
    add a8, a8, a9
    add a9, a9, a10
    xor a11, a11, a15
.Lspan_end:
    retw.n
.size scout_surface_span_s3, .-scout_surface_span_s3
