// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"
#define NBLUCKS 13
#define NLEN 5
struct
{
    struct spinlock lock;
    struct buf buf[NLEN];

    // Linked list of all buffers, through prev/next.
    // Sorted by how recently the buffer was used.
    // head.next is most recent, head.prev is least.
    struct buf head;
} bcache[NBLUCKS];
struct spinlock bcachelock;
int hashcode(uint dev, uint blockno) { return (blockno % NBLUCKS); }
void binit(void)
{
    struct buf *b, *head;
    initlock(&bcachelock, "all");
    for (int i = 0; i < NBLUCKS; i++)
    {
        char name[8] = {'b', 'c', 'a', 'c', 'h', 'e', '0' + i / 10, '0' + i % 10};
        initlock(&bcache[i].lock, name);
        // Create linked list of buffers
        bcache[i].head.prev = &bcache[i].head;
        bcache[i].head.next = &bcache[i].head;

        head = &bcache[i].head;
        head->next = head->prev = head;

        for (int j = 0; j < NLEN; j++)
        {
            b = &bcache[i].buf[j];
            char name[11] = {'b',
                             'u',
                             'f',
                             'f',
                             'e',
                             'r',
                             ('0' + i / 10),
                             ('0' + i % 10),
                             ' ',
                             ('0' + j / 10),
                             ('0' + j % 10)};
            initsleeplock(&b->lock, name);
            b->next = head->next;
            b->prev = head;
            head->next->prev = b;
            head->next = b;
        }
    }
    // printf("init all\n");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *bget(uint dev, uint blockno)
{
    // acquire(&bcachelock);
    struct buf *b;
    int index = hashcode(dev, blockno);
    acquire(&bcache[index].lock);

    // Is the block already cached?
    for (b = bcache[index].head.next; b != &bcache[index].head; b = b->next)
    {
        if (b->dev == dev && b->blockno == blockno)
        {
            b->refcnt++;
            release(&bcache[index].lock);
            acquiresleep(&b->lock);
            // release(&bcachelock);
            return b;
        }
    }

    // Not cached.
    // Recycle the least recently used (LRU) unused buffer.
    for (b = bcache[index].head.prev; b != &bcache[index].head; b = b->prev)
    {
        if (b->refcnt == 0)
        {
            b->dev = dev;
            b->blockno = blockno;
            b->valid = 0;
            b->refcnt = 1;

            b->prev->next = b->next;
            b->next->prev = b->prev;

            b->next = bcache[index].head.next;
            b->prev = &bcache[index].head;
            bcache[index].head.next->prev = b;
            bcache[index].head.next = b;
            release(&bcache[index].lock);
            acquiresleep(&b->lock);
            // release(&bcachelock);
            return b;
        }
    }
    release(&bcache[index].lock);
    acquire(&bcachelock);

    acquire(&bcache[index].lock);

    // Is the block already cached?
    for (b = bcache[index].head.next; b != &bcache[index].head; b = b->next)
    {
        if (b->dev == dev && b->blockno == blockno)
        {
            b->refcnt++;
            release(&bcache[index].lock);
            release(&bcachelock);
            acquiresleep(&b->lock);
            // release(&bcachelock);
            return b;
        }
    }

    // Not cached.
    // Recycle the least recently used (LRU) unused buffer.
    for (b = bcache[index].head.prev; b != &bcache[index].head; b = b->prev)
    {
        if (b->refcnt == 0)
        {
            b->dev = dev;
            b->blockno = blockno;
            b->valid = 0;
            b->refcnt = 1;

            b->prev->next = b->next;
            b->next->prev = b->prev;

            b->next = bcache[index].head.next;
            b->prev = &bcache[index].head;
            bcache[index].head.next->prev = b;
            bcache[index].head.next = b;
            release(&bcache[index].lock);
            release(&bcachelock);
            acquiresleep(&b->lock);
            // release(&bcachelock);
            return b;
        }
    }
    // release(&bcache[index].lock);
    for (int i = 0; i < NBLUCKS; i++)
    {
        if (i == index) continue;
        acquire(&bcache[i].lock);
        for (b = bcache[i].head.prev; b != &bcache[i].head; b = b->prev)
        {
            if (b->refcnt == 0)
            {
                b->dev = dev;
                b->blockno = blockno;
                b->valid = 0;
                b->refcnt = 1;

                b->prev->next = b->next;
                b->next->prev = b->prev;

                // acquire(&bcache[index].lock);
                b->next = bcache[index].head.next;
                b->prev = &bcache[index].head;
                bcache[index].head.next->prev = b;
                bcache[index].head.next = b;
                // release(&bcache[index].lock);

                release(&bcache[i].lock);
                release(&bcache[index].lock);
                release(&bcachelock);
                acquiresleep(&b->lock);
                return b;
            }
            // break;
        }
        release(&bcache[i].lock);
    }
    release(&bcache[index].lock);
    release(&bcachelock);
    panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf *bread(uint dev, uint blockno)
{
    struct buf *b;

    b = bget(dev, blockno);
    if (!b->valid)
    {
        virtio_disk_rw(b, 0);
        b->valid = 1;
    }
    return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b)
{
    if (!holdingsleep(&b->lock)) panic("bwrite");
    virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b)
{
    if (!holdingsleep(&b->lock)) panic("brelse");

    int index = hashcode(b->dev, b->blockno);
    acquire(&bcache[index].lock);
    b->refcnt--;
    if (b->refcnt == 0)
    {
        // no one is waiting for it.
        b->next->prev = b->prev;
        b->prev->next = b->next;

        b->next = &bcache[index].head;
        b->prev = bcache[index].head.prev;
        bcache[index].head.prev->next = b;
        bcache[index].head.prev = b;
    }
    release(&bcache[index].lock);
    releasesleep(&b->lock);
}

void bpin(struct buf *b)
{
    int index = hashcode(b->dev, b->blockno);
    acquire(&bcache[index].lock);
    b->refcnt++;
    release(&bcache[index].lock);
}

void bunpin(struct buf *b)
{
    int index = hashcode(b->dev, b->blockno);
    acquire(&bcache[index].lock);
    b->refcnt--;
    release(&bcache[index].lock);
}
