.global archi586_geteflags
archi586_geteflags:
    pushf
    pop %eax
    ret

.global archi586_cli
archi586_cli:
    cli
    ret

.global archi586_sti
archi586_sti:
    sti
    ret

.global archi586_hlt
archi586_hlt:
    sti
    ret

.global archi586_rdtsc
archi586_rdtsc:
    push %ebp
    mov %esp, %ebp
    rdtsc
    mov 12(%ebp), %ecx
    mov %eax, (%ecx)
    mov 8(%ebp), %ecx
    mov %edx, (%ecx)
    pop %ebp
    ret

.global archi586_invlpg
archi586_invlpg:
	mov 4(%esp),%eax
	invlpg (%eax)
    ret

.global archi586_reload_cr3
archi586_reload_cr3:
    mov %cr3, %eax
    mov %eax, %cr3
    ret

.global archi586_read_cr0
archi586_read_cr0:
    mov %cr0, %eax
    ret

.global archi586_read_cr2
archi586_read_cr2:
    mov %cr2, %eax
    ret

.global archi586_read_cr3
archi586_read_cr3:
    mov %cr3, %eax
    ret

.global archi586_read_cr4
archi586_read_cr4:
    mov %cr3, %eax
    ret

.global archi586_read_cr8
archi586_read_cr8:
    mov %cr3, %eax
    ret