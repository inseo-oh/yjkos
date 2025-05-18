.global ArchI586_GetEFlags
ArchI586_GetEFlags:
    pushf
    pop %eax
    ret

.global ArchI586_Cli
ArchI586_Cli:
    cli
    ret

.global ArchI586_Sti
ArchI586_Sti:
    sti
    ret

.global ArchI586_Hlt
ArchI586_Hlt:
    sti
    ret

.global ArchI586_Rdtsc
ArchI586_Rdtsc:
    push %ebp
    mov %esp, %ebp
    rdtsc
    mov 12(%ebp), %ecx
    mov %eax, (%ecx)
    mov 8(%ebp), %ecx
    mov %edx, (%ecx)
    pop %ebp
    ret

.global ArchI586_Invlpg
ArchI586_Invlpg:
	mov 4(%esp),%eax
	invlpg (%eax)
    ret

.global ArchI586_ReloadCr3
ArchI586_ReloadCr3:
    mov %cr3, %eax
    mov %eax, %cr3
    ret

.global ArchI586_ReadCr0
ArchI586_ReadCr0:
    mov %cr0, %eax
    ret

.global ArchI586_ReadCr2
ArchI586_ReadCr2:
    mov %cr2, %eax
    ret

.global ArchI586_ReadCr3
ArchI586_ReadCr3:
    mov %cr3, %eax
    ret

.global ArchI586_ReadCr4
ArchI586_ReadCr4:
    mov %cr3, %eax
    ret

.global ArchI586_ReadCr8
ArchI586_ReadCr8:
    mov %cr3, %eax
    ret