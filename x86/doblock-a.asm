%include "x86inc.asm"

SECTION .text

;------------------------------------------------------------------------------------------
; void do_block_mmx( uint32_t *table, uint16_t *src, int bsize, uint16_t *dst )
;------------------------------------------------------------------------------------------
INIT_MMX mmx
cglobal do_block, 4,7,6
    mov    r4, r0
    mov    r5, r1

    mov    eax, dword [r5]
    movd   mm4,[r5+4]

    add    r5,r2
    add    r3,r2
    neg    r2

%if ARCH_X86_64
    %xdefine rr  r0
    %xdefine rrd r0d
%else
    %xdefine rr  r6
    %xdefine rrd r6d
%endif

ALIGN 16
lp8:
    movzx  r1d,  al
    movzx  rrd,  ah
    shr    eax,  16
    movd   mm0, [r4+r1*4]
    movd   mm1, [r4+0400h+rr*4]
    movzx  r1d,  al
    movzx  rrd,  ah
    movd   eax,  mm4
    movq   mm4, [r5+r2+8]
    movd   mm2, [r4+r1*4]
    movzx  r1d,  al
    movq   mm5, [r3+r2]
    movd   mm3, [r4+0400h+rr*4]
    movzx  rrd,  ah
    shr    eax,  16
    punpckldq mm0, [r4+r1*4]
    movzx  r1d,  al
    punpckldq mm1, [r4+0400h+rr*4]
    movzx  rrd,  ah
    punpckldq mm2, [r4+r1*4]
    pxor   mm1, mm0
    punpckldq mm3, [r4+0400h+rr*4]
    pxor   mm3, mm2
    pslld  mm3, 16
    movd   eax,  mm4
    pxor   mm1, mm5
    psrlq  mm4,32
    pxor   mm1, mm3
    movq  [r3+r2], mm1
    add    r2, 8
    jnz    lp8
    REP_RET
