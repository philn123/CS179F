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
struct cow_info{
  struct list entry;
  uint refcount;
  void * pa;
};
typedef struct cow_info Cow_info;

static struct list cow_list;
static struct spinlock cow_lock;
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  char *p = (char *) PGROUNDUP((uint64) end);
  bd_init(p, (void *) PHYSTOP);
  initlock(&cow_lock, "cow");
  lst_init(&cow_list);
  // initlock(&kmem.lock, "kmem");
  // freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}
uint 
cowrefCount (void *pa){
   uint count = 0;
  Cow_info *ci; 
  acquire(&cow_lock);
  for(struct list *p = cow_list.next; p != &cow_list; p = p->next){
    ci = (Cow_info *) p;
    if(ci->pa == pa){
      count = ci->refcount;
      break;
    }

  }
  release(&cow_lock);
  return count;
}
uint 
cowput(void * pa){
  uint count = 0;
  Cow_info *ci; 
  acquire(&cow_lock);
  for(struct list *p = cow_list.next; p != &cow_list; p = p->next){
    ci = (Cow_info *) p;
    if(ci->pa != pa)
      continue;
    count = --ci->refcount;
    if(count >= 0)
      goto done;
    else{
      printf("negative refcount in cowput...\n");
    }
    lst_remove(p);
    bd_free(ci);
    break;
  }

  done:
  release(&cow_lock);
  return count;

}
uint
cowget(void * pa){
  uint count = 0;
  Cow_info *ci; 
  acquire(&cow_lock);
  for(struct list *p = cow_list.next; p != &cow_list; p = p->next){
    ci = (Cow_info *) p;
    if(ci->pa == pa){
      count = ++ci->refcount;
      break;
    }

  }
  if(count == 0){
    ci = (Cow_info *) bd_malloc(sizeof(Cow_info));
    if(ci == 0){
      release(&cow_lock);
      panic("Cow_info allocation");
    }
    ci->pa = pa;
    count = ci->refcount = 1;
    lst_push(&cow_list, ci);
  }
  release(&cow_lock);
  return count;
}


// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  uint count = cowput(pa);
  if(count == 0){
    bd_free(pa);
  }
}


// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{

  void *p = bd_malloc(PGSIZE);
  cowget(p);
  return p;
  // struct run *r;

  // acquire(&kmem.lock);
  // r = kmem.freelist;
  // if(r)
  //   kmem.freelist = r->next;
  // release(&kmem.lock);

  // if(r)
  //   memset((char*)r, 5, PGSIZE); // fill with junk
  // return (void*)r;
}
