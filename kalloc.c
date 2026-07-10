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

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

#define MAX_PHYS_PAGES (PHYSTOP / PGSIZE)
int refcount[MAX_PHYS_PAGES];
struct spinlock refcount_lock;

int
get_page_index(uint64 pa)
{
    return pa / PGSIZE;
}

void
kinit_refcount(void)
{
    for (int i = 0; i < MAX_PHYS_PAGES; i++) {
        refcount[i] = 0;
    }
    initlock(&refcount_lock, "refcount");
}

void
incref(uint64 pa)
{
    int idx = get_page_index(pa);
    acquire(&refcount_lock);
    refcount[idx]++;
    release(&refcount_lock);
}

int
decref(uint64 pa)
{
    int idx = get_page_index(pa);
    int should_free = 0;
    
    acquire(&refcount_lock);
    if (refcount[idx] > 0) {
        refcount[idx]--;
        if (refcount[idx] == 0) {
            should_free = 1;
        }
    } else {
        should_free = 1;
    }
    release(&refcount_lock);
    
    return should_free;
}

int
get_refcount(uint64 pa)
{
    int idx = get_page_index(pa);
    int cnt;
    acquire(&refcount_lock);
    cnt = refcount[idx];
    release(&refcount_lock);
    return cnt;
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  kinit_refcount();
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  uint64 p = (uint64)pa;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  if (decref(p) == 0) {
    return;
  }

  r = (struct run*)pa;
  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  /*if(r) {
    uint64 pa = (uint64)r;
    int idx = get_page_index(pa);
    acquire(&refcount_lock);
    refcount[idx] = 1;
    release(&refcount_lock);
  }*/
  if(r) {
    uint64 pa = (uint64)r;
    int idx = get_page_index(pa);
    acquire(&refcount_lock);
    refcount[idx] = 1;
    //printf("kalloc: allocated pa=%p idx=%d refcount=%d\n", pa, idx, refcount[idx]);
    release(&refcount_lock);
  } else {
    printf("kalloc: WARNING - freelist is EMPTY!\n");
  }

  return (void*)r;
}

int
get_free_pages(void)
{
    struct run *r;
    int count = 0;
    
    acquire(&kmem.lock);
    r = kmem.freelist;
    while(r) {
        count++;
        r = r->next;
    }
    release(&kmem.lock);
    
    return count;
}
