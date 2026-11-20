    .text
    .global ax_neon_dot_f32
    .type ax_neon_dot_f32, %function
    .p2align 4
ax_neon_dot_f32:
    movi v0.4s, #0
    cmp x2, #4
    b.lo .Lneon_dot_reduce
.Lneon_dot_loop:
    ldr q1, [x0], #16
    ldr q2, [x1], #16
    fmla v0.4s, v1.4s, v2.4s
    subs x2, x2, #4
    cmp x2, #4
    b.hs .Lneon_dot_loop
.Lneon_dot_reduce:
    addv s0, v0.4s
    cbz x2, .Lneon_dot_done
.Lneon_dot_tail:
    ldr s1, [x0], #4
    ldr s2, [x1], #4
    fmadd s0, s1, s2, s0
    subs x2, x2, #1
    b.ne .Lneon_dot_tail
.Lneon_dot_done:
    ret
    .size ax_neon_dot_f32, .-ax_neon_dot_f32
    .section .note.GNU-stack,"",%progbits
