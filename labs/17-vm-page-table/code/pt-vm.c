#include "rpi.h"
#include "pt-vm.h"
#include "helper-macros.h"
#include "procmap.h"

// turn this off if you don't want all the debug output.
enum { verbose_p = 1 };
enum { OneMB = 1024*1024 };

vm_pt_t *vm_pt_alloc(unsigned n) {
    demand(n == 4096, we only handling a fully-populated page table right now);
    unsigned nbytes = n * sizeof(vm_pt_t);

    // trivial:
    // allocate pt with n entries [should look just like you did 
    // for pinned vm]
    vm_pt_t *pt = kmalloc_aligned(nbytes, 1 << 14); // level 1 page table is 14-bit aligned (16kb)
    demand(pt, "kmalloc_aligned failed for page table");
    demand(is_aligned_ptr(pt, 1<<14), must be 14-bit aligned!);
    memset(pt, 0, nbytes);
    return pt;
}

// allocate new page table and copy pt.  not the
// best interface since it will copy private mappings.
vm_pt_t *vm_dup(vm_pt_t *pt1) {
    vm_pt_t *pt2 = vm_pt_alloc(PT_LEVEL1_N);
    memcpy(pt2,pt1,PT_LEVEL1_N * sizeof *pt1);
    return pt2;
}

// same as pinned version: 
//  - probably should check that the page table 
//    is set, and asid makes sense.
void vm_mmu_enable(void) {
    assert(!mmu_is_enabled());
    mmu_enable();
    assert(mmu_is_enabled());
}

// same as pinned 
void vm_mmu_disable(void) {
    assert(mmu_is_enabled());
    mmu_disable();
    assert(!mmu_is_enabled());
}

// - set <pt,pid,asid> for an address space.
// - must be done before you switch into it!
// - mmu can be off or on.
void vm_mmu_switch(vm_pt_t *pt, uint32_t pid, uint32_t asid) {
    assert(pt);
    mmu_set_ctx(pid, asid, pt);
}

// just like pinned.
void vm_mmu_init(uint32_t domain_reg) {
    // initialize everything, after bootup.
    mmu_init();
    domain_access_ctrl_set(domain_reg);
}

// map the 1mb section starting at <va> to <pa>
// with memory attribute <attr>.
vm_pte_t *
vm_map_sec(vm_pt_t *pt, uint32_t va, uint32_t pa, pin_t attr) 
{
    assert(aligned(va, OneMB));
    assert(aligned(pa, OneMB));

    // today we just use 1mb.
    assert(attr.pagesize == PAGE_1MB);

    unsigned index = va >> 20; // gets l1 index
    assert(index < PT_LEVEL1_N);

    // 1. lookup the <pte> in <pt> using index (l1 index)
    vm_pte_t *pte = &pt[index]; // get the pte for the l1 index
    memset(pte, 0, sizeof *pte); // clear the pte

    // 2. use values in <attr> to set pte's:
    //  - nG
    //  - B
    //  - C
    //  - TEX
    //  - AP
    //  - domain
    //  - XN
    // also: <tag> since its a 1mb section.
    uint32_t mem = attr.mem_attr;
    uint32_t tex = (mem >> 2) & 0b111;
    uint32_t c   = (mem >> 1) & 0b1;
    uint32_t b   = (mem >> 0) & 0b1;

    pte->tag = 0b10;              // first-level section descriptor
    pte->B   = b;
    pte->C   = c;
    pte->XN  = 0; 
    pte->domain = attr.dom;
    pte->IMP = 0;
    pte->AP  = attr.AP_perm & 0b11;
    pte->TEX = tex;
    pte->APX = (attr.AP_perm >> 2) & 0b1;
    pte->S   = 0;
    pte->nG  = attr.G ? 0 : 1;       // descriptor stores nG (not global)
    pte->super = 0;
    pte->_sbz1 = 0;

    // 3. set <pte->sec_base_addr> to the right physical section.
    pte->sec_base_addr = pa >> 20; // gets the 12-bit section number
    mmu_sync_pte_mods(); // mmu sees updated table

    // 4. you modified the page table!  
    //   - make sure you call your sync PTE (lab 15)
    if(verbose_p)
        vm_pte_print(pt,pte);
    assert(pte);
    return pte;
}

