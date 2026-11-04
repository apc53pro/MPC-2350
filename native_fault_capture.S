.syntax unified
.cpu cortex-m33
.thumb

.section .time_critical.narm_fault_vector, "ax", %progbits
.global narm_fault_vector
.type narm_fault_vector, %function
.thumb_func
narm_fault_vector:
    cpsid i
    mrs r0, msp
    tst lr, #4
    it ne
    mrsne r0, psp
    mov r1, lr
    mrs r2, ipsr
    // Each core gets its own emergency MSP. No pushes use a damaged MSP.
    ldr r3, =0xd0000000
    ldr r3, [r3]
    lsls r3, r3, #9
    ldr r12, =narm_fault_emergency_stacks + 512
    add r12, r12, r3
    movs r3, #0
    msr msplim, r3
    msr msp, r12
    b narm_fault_capture_core
.size narm_fault_vector, .-narm_fault_vector
