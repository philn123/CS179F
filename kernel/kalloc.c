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
static struct spinlock cowlock;
struct {
  struct spinlock lock;
  struct run *freelist;
  uint refCount[(PHYSTOP - KERNBASE ) >> PGSHIFT];
} kmem;
uint addr2index(void* r){
  return (uint)((PGROUNDUP((uint64)r)-KERNBASE) >> PGSHIFT);
}
void refCountInit(){
  acquire(&kmem.lock);
  for(uint i = 0; i < ((PHYSTOP - KERNBASE ) >> PGSHIFT); i++){
    //so that freerange will set them to 0... maybe
    kmem.refCount[i] = 1;
  }
  release(&kmem.lock);
}
void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&cowlock, "cowlock");
  refCountInit();
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

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  

  r = (struct run*)pa;

  acquire(&cowlock);
  if(kmem.refCount[addr2index(r)] > 1){
    //printf("decre index counter \n");
    kmem.refCount[addr2index(r)] -= 1;
  }
  else{
    kmem.refCount[addr2index(r)] -= 1;
    if(kmem.refCount[addr2index(r)] != 0){
      kmem.refCount[addr2index(r)] = 0;
      //printf("Error, reference counter is : %d\n", kmem.refCount[addr2index(r)]);
      //panic("negative ref counter in kfree");
    }
    memset(pa, 1, PGSIZE);
    r->next = kmem.freelist;
    kmem.freelist = r;
  }
  
  release(&cowlock);
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
  if(r){
    kmem.freelist = r->next;
    // add reference count whenever allocated
  }
  release(&kmem.lock);

  acquire(&cowlock);
  if(r){
    if(kmem.refCount[addr2index(r)] == 0){
      kmem.refCount[addr2index(r)] += 1;
    }
      
    else{
      printf("kalloc not an empty page?\n");
      kmem.refCount[addr2index(r)] += 1;
    }
  }
  release(&cowlock);
  
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

uint incref(void *pa){
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("incref out of bounds");
  uint count;
  struct run *r;
  r = (struct run*)pa;
  acquire(&cowlock);
  if(kmem.refCount[addr2index(r)] != 0){
     count = ++kmem.refCount[addr2index(r)];
  }
  else{
    panic("incref on ref with 0?");
  }
  
  release(&cowlock);
  return count;
}
uint decref(void *pa){
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("decref out of bounds");
  uint count;
  struct run *r;
  r = (struct run*) pa;
  acquire(&cowlock);
  if(kmem.refCount[addr2index(r)] != 0){
     count = --kmem.refCount[addr2index(r)];
  }
  else{
    panic("dec on ref with 0?");
  }
  release(&cowlock);
  if(count == 0){
    printf("ref 0, freeing\n");
    kfree((char*)PGROUNDUP((uint64)pa));
  }
  
  return count;
}
uint getref(void *pa){
  struct run *r;
  r = (struct run*) pa;
  acquire(&cowlock);
  int count = kmem.refCount[addr2index(r)];
  release(&cowlock);
  return count;
}