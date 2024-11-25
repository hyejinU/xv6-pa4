// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

// [PA4]
struct spinlock lru_lock;
struct page pages[PHYSTOP/PGSIZE];
struct page *page_lru_head;
int num_free_pages;
int num_lru_pages;
//

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  initlock(&lru_lock, "lru");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
  // cprintf("num_free_pages: %d\n", num_free_pages);
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  if(kmem.use_lock) {
    acquire(&kmem.lock);
  }
  r = (struct run*)v;
  r->next = kmem.freelist;

// [PA4]
  num_free_pages++;
//

  kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  struct run *r;

try_again:
  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
//  if(!r && reclaim())
//	  goto try_again;
  if(r) {
    kmem.freelist = r->next;
    num_free_pages--; // [PA4]
  } else {
// [PA4]
    // no free page, so swap out.
    if (kmem.use_lock) {
      release(&kmem.lock);
    }
    // there is no page in LRU list, out of memory error should be occured
    if (swap_out() == 0) {
      cprintf("kalloc: out of memory\n"); // inside the kalloc function, just cprintf error message
      return (char*)0;  // kalloc should return 0 when OOM occurs
    }
    goto try_again;
  }
  if(kmem.use_lock)
    release(&kmem.lock);
  return (char*)r;
}

// [PA4]

// lru lock must be acquired before
void add_page_to_lru_list(struct page* page) {
  // lru list에 이 page만 있음
  if(num_lru_pages == 0) {
    page->prev = page;
    page->next = page;
  } else if (num_lru_pages == 1) {
    // 현재 head 다음에 나를 insert
    page->prev = page_lru_head;
    page_lru_head->next = page;

    // haed->prev와 page->next가 서로 point
    page_lru_head->prev = page;
    page->next = page_lru_head;
  } else {
    // tail의 다음에 나를 insert
    page_lru_head->prev->next = page;
    page->prev = page_lru_head->prev;

    // haed->prev와 page->next가 서로 point
    page_lru_head->prev = page;
    page->next = page_lru_head;
  }

  page_lru_head = page;
  num_lru_pages++;
  // if ((num_lru_pages % 100) == 0) 
  //   cprintf("num_lru_pages++: %d\n", num_lru_pages);
}

// lru lock must be acquired before
void del_page_from_lru(struct page* page) {

  // haed를 eviction 시도
  if(page == page_lru_head) {
    // head부터 이동
    page_lru_head = page->next;
  }
  
  // head부터 시작
  if(page->next == page || num_lru_pages == 1){
    page_lru_head = NULL; // lru page가 1개
  } else {
     // 나(page)를 delete
    page->next->prev = page->prev; // 내 next의 이전을 내 prev로
    page->prev->next = page->next; // 내 prev의 다음을 내 next로
  }

  // 나 초기화
  page->next = NULL;
  page->prev = NULL;

  num_lru_pages--;
  // if ((num_lru_pages % 100) == 0) 
  //   cprintf("num_lru_pages--: %d\n", num_lru_pages);
}


// allocate lru list page
void kalloc_to_lru_list(pde_t* pgdir, char* pa, void* va) {
  acquire(&lru_lock);
  struct page* page = &pages[V2P(pa)/PGSIZE];
  page->pgdir = pgdir;
  page->vaddr = va;
  add_page_to_lru_list(page);
  release(&lru_lock);

}

// deallocate lru list page
void kfree_from_lru_list(char* v) {
  acquire(&lru_lock);

  struct page* page = &pages[V2P(v)/PGSIZE];
  page->pgdir = NULL;
  page->vaddr = NULL;

  del_page_from_lru(page);

  release(&lru_lock);
}

struct page* select_victim(void) {

  acquire(&lru_lock);

  pde_t* pde;
  pte_t* pgtab;
  pte_t* pte;
  
  // haed부터 시작
  struct page* curr = page_lru_head;
  if (curr == 0) {
    release(&lru_lock);
    return NULL;
  }

  while(1) {
    struct page* nxt = curr->next;
    pde = &curr->pgdir[PDX(curr->vaddr)];

    // if(*pde & PTE_P) / else: 고려하지 않아도 됨
    // pde must have PTE_P bits in lru_list
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
    pte = &pgtab[PTX(curr->vaddr)];

    // if PTE_A == 1, clear it and send the page to the tail of LRU list
    // if PTE_A == 0, evict the page (victim page)
    if (*pte & PTE_A) {
      *pte &= (~PTE_A); //clear
      if (curr == page_lru_head) {
        page_lru_head = curr->next;
      } else {
        // delete myself
        curr->prev->next = curr->next;
        curr->next->prev = curr->prev;
        // to tail
        page_lru_head->prev->next = curr;
        curr->prev = page_lru_head->prev;
        page_lru_head->prev = curr;
        curr->next = page_lru_head;
      }
    } else {
      // victim page
      release(&lru_lock);
      return curr;
    }
    curr = nxt;
  }
}
//