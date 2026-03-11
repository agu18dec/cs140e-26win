// put your code here.
//
#include "rpi.h"
#include "libc/bit-support.h"

// has useful enums and helpers.
#include "asm-helpers.h"
#include "vector-base.h"
#include "pinned-vm.h"
#include "mmu.h"
#include "procmap.h"



// TLB Lockdown Index Register: c15, c4, 2
cp_asm_set_raw(lockdown_index, p15, 5, c15, c4, 2)
cp_asm_get    (lockdown_index, p15, 5, c15, c4, 2)

cp_asm_set_raw(lockdown_va,    p15, 5, c15, c5, 2)
cp_asm_get    (lockdown_va,    p15, 5, c15, c5, 2)

// TLB Lockdown PA Register: c15, c6, 2
cp_asm_set_raw(lockdown_pa,    p15, 5, c15, c6, 2)
cp_asm_get    (lockdown_pa,    p15, 5, c15, c6, 2)

cp_asm_set_raw(lockdown_attr,  p15, 5, c15, c7, 2)
cp_asm_get    (lockdown_attr,  p15, 5, c15, c7, 2)

// generate the _get and _set methods.
// (see asm-helpers.h for the cp_asm macro 
// definition)
// arm1176.pdf: 3-149

static void *null_pt = 0;

// should we have a pinned version?
void domain_access_ctrl_set(uint32_t d) {
    staff_domain_access_ctrl_set(d);
}

// fill this in based on the <1-test-basic-tutorial.c>
// NOTE: 
//    you'll need to allocate an invalid page table
void pin_mmu_init(uint32_t domain_reg) {
    // null page table w/ 4096 entries (4GB / 1MB) 4 bytes per entry
    if(!null_pt) {
        null_pt = kmalloc_aligned(4096 * 4, 1 << 14);
        demand(null_pt, "kmalloc_aligned failed for null page table");
        demand(((uint32_t)null_pt & ((1<<14)-1)) == 0, "null_pt not 16KB aligned");

        // zero-fill so all entries are invalid
        memset(null_pt, 0, 4096 * 4);
    }
    staff_mmu_init();
    domain_access_ctrl_set(domain_reg);
    return;
}

// do a manual translation in tlb:
//   1. store result in <result>
//   2. return 1 if entry exists, 0 otherwise.
//
// NOTE: mmu must be on (confusing).
int tlb_contains_va(uint32_t *result, uint32_t va) {
    assert(mmu_is_enabled());

    // 3-79
    assert(bits_get(va, 0,2) == 0);
    uint32_t par;
    // "translate VA in current mode, privileged read" from 3-79
    asm volatile("mcr p15, 0, %0, c7, c8, 0" :: "r"(va)); // tlb/page lookup
    // "read PA Register" from 3-80
    asm volatile("mrc p15, 0, %0, c7, c4, 0" : "=r"(par)); // contains success or failure of lookup
    if(par & 1) { // reading par was unsuccesful so no entry exists
        *result = par;
        return 0;
    }

    // par[31:12] give upper bits of pa/page number, low bits have attrs, low bits of va are the offset
    *result = (par & ~0xfff) | (va & 0xfff);
    return 1;

}

// map <va>-><pa> at TLB index <idx> with attributes <e>
void pin_mmu_sec(unsigned idx,  
                uint32_t va, 
                uint32_t pa,
                pin_t e) {


    demand(idx < 8, lockdown index too large);
    // lower 20 bits should be 0.
    demand(bits_get(va, 0, 19) == 0, only handling 1MB sections); // 1mb region so pa/va should be aligned to 1mb
    demand(bits_get(pa, 0, 19) == 0, only handling 1MB sections);

    debug("about to map %x->%x\n", va,pa);

    // these will hold the values you assign for the tlb entries.
    uint32_t va_ent = 0;
    uint32_t pa_ent = 0;
    uint32_t attr   = 0;
    
    //va register
    va_ent = bits_set(va_ent, 12, 31, bits_get(va, 12, 31)); // va tag/section 
    va_ent = bits_set(va_ent, 9,  9,  e.G ? 1 : 0); // global bit
    va_ent = bits_set(va_ent, 0,  7,  e.G ? 0 : e.asid); // asid
    
    //attributes register in 3-151
    uint32_t mem = e.mem_attr;
    uint32_t tex = (mem >> 2) & 0b111;
    uint32_t c   = (mem >> 1) & 0b1;
    uint32_t b   = (mem >> 0) & 0b1;
    
    // write to lockdown attributes register
    attr = bits_set(attr, 7, 10, e.dom);
    attr = bits_set(attr, 1, 5, e.mem_attr);
    //lockdown_attr_set_raw(attr);
    

    // pa register
    pa_ent = bits_set(pa_ent, 12, 31, bits_get(pa, 12, 31));
    pa_ent = bits_set(pa_ent, 6,  7,  e.pagesize);
    pa_ent = bits_set(pa_ent, 1,  3,  e.AP_perm); // access permissions
    pa_ent = bits_set(pa_ent, 0,  0,  1); // valid bit for tlb entry

    lockdown_index_set_raw(idx); // idx to write to in lockdown
    lockdown_va_set_raw(va_ent);
    lockdown_attr_set_raw(attr);
    lockdown_pa_set_raw(pa_ent); 
    dsb();         
    prefetch_flush();    


#if 0
    if((x = lockdown_va_get()) != va_ent)
        panic("lockdown va: expected %x, have %x\n", va_ent,x);
    if((x = lockdown_pa_get()) != pa_ent)
        panic("lockdown pa: expected %x, have %x\n", pa_ent,x);
    if((x = lockdown_attr_get()) != attr)
        panic("lockdown attr: expected %x, have %x\n", attr,x);
#endif
}

