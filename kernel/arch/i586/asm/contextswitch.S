.global archi586_context_switch
archi586_context_switch:
    /*
     * Registers to preserve: ebx, esi, edi, ebp, esp, eflags
     * Register that doesn't need to be preserved: eax, ecx, edx
     */
    push %ebp
    mov %esp, %ebp
    pushf
    /* Save registers */
    push %ebx
    push %esi
    push %edi
    /*
     * Current stack looks like:
     * | EBP + 12 | Arg 1(New ESP value)
     * | EBP + 8  | Arg 0(Where to save current ESP)
     * | EBP + 4  | Return address
     * | EBP      | Saved EBP
     * | EBP - 4  | Saved EFLAGS
     * | EBP - 8  | Saved EBX
     * | EBP - 12 | Saved ESI
     * | EBP - 16 | Saved EDI <--- ESP is currently pointing here
     */
    /* Save current ESP value */
    mov 8(%ebp), %ecx
    mov %esp, (%ecx)
    /* Load new ESP value */
    mov 12(%ebp), %esp
    /*
     * Current stack looks like:
     * | ESP + 24 | Arg 0 for new thread
     * | ESP + 20 | Return address
     * | ESP + 16 | Saved EBP
     * | EBP + 12 | Saved EFLAGS
     * | ESP + 8  | Saved EBX
     * | ESP + 4  | Saved ESI
     * | ESP      | Saved EDI
     */
    /* Restore registers */
    pop %edi
    pop %esi
    pop %ebx
    popf
    pop %ebp
    ret

    