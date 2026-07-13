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

struct kmem {
  struct spinlock lock;
  struct run *freelist;
} kmem_cpu[NCPU];

void
kinit()
{
  for (int i = 0; i < NCPU; i++) {
    char name[8];
    snprintf(name, sizeof(name), "kmem_%d", i);
    initlock(&kmem_cpu[i].lock, name);
    kmem_cpu[i].freelist = 0;
  }

  freerange(end, (void*)PHYSTOP);
}

void
kfree_to_cpu(void *pa, int cpu)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree_to_cpu");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem_cpu[cpu].lock);
  r->next = kmem_cpu[cpu].freelist;
  kmem_cpu[cpu].freelist = r;
  release(&kmem_cpu[cpu].lock);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree_to_cpu(p, 0);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  int cpu;
  
  push_off();
  cpu = cpuid();
  pop_off();
  
  kfree_to_cpu(pa, cpu);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  int cpu;

  push_off();
  cpu = cpuid();
  pop_off();
  
  acquire(&kmem_cpu[cpu].lock);
  r = kmem_cpu[cpu].freelist;
  if(r) {
    kmem_cpu[cpu].freelist = r->next;
    release(&kmem_cpu[cpu].lock);
    if(r)
      memset((char*)r, 5, PGSIZE); // fill with junk
    return (void*)r;
  }
  release(&kmem_cpu[cpu].lock);
  
  for (int i = 0; i < NCPU; i++) {
    if (i == cpu) continue;
    
    acquire(&kmem_cpu[i].lock);
    r = kmem_cpu[i].freelist;
    if (r) {
      kmem_cpu[i].freelist = r->next;
      release(&kmem_cpu[i].lock);

      acquire(&kmem_cpu[cpu].lock);
      r->next = kmem_cpu[cpu].freelist;
      kmem_cpu[cpu].freelist = r;
      release(&kmem_cpu[cpu].lock);

      acquire(&kmem_cpu[cpu].lock);
      r = kmem_cpu[cpu].freelist;
      if (r) {
        kmem_cpu[cpu].freelist = r->next;
      }
      release(&kmem_cpu[cpu].lock);
      
      if(r)
        memset((char*)r, 5, PGSIZE); 
      return (void*)r;
    }
    release(&kmem_cpu[i].lock);
  }
  
  return 0;
}
