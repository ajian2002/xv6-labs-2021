#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "fcntl.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"

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
uint64 sys_mmap(void)
{
    // void *addr, size_t length, int prot, int flags, int fd, off_t offset
    uint64 addr;
    int length, prot, flags, fd, offset;
    argaddr(0, &addr);
    //可以假设 addr总是为零
    argint(1, &length);
    // length是要映射的字节数；它可能与文件的长度不同。
    argint(2, &prot);
    //指示内存是否应该被映射为可读、可写和/或可执行；您可以假设prot是PROT_READ或PROT_WRITE
    //或两者兼而有之
    argint(3, &flags);
    // 标志将是MAP_SHARED，这意味着对映射内存的修改应该写回文件，或MAP_PRIVATE，这意味着它们不应该
    argint(4, &fd);
    argint(5, &offset);
    //假设偏移量为零（它是文件中映射的起点）
    // printf("mmap::1:%p,2:%d,3:%d,4:%d,5:%d\n", addr, length, prot, flags, fd, offset);
    // pagetable_t pt = myproc()->pagetable;  // uint64*
    addr = myproc()->sz;
    struct file *file = myproc()->ofile[fd];
    if (prot & PROT_READ)
    {
        if (file->readable != 1)
        {
            return -1;
        }
    }
    if (prot & PROT_WRITE && flags & MAP_SHARED)
    {
        if (file->writable != 1)
        {
            return -1;
        }
    }
    myproc()->sz += length;

    // struct VMA
    // {
    //     uint64 used;
    //     uint64 v_start;
    //     uint64 v_end;
    //     uint64 private;
    //     uint64 fd;
    //     uint64 offset;
    // };

    // fcntl(fd, prot);
    struct VMA *vma;
    int i = 0;
    for (i = 0; i < 16; i++)
    {
        vma = myproc()->vmas[i];
        if (vma->used == 0) break;
    }
    vma->used = 1;
    vma->v_start = addr;
    vma->v_end = addr + length;
    vma->prot = prot;
    vma->flags = flags;
    vma->fd = fd;
    vma->offset = offset;
    vma->file = file;
    filedup(file);  // count ++
    printf("mmap[%d][%p --------------------%p]\n", i, vma->v_start, vma->v_end);
    return addr;
}

uint64 realy_munmap(uint64 addr, int length)
{
    if (length <= 0) return length;

    uint64 udval = PGROUNDDOWN(addr);
    uint64 uuval = PGROUNDUP(addr + length);
    struct proc *p = myproc();
    struct VMA *vma = 0;
    for (uint64 ud = udval; ud < uuval; ud += PGSIZE)
    {
        vma = 0;
        int i = 0;
        for (i = 0; i < 16; i++)
        {
            if (myproc()->vmas[i]->used == 1)
                if (myproc()->vmas[i]->v_start <= ud && myproc()->vmas[i]->v_end > ud)
                {
                    vma = myproc()->vmas[i];
                    break;
                }
        }
        if (vma == 0) continue;
        printf("unvma[%d][%p --%p]\n", i, vma->v_start, vma->v_end);
        if (vma->flags & MAP_SHARED)
        {
            // struct inode *ip = vma->file->ip;
            // acquiresleep(&ip->lock);
            // writei(ip, 1, ud, vma->offset + ud - vma->v_start, PGSIZE);
            filewrite(vma->file, ud, PGSIZE);
            // releasesleep(&ip->lock);
        }
        pte_t *pte;
        if ((pte = walk(p->pagetable, ud, 0)) == 0) return -1;
        if ((*pte & PTE_V) == 0) continue;
        uvmunmap(p->pagetable, ud, 1, 1);
    }
    if (vma->v_start == udval && vma->v_end == uuval)
    {
        fileclose(vma->file);
        vma->used = 0;
    }

    return 0;
}
uint64 sys_munmap(void)
{
    //    int munmap(void* addr, int length);
    //    如果进程修改了内存并映射了MAP_SHARED，则应首先将修改写入文件。一个munmap调用可能只覆盖
    //    mmap-ed
    //    区域的一部分，但您可以假设它会在开始、结束或整个区域取消映射（但不会在区域中间打孔） .
    uint64 addr;
    int length;
    argaddr(0, &addr);
    argint(1, &length);
    return realy_munmap(addr, length);
}