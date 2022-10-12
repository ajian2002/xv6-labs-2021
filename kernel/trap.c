#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "fcntl.h"
#include "defs.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"

char *getReasion(int cause)
{
    if (cause == 12)
    {
        return "x";
    }
    if (cause == 13)
    {
        return "r";
    }
    if (cause == 15)
    {
        return "w";
    }
    return "?";
    // 12c表示是因为指令执行引起的page fault。
    // 13d表示是因为load引起的page fault；
    // 15f表示是因为store引起的page fault；
    // 所以第二个信息存在SCAUSE寄存器中，其中总共有3个类型的原因与page
    // fault相关，分别是读、写和指令.
}

int reallymapped(int val)
{
    struct proc *p = myproc();
    uint64 vdval = PGROUNDDOWN(val);
    uint64 vuval = PGROUNDUP(val);

    struct VMA *vma = 0;
    for (int i = 0; i < 16; i++)
    {
        if (myproc()->vmas[i]->used == 1)
        {
            printf("vma[%d]-->[%p---%p]\n", i, myproc()->vmas[i]->v_start,
                   myproc()->vmas[i]->v_end);
            if (myproc()->vmas[i]->v_start <= vdval && myproc()->vmas[i]->v_end > vdval)
            {
                vma = myproc()->vmas[i];
                break;
            }
        }
    }
    if (vma == 0)
    {
        printf("mmap:::not found %p\n", vdval);
        return -1;
    }

    uint64 len = PGSIZE;
    if (vuval > vma->v_end)
    {
        len = vma->v_end - vdval;
    }

    int flags = PTE_V | PTE_U;
    if (vma->prot & PROT_READ)
    {
        flags |= PTE_R;
    }
    if (vma->prot & PROT_WRITE)
    {
        flags |= PTE_W;
    }

    char *pa = kalloc();
    memset(pa, 0, PGSIZE);

    struct inode *ip = vma->file->ip;
    acquiresleep(&ip->lock);
    // int tot =
    readi(vma->file->ip, 0, (uint64)pa, vma->offset + vdval - vma->v_start, PGSIZE);
    releasesleep(&ip->lock);
    // printf("read=%d\n", tot);

    if (0 != mappages(p->pagetable, vdval, len, (uint64)pa, flags))
    {
        kfree(pa);
        printf("mmap::error;\n");
        return -1;
    }
    return 0;
}
struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void trapinit(void) { initlock(&tickslock, "time"); }

// set up to take exceptions and traps while in the kernel.
void trapinithart(void) { w_stvec((uint64)kernelvec); }

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void usertrap(void)
{
    int which_dev = 0;

    if ((r_sstatus() & SSTATUS_SPP) != 0) panic("usertrap: not from user mode");

    // send interrupts and exceptions to kerneltrap(),
    // since we're now in the kernel.
    w_stvec((uint64)kernelvec);

    struct proc *p = myproc();

    // save user program counter.
    p->trapframe->epc = r_sepc();
    int cause = r_scause();
    if (cause == 8)
    {
        // system call

        if (p->killed) exit(-1);

        // sepc points to the ecall instruction,
        // but we want to return to the next instruction.
        p->trapframe->epc += 4;

        // an interrupt will change sstatus &c registers,
        // so don't enable until done with those registers.
        intr_on();

        syscall();
    }
    else if ((which_dev = devintr()) != 0)
    {
        // ok
    }
    else if (cause == 12 || cause == 13 || cause == 15)
    {
        //比如，
        // 12c表示是因为指令执行引起的page fault。
        // 13d表示是因为load引起的page fault；
        // 15f表示是因为store引起的page fault；
        // 所以第二个信息存在SCAUSE寄存器中，其中总共有3个类型的原因与page
        // fault相关，分别是读、写和指令.
        uint64 val = r_stval();
        // uint64 pc = r_sepc();
        // printf("reasion=%s pid=%d\n", getReasion(cause), p->pid);
        // printf("sepc=%p stval=%p\n", pc, val);
        if (0 != reallymapped(val))
        {
            p->killed = 1;
        }
    }
    else
    {
        printf("            usertrap(): unexpected scause %s pid=%d\n", getReasion(cause), p->pid);
        printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
        p->killed = 1;
    }

    if (p->killed) exit(-1);

    // give up the CPU if this is a timer interrupt.
    if (which_dev == 2) yield();

    usertrapret();
}