// lookup 32-bit address va in pt and return the pte
// if it exists, return 0 otherwise (use tag)
vm_pte_t * vm_lookup(vm_pt_t *pt, uint32_t va) {
    assert(pt);
    unsigned idx = va >> 20; // gets l1 index
    assert(idx < PT_LEVEL1_N); // check if index is within 4096 entries
    vm_pte_t *pte = &pt[idx];
    if(pte->tag != 0b10) // section descriptor
        return 0;
    return pte;
}

// manually translate <va> in page table <pt>
// - if doesn't exist, return 0.
// - if does exist:
//    1. write the translated physical address to <*pa> 
//    2. return the pte pointer.
//
// NOTE: 
//   - we can't just return the <pa> b/c page 0 could be mapped.
//   - the common unix kernel hack of returning (void*)-1 leads
//     to really really nasty bugs.  so we don't.
vm_pte_t *vm_xlate(uint32_t *pa, vm_pt_t *pt, uint32_t va) {
    assert(pa);
    assert(pt);
    vm_pte_t *pte = vm_lookup(pt, va);

    if(!pte)
        return 0;
    
    *pa = (pte->sec_base_addr << 20) | (va & (OneMB - 1));
    return pte;
}

// compute the default attribute for each type.
static inline pin_t attr_mk(pr_ent_t *e) {
    switch(e->type) {
    case MEM_DEVICE: 
        return pin_mk_device(e->dom);
    // kernel: currently everything is uncached.
    case MEM_RW:
        return pin_mk_global(e->dom, perm_rw_priv, MEM_uncached);
   case MEM_RO: 
        panic("not handling\n");
   default: 
        panic("unknown type: %d\n", e->type);
    }
}

// setup the initial kernel mapping.  This will mirror
//    static inline void procmap_pin_on(procmap_t *p) 
// in <13-pinned-vm/code/procmap.h>  but will call
// your vm_ routines, not pinned routines.
//
// if <enable_p>=1, should enable the MMU.  make sure
// you setup the page table and asid. use  
// kern_asid, and kern_pid.
vm_pt_t *vm_map_kernel(procmap_t *p, int enable_p) {
    // return staff_vm_map_kernel(p,enable_p);

    // install at least the default handlers so we get 
    // error messages.
    full_except_install(0);

    // the asid and pid we start with.  
    //    shouldn't matter since kernel is global.
    enum { kern_asid = 1, kern_pid = 0x140e };

    // 1. compute domains being used.
    uint32_t d = dom_perm(p->dom_ids, DOM_client);
    // 2. call <vm_mmu_init> to set domain reg (using 1) and init MMU.
    vm_mmu_init(d);
    // 3. allocate a page table <vm_pt_alloc>
    vm_pt_t *pt = vm_pt_alloc(PT_LEVEL1_N);
    // 4. walk through procmap, mapping each entry <vm_map_sec>
    //    - note: for today we use identity map, so <addr> --> <addr>
    for(unsigned i = 0; i < p->n; i++) {
        pr_ent_t *e = &p->map[i];
        pin_t attr = attr_mk(e);

        assert(e->nbytes == OneMB); // procmap entry into a pin_t
        vm_map_sec(pt, e->addr, e->addr, attr);

        uint32_t pa;
        demand(vm_xlate(&pa, pt, e->addr), "missing translation for %x", e->addr);
        demand(pa == e->addr, "bad identity map: va=%x pa=%x", e->addr, pa);
    }
    // 5. use <vm_mmu_switch> to setup <kern_asid>, <pt>, and <kern_pid>
    vm_mmu_switch(pt, kern_pid, kern_asid);
    // 6. if <enable_p>=1
    //    - <mmu_sync_pte_mods> since modified page table.
    //    - <vm_mmu_enable> to turn onr
    if(enable_p) {
        mmu_sync_pte_mods();
        vm_mmu_enable();
    }

    // return page table.
    assert(pt);
    return pt;
}