// check that <va> is pinned.  
int pin_exists(uint32_t va, int verbose_p) {
    if(!mmu_is_enabled())
        panic("XXX: i think we can only check existence w/ mmu enabled\n");

    uint32_t r;
    if(tlb_contains_va(&r, va)) {
        assert(va == r);
        return 1;
    } else {
        if(verbose_p) 
            output("TLB should have %x: returned %x [reason=%b]\n", 
                va, r, bits_get(r,1,6));
        return 0;
    }
}

// look in test <1-test-basic.c> to see what to do.
// need to set the <asid> before turning VM on and 
// to switch processes.
void pin_set_context(uint32_t asid) {
    // put these back
    demand(asid > 0 && asid < 64, "invalid asid");
    demand(null_pt, "must setup null_pt --- look at tests");
    // valid asid and null pt exists
    // staff_mmu_set_ctx requires pid==0 or pid>64 (see staff_set_procid_ttbr0)
    enum { PID = 128 };
    staff_mmu_set_ctx(PID, asid, null_pt); // every translation faults unless pinned TLB matches
    dsb();
    prefetch_flush();
}

void pin_clear(unsigned idx)  {
    // set all three registers at TLB idx to 0
    demand(idx < 8, "invalid lockdown idx=%u", idx);
    lockdown_index_set_raw(idx);
    lockdown_va_set_raw(0);
    lockdown_attr_set_raw(0);
    lockdown_pa_set_raw(0);
    dsb();
    prefetch_flush();
}

void lockdown_print_entry(unsigned idx) {
    trace_nofn("   idx=%d\n", idx);

    lockdown_index_set_raw(idx);

    uint32_t va_ent = lockdown_va_get();
    uint32_t pa_ent = lockdown_pa_get();
    uint32_t attr   = lockdown_attr_get();

    unsigned v = bit_get(pa_ent, 0);
    if(!v) {
        trace_nofn("     \n");
        return;
    }
    unsigned G    = bit_get(va_ent, 9);
    unsigned asid = bits_get(va_ent, 0, 7);
    // display section number (divide by 1MB per README)
    uint32_t va   = bits_get(va_ent, 12, 31);

    trace_nofn("     va_ent=%x: va=%x|G=%d|ASID=%d\n",
        va_ent, va, G, asid);
    
    uint32_t pa   = bits_get(pa_ent, 12, 31);
    unsigned nsa  = bit_get(pa_ent, 10);
    unsigned nstid= bit_get(pa_ent, 9);
    unsigned size = bits_get(pa_ent, 6, 7);
    unsigned apx  = bits_get(pa_ent, 1, 3);

    trace_nofn("     pa_ent=%x: pa=%x|nsa=%d|nstid=%d|size=%b|apx=%b|v=%d\n",
        pa_ent, pa, nsa, nstid, size, apx, v);

    unsigned dom = bits_get(attr, 7, 10);
    unsigned xn  = bit_get(attr, 6);
    unsigned tex = bits_get(attr, 3, 5);
    unsigned C   = bit_get(attr, 1);
    unsigned B   = bit_get(attr, 0);

    trace_nofn("     attr=%x: dom=%d|xn=%d|tex=%b|C=%d|B=%d\n",
        attr, dom, xn, tex, C, B);

}
void lockdown_print_entries(const char *msg) {
    trace_nofn("-----  <%s> ----- \n", msg);
    trace_nofn("  pinned TLB lockdown entries:\n");

    for(int i = 0; i < 8; i++)
        lockdown_print_entry(i);
    trace_nofn("----- ---------------------------------- \n");
}