//
// return to user space
//
void usertrapret(void)
{
    struct proc *p = myproc();

    // we're about to switch the destination of traps from
    // kerneltrap() to usertrap(), so turn off interrupts until
    // we're back in user space, where usertrap() is correct.
    intr_off();

    // send syscalls, interrupts, and exceptions to trampoline.S
    w_stvec(TRAMPOLINE + (uservec - trampoline));

    // set up trapframe values that uservec will need when
    // the process next re-enters the kernel.
    p->trapframe->kernel_satp = r_satp();          // kernel page table
    p->trapframe->kernel_sp = p->kstack + PGSIZE;  // process's kernel stack
    p->trapframe->kernel_trap = (uint64)usertrap;
    p->trapframe->kernel_hartid = r_tp();  // hartid for cpuid()

    // set up the registers that trampoline.S's sret will use
    // to get to user space.

    // set S Previous Privilege mode to User.
    unsigned long x = r_sstatus();
    x &= ~SSTATUS_SPP;  // clear SPP to 0 for user mode
    x |= SSTATUS_SPIE;  // enable interrupts in user mode
    w_sstatus(x);

    // set S Exception Program Counter to the saved user pc.
    w_sepc(p->trapframe->epc);

    // tell trampoline.S the user page table to switch to.
    uint64 satp = MAKE_SATP(p->pagetable);

    // jump to trampoline.S at the top of memory, which
    // switches to the user page table, restores user registers,
    // and switches to user mode with sret.
    uint64 fn = TRAMPOLINE + (userret - trampoline);
    ((void (*)(uint64, uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void kerneltrap()
{
    int which_dev = 0;
    uint64 sepc = r_sepc();
    uint64 sstatus = r_sstatus();
    uint64 scause = r_scause();

    if ((sstatus & SSTATUS_SPP) == 0) panic("kerneltrap: not from supervisor mode");
    if (intr_get() != 0) panic("kerneltrap: interrupts enabled");

    if ((which_dev = devintr()) == 0)
    {
        printf("scause %p\n", scause);
        printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
        panic("kerneltrap");
    }

    // give up the CPU if this is a timer interrupt.
    if (which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING) yield();

    // the yield() may have caused some traps to occur,
    // so restore trap registers for use by kernelvec.S's sepc instruction.
    w_sepc(sepc);
    w_sstatus(sstatus);
}

void clockintr()
{
    acquire(&tickslock);
    ticks++;
    wakeup(&ticks);
    release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int devintr()
{
    uint64 scause = r_scause();

    if ((scause & 0x8000000000000000L) && (scause & 0xff) == 9)
    {
        // this is a supervisor external interrupt, via PLIC.

        // irq indicates which device interrupted.
        int irq = plic_claim();

        if (irq == UART0_IRQ)
        {
            uartintr();
        }
        else if (irq == VIRTIO0_IRQ)
        {
            virtio_disk_intr();
        }
        else if (irq)
        {
            printf("unexpected interrupt irq=%d\n", irq);
        }

        // the PLIC allows each device to raise at most one
        // interrupt at a time; tell the PLIC the device is
        // now allowed to interrupt again.
        if (irq) plic_complete(irq);

        return 1;
    }
    else if (scause == 0x8000000000000001L)
    {
        // software interrupt from a machine-mode timer interrupt,
        // forwarded by timervec in kernelvec.S.

        if (cpuid() == 0)
        {
            clockintr();
        }

        // acknowledge the software interrupt by clearing
        // the SSIP bit in sip.
        w_sip(r_sip() & ~2);

        return 2;
    }
    else
    {
        return 0;
    }
}
