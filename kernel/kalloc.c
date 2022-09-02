// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[];  // first address after kernel.
                    // defined by kernel.ld.
extern int pgcount[PGCOUNT];
struct run
{
    struct run *next;
};

struct
{
    struct spinlock lock;
    struct run *freelist;
} kmem;

void kinit()
{
    initlock(&kmem.lock, "kmem");
    freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end)
{
    char *p;
    p = (char *)PGROUNDUP((uint64)pa_start);
    for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
    {
        pgcount[(uint64)p / PGSIZE] = 0;
        kfree(p);
    }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
    struct run *r;

    if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP) panic("kfree");

    if (pgcount[(uint64)pa / PGSIZE] > 1)
    {
        --pgcount[(uint64)pa / PGSIZE];
        return;
    }
    pgcount[(uint64)pa / PGSIZE] = 0;

    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run *)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *kalloc(void)
{
    struct run *r;

    acquire(&kmem.lock);
    r = kmem.freelist;
    if (r) kmem.freelist = r->next;
    release(&kmem.lock);
    if (r)
    {
        memset((char *)r, 5, PGSIZE);  // fill with junk
        pgcount[(uint64)r / PGSIZE] = 1;
    }
    return (void *)r;
}
int cow_alloc(pagetable_t pagetable, uint64 va)
{
    va = PGROUNDDOWN(va);
    if (va >= MAXVA) return -1;
    pte_t *pte = walk(pagetable, va, 0);
    if (pte == 0) return -1;
    uint64 pa = walkaddr(pagetable, va);
    if (pa == 0||pa<(uint64)end) return -1;
    uint flags = PTE_FLAGS(*pte);
    if (!(flags & PTE_V)) return -1;

    if (flags & PTE_W) return 0;  // w-->1 一般情况
    // w -->0
    if (!(flags & PTE_COW)) return -1;
    // cow--->1
    if (pgcount[pa / PGSIZE] == 1)
    {
        *pte |= PTE_W;
        *pte &= ~PTE_COW;
    }
    else
    {
        uint64 ka = (uint64)kalloc();
        if (ka == 0)
        {
            // panic("kalloc() error");
            return -2;
        }
        memmove((void *)ka, (void *)pa, PGSIZE);
        int newflags = (flags | PTE_W) & ~PTE_COW;  // cow1w0-->cow0w1
        uvmunmap(pagetable, va, 1, 1);
        if (mappages(pagetable, va, PGSIZE, ka, newflags) != 0)
        {
            kfree((void *)ka);
            // panic("mappages error");
            return -1;
        }
    }
    return 0;
}