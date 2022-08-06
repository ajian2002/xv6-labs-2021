#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64 sys_exit(void)
{
    int n;
    if (argint(0, &n) < 0) return -1;
    exit(n);
    return 0;  // not reached
}

uint64 sys_getpid(void) { return myproc()->pid; }

uint64 sys_fork(void) { return fork(); }

uint64 sys_wait(void)
{
    uint64 p;
    if (argaddr(0, &p) < 0) return -1;
    return wait(p);
}

uint64 sys_sbrk(void)
{
    int addr;
    int n;

    if (argint(0, &n) < 0) return -1;

    addr = myproc()->sz;
    if (growproc(n) < 0) return -1;
    return addr;
}

uint64 sys_sleep(void)
{
    int n;
    uint ticks0;

    if (argint(0, &n) < 0) return -1;
    acquire(&tickslock);
    ticks0 = ticks;
    while (ticks - ticks0 < n)
    {
        if (myproc()->killed)
        {
            release(&tickslock);
            return -1;
        }
        sleep(&ticks, &tickslock);
    }
    release(&tickslock);
    return 0;
}

#ifdef LAB_PGTBL
int sys_pgaccess(void)
{
    vmprint(myproc()->pagetable);
    int n;
    uint64 addr1, maskArg, masks;
    masks=0;
    argaddr(0, &addr1);
    argint(1, &n);
    argaddr(2, &maskArg);
    if(n>40)return -1;
    char*buf=(char*)addr1;
    for (int i = 0; i < n; i++)
    {
        uint64 pa = walkaddr(myproc()->pagetable,buf[i*PGSIZE]);
        if (0 == pa)
        {
            return -1;
        }
        pte_t *realPte = (pte_t *)PA2PTE(pa);
        if (*realPte & PTE_V&& * realPte & PTE_A)
        {
            masks |= (1 << i);
            (*realPte) = (*realPte)^(1 << i);
        }
    }
    if (0 == copyout(myproc()->pagetable,(uint64) &maskArg, (char *)&masks, sizeof(uint64)))
    {
        return 0;
    }
    return -1;
}
#endif

uint64 sys_kill(void)
{
    int pid;

    if (argint(0, &pid) < 0) return -1;
    return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void)
{
    uint xticks;

    acquire(&tickslock);
    xticks = ticks;
    release(&tickslock);
    return xticks;
}
