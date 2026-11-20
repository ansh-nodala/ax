// AX M:N coroutine context switch — AArch64
// void ax_coro_switch(void** from, void** to)
// Saves callee-saved x19-x30, d8-d15, sp into *from, restores from *to.
    .text
    .global ax_coro_switch
    .p2align 4
ax_coro_switch:
    // x0 = from, x1 = to
    // Save current context into *from
    mov x2, sp
    // store x19-x30, x29/x30, d8-d15, sp
    stp x19, x20, [x0, #0]
    stp x21, x22, [x0, #16]
    stp x23, x24, [x0, #32]
    stp x25, x26, [x0, #48]
    stp x27, x28, [x0, #64]
    stp x29, x30, [x0, #80]
    str x2, [x0, #96]
    stp d8, d9, [x0, #112]
    stp d10, d11, [x0, #128]
    stp d12, d13, [x0, #144]
    stp d14, d15, [x0, #160]
    str x30, [x0, #176]
    // Restore from *to
    ldp x19, x20, [x1, #0]
    ldp x21, x22, [x1, #16]
    ldp x23, x24, [x1, #32]
    ldp x25, x26, [x1, #48]
    ldp x27, x28, [x1, #64]
    ldp x29, x30, [x1, #80]
    ldr x2, [x1, #96]
    mov sp, x2
    ldp d8, d9, [x1, #112]
    ldp d10, d11, [x1, #128]
    ldp d12, d13, [x1, #144]
    ldp d14, d15, [x1, #160]
    ldr x30, [x1, #176]
    ret
