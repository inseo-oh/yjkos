.global archx86_geteflags
archx86_geteflags:
    pushf
    pop %eax
    ret

.global archx86_cli
archx86_cli:
    cli
    ret

.global archx86_sti
archx86_sti:
    sti
    ret

.global archx86_hlt
archx86_hlt:
    sti
    ret

.global archx86_rdtsc
archx86_rdtsc:
    push %ebp
    mov %esp, %ebp
    rdtsc
    mov 12(%ebp), %ecx
    mov %eax, (%ecx)
    mov 8(%ebp), %ecx
    mov %edx, (%ecx)
    pop %ebp
    ret

.global archx86_invlpg
archx86_invlpg:
	mov 4(%esp),%eax
	invlpg (%eax)
    ret

.global archx86_reloadcr3
archx86_reloadcr3:
    mov %cr3, %eax
    mov %eax, %cr3
    ret

.global archx86_readcr0
archx86_readcr0:
    mov %cr0, %eax
    ret

.global archx86_readcr2
archx86_readcr2:
    mov %cr2, %eax
    ret

.global archx86_readcr3
archx86_readcr3:
    mov %cr3, %eax
    ret

.global archx86_readcr4
archx86_readcr4:
    mov %cr3, %eax
    ret

.global archx86_readcr8
archx86_readcr8:
    mov %cr3, %eax
    ret