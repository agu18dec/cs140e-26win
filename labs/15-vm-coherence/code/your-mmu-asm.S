@ Must carefully read B2 of the armv6 manual.  You cannot rely on "well it
@ worked on my test case": mistakes cause stale data, which may or may not
@ show up in your test case (likely not) despite being wildly broken.
@
@ Note: Rd is allegedly a read-only parameter to these instructions, but
@ the manual says SBZ (should be zero).  I think (and the linux source seems
@ to think) that this means we have to explicitly set it to 0.  Not setting
@ Rd=0 is an easy mistake to make.  Perhaps is worth building in (Linux does
@ not, but) --- the cost of an extra mov is negligible given our other 
@ overheads.
@
@ Alot of people put these instructions in inline assembly, wrapped up by 
@ function calls (e.g., cs107e's MMU code).  This practice is dangerous. 
@ For example, if you are flushing caches, you cannot be sure that the 
@ function return, etc does not then reload them, esp w.r.t. the BTB.  I 
@ think the only safe approach is to put directly in assembly so you are 
@ guaranteed no branch, indirect jump, load/store happens in between.
@
@ A lot of MMU code out there appears pretty broken b/c they don't do enough
@ flushing operations and/or rely on gcc code generation to not do the above.

#include "rpi-asm.h"
#include "armv6-coprocessor-asm.h"


@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@ many page numbers are in 
@    armv6-coprocessor-asm.h
@ and the other armv6-*.h files

// just read the cache type: use C code to figure out if it is unified
// b6-13, b6-14
MK_FN(get_cache_type)
    asm_not_implemented()
    bx lr

// void reset(void)
// clear the dcache, icache, tlbs
MK_FN(mmu_reset)
    // Use r0 as the "SBZ=0" operand for maintenance ops
    mov r0, #0
    INV_DCACHE(r0)
    INV_TLB(r0)
    INV_ICACHE(r0)

    DSB(r0)

    // FLUSH_BTB(r0)

    // Make BTB flush + CP15-visible effects visible to instruction fetch.
    PREFETCH_FLUSH(r0)

    bx lr

// void cp15_domain_ctrl_wr(uint32_t dom_reg);
// NOTE:
//   - definitely need to do prefetch flush 
//   - I don't *think* we need the full cp15 sync: flush_btb, 
//     dsb, prefetch flush.
MK_FN(cp15_domain_ctrl_wr)
    DOMAIN_CTRL_WR(r0)
    mov r1, #0
    PREFETCH_FLUSH(r1)
    bx lr

// void cp15_ctrl_reg1_wr(cp15_ctrl_reg1_t r);
MK_FN(cp15_ctrl_reg1_wr)
    CONTROL_REG1_WR(r0)     @ write CP15 c1
    mov r1, #0
    DSB(r1)                 @ conservative completion fence
    PREFETCH_FLUSH(r1)      @ make CP15 change visible to subsequent instructions
    bx lr

MK_FN(cp15_ctrl_reg1_rd)
    CONTROL_REG1_RD(r0)
    bx lr

// void cp15_set_procid_ttbr0(uint32_t proc_and_asid, fld_t *pt);
// sequence from b2-25
MK_FN(cp15_set_procid_ttbr0)
    // r0 = proc_and_asid (pid<<8 | asid), r1 = pt
    // r2 = 0 for SBZ operands and ASID=0 hack
    mov r2, #0
    DSB(r2)

    // Change ASID to 0 to prevents old/new ASID from being associated with wrong page-table walks.
    ASID_SET(r2)
    FLUSH_BTB(r2)
    PREFETCH_FLUSH(r2)

    // translation table base registers/control:
    //    - TTBR0 = pt
    //    - TTBR1 = 0
    //    - TTBCR (N) = 0 so always use TTBR0 (B4-41)
    TTBR0_SET(r1)
    TTBR1_SET(r2)
    TTBR_BASE_CTRL_WR(r2)

    // ensure ttbr change is visible
    FLUSH_BTB(r2)
    PREFETCH_FLUSH(r2)

    // ASID to new value (proc_and_asid packed )
    DSB(r2)
    ASID_SET(r0)
    FLUSH_BTB(r2)


    PREFETCH_FLUSH(r2)
    bx lr

// void mmu_disable_set_asm(cp15_ctrl_reg1_t c);
MK_FN(mmu_disable_set_asm)
    mov r1, #0
    DSB(r1)
    // dcache is not accessible when MMU is off: write back dirty lines now to main memory
    // Clean+invalidate is fine here? (change translation regime).
    // once MMU is off, the dcache becomes inaccessible on ARM1176, and dirty lines may never hit RAM
    CLEAN_INV_DCACHE(r1)
    DSB(r1)

    // disable MMU
    CONTROL_REG1_WR(r0)
    INV_ICACHE(r1)
    DSB(r1)
    // DSB(r1)
    FLUSH_BTB(r1)
    DSB(r1)
    PREFETCH_FLUSH(r1)

    @ note: this piece of code is mapped into the same place in both
    @ MMU-enabled and MMU-disabled so BTB and Prefetch entries work identically
    @ to both MMU-enable and disable.   Without this requirement I believe is
    @ very hard to get right (perhaps impossible?) since we cannot flush
    @ the BTB and prefetch and switch MMU on/off atomically.  thus, would
    @ flush, but instructions could get fetched before we could disable,
    @ defeating the purpose.
    bx lr

// void mmu_enable_set_asm(cp15_ctrl_reg1_t c);
MK_FN(mmu_enable_set_asm)
    // r1 will be our SBZ=0 operand for CP15 maintenance ops
    mov r1, #0
    // Enable MMU 
    bic r0, r0, #(1 << 12)  @ disable I-cache
    CONTROL_REG1_WR(r0)
    FLUSH_BTB(r1)
    PREFETCH_FLUSH(r1)
    INV_ICACHE(r1)
    DSB(r1)
    orr r0, r0, #(1 << 12)    @ re-enable I-cache
    DSB(r1)
    CONTROL_REG1_WR(r0)
    PREFETCH_FLUSH(r1)
    bx lr

MK_FN(mmu_sync_pte_mods)
    mov r0, #0
    // clean/invalidate dcache
    CLEAN_INV_DCACHE(r0)
    // invalidate I-cache
    INV_ICACHE(r0)
    DSB(r0)
    // invalidate TLB
    INV_TLB(r0)
    // invalidate BTB
    FLUSH_BTB(r0)
    // Ensure TLB/cache/BTB maintenance complete.
    DSB(r0)
    PREFETCH_FLUSH(r0)
    bx lr



