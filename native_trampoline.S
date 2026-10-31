.syntax unified
.thumb
.fpu fpv5-d16

.equ ARCH_PSP, 0
.equ ARCH_EXC_RETURN, 4
.equ ARCH_CONTROL, 8
.equ ARCH_PSPLIM, 12
.equ SWITCH_TIMESLICE, 1
.equ SWITCH_EXIT, 3

.global narm_arch_resume
.type narm_arch_resume, %function
.thumb_func
narm_arch_resume:
    // The hardware frame only preserves r0-r3/r12/lr/pc/xPSR (and s0-s15).
    // Dispatch overwrites the other registers with the process context, so
    // preserve the firmware's complete AAPCS callee-saved state on MSP first.
    // Including r12 as padding keeps MSP 8-byte aligned at SVC entry.
    push {r4-r11, r12, lr}
    vpush {s16-s31}
    svc #0
    vpop {s16-s31}
    ldr r1, =narm_arch_switch_reason
    ldr r0, [r1]
    pop {r4-r11, r12, pc}
.size narm_arch_resume, .-narm_arch_resume

.global narm_arch_yield
.type narm_arch_yield, %function
.thumb_func
narm_arch_yield:
    ldr r1, =narm_arch_requested_reason
    str r0, [r1]
    svc #1
    bx lr
.size narm_arch_yield, .-narm_arch_yield

.global narm_arch_abort
.type narm_arch_abort, %function
.thumb_func
narm_arch_abort:
    ldr r1, =narm_arch_exit_status
    str r0, [r1]
    movs r0, #SWITCH_EXIT
    ldr r1, =narm_arch_requested_reason
    str r0, [r1]
    svc #2
    udf #0
.size narm_arch_abort, .-narm_arch_abort

.global narm_arch_task_return
.type narm_arch_task_return, %function
.thumb_func
narm_arch_task_return:
    ldr r1, =narm_arch_exit_status
    str r0, [r1]
    movs r0, #SWITCH_EXIT
    ldr r1, =narm_arch_requested_reason
    str r0, [r1]
    svc #2
    udf #0
.size narm_arch_task_return, .-narm_arch_task_return

.global narm_arch_svcall_handler
.type narm_arch_svcall_handler, %function
.thumb_func
narm_arch_svcall_handler:
    tst lr, #4
    bne .Lnarm_save_app
    ldr r0, =narm_arch_firmware_exc_return
    str lr, [r0]
    mrs r1, control
    ldr r0, =narm_arch_firmware_control
    str r1, [r0]
    ldr r0, =narm_arch_current_context
    ldr r0, [r0]
    cbnz r0, .Lnarm_dispatch_context_ok
    b.w .Lnarm_bad_context
.Lnarm_dispatch_context_ok:
    ldr r1, [r0, #ARCH_PSP]
    ldr r2, [r0, #ARCH_EXC_RETURN]
    ldr r3, [r0, #ARCH_CONTROL]
    orr r3, r3, #2
    msr control, r3
    ldr r3, [r0, #ARCH_PSPLIM]
    msr psplim, r3
    ldmia r1!, {r4-r11}
    tst r2, #0x10
    bne .Lnarm_restore_complete
    vldmia r1!, {s16-s31}
.Lnarm_restore_complete:
    msr psp, r1
    isb
    mov lr, r2
    bx lr

.Lnarm_save_app:
    ldr r0, =narm_arch_requested_reason
    ldr r0, [r0]
    ldr r1, =narm_arch_switch_reason
    str r0, [r1]
    b .Lnarm_save_common

.global narm_arch_pendsv_handler
.type narm_arch_pendsv_handler, %function
.thumb_func
narm_arch_pendsv_handler:
    // A process-to-firmware SVC leaves current_context published for the few
    // instructions needed to return from narm_arch_resume. Never interpret an
    // MSP exception frame from that window as a process PSP frame.
    tst lr, #4
    beq .Lnarm_exception_return
    ldr r0, =narm_arch_current_context
    ldr r0, [r0]
    cbnz r0, .Lnarm_pendsv_context_ok
    b.w .Lnarm_exception_return
.Lnarm_pendsv_context_ok:
    // Shared firmware services cannot be suspended with a lock or a partial
    // graphics/audio operation live. Pending commands yield at their entry
    // checkpoint instead. Only native program instructions are preemptible.
    mrs r1, psp
    // On Armv8-M the core frame starts at PSP even with FP stacking enabled:
    // PC is always word 6; the extra s0-s15/FPSCR words follow the core frame.
    ldr r2, [r1, #24]
    ldr r3, =narm_arch_text_begin
    ldr r3, [r3]
    cmp r2, r3
    blo .Lnarm_exception_return
    ldr r3, =narm_arch_text_end
    ldr r3, [r3]
    cmp r2, r3
    bhs .Lnarm_exception_return
    movs r1, #SWITCH_TIMESLICE
    ldr r2, =narm_arch_switch_reason
    str r1, [r2]

.Lnarm_save_common:
    ldr r0, =narm_arch_current_context
    ldr r0, [r0]
    cbnz r0, .Lnarm_save_context_ok
    b.w .Lnarm_bad_context
.Lnarm_save_context_ok:
    mrs r1, psp
    tst lr, #0x10
    bne .Lnarm_no_fp_save
    vstmdb r1!, {s16-s31}
.Lnarm_no_fp_save:
    stmdb r1!, {r4-r11}
    str r1, [r0, #ARCH_PSP]
    str lr, [r0, #ARCH_EXC_RETURN]
    mrs r2, control
    str r2, [r0, #ARCH_CONTROL]
    mrs r2, psplim
    str r2, [r0, #ARCH_PSPLIM]
    ldr r2, =narm_arch_firmware_control
    ldr r2, [r2]
    bic r2, r2, #2
    msr control, r2
    isb
    ldr r2, =narm_arch_firmware_exc_return
    ldr lr, [r2]
    bx lr

.Lnarm_exception_return:
    bx lr
.Lnarm_bad_context:
    udf #0
.size narm_arch_svcall_handler, .-narm_arch_svcall_handler
.size narm_arch_pendsv_handler, .-narm_arch_pendsv_handler

// Bounded boot-time probe: changes both integer and FP callee-saved state,
// then stays in native text long enough for two 5 ms timer interrupts. If the
// timer fails, the loop still completes and returns through the normal exit
// trampoline, allowing the firmware to report a failed self-test.
.global narm_arch_probe_entry
.type narm_arch_probe_entry, %function
.thumb_func
narm_arch_probe_entry:
    movs r4, #0x44
    movs r5, #0x55
    movs r6, #0x66
    movs r7, #0x77
    mov.w r8, #0x88
    mov.w r9, #0x99
    mov.w r10, #0xaa
    mov.w r11, #0xbb
    mov.w r1, #0x3f800000
    vmov s0, r1
    vmov s16, r1
    vmov s31, r1
    mov.w r0, #0x01000000
.Lnarm_probe_loop:
    subs r0, r0, #1
    bne .Lnarm_probe_loop
    movs r0, #0
    bx lr
.global narm_arch_probe_end
narm_arch_probe_end:
.size narm_arch_probe_entry, .-narm_arch_probe_entry
