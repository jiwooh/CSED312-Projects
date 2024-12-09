#include <string.h>
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/frame.h"
#include "vm/spt.h"

extern struct lock file_lock;
static hash_hash_func spt_hash_func;
static hash_less_func spt_less_func;
static void page_destructor(struct hash_elem *elem, void *aux);

/* Initialize hash table */
void init_spt(struct hash *spt)
{
  hash_init(spt, spt_hash_func, spt_less_func, NULL);
}

/* Simple hash function for hash table initialization */
static unsigned
spt_hash_func(const struct hash_elem *elem, void *aux)
{
  struct spte *p = hash_entry(elem, struct spte, hash_elem);
  return hash_bytes(&p->upage, sizeof(p->kpage));
}

/* Simple compare function for hash table initialization */
static bool
spt_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
  void *a_upage = hash_entry(a, struct spte, hash_elem)->upage;
  void *b_upage = hash_entry(b, struct spte, hash_elem)->upage;
  return a_upage < b_upage;
}

/* Destroy hash table */
void destroy_spt(struct hash *spt)
{
  hash_destroy(spt, page_destructor);
}

/* Simple hash action function for hash table destruction */
static void
page_destructor(struct hash_elem *elem, void *aux)
{
  struct spte *e;
  e = hash_entry(elem, struct spte, hash_elem);
  free(e);
}

/* Initialize S-page table entry for frame
 *  usage: setup_stack at userprog/process.c
 */
void init_frame_spte(struct hash *spt, void *upage, void *kpage)
{
  struct spte *e;
  e = (struct spte *)malloc(sizeof *e);

  e->upage = upage;
  e->kpage = kpage;

  e->status = FRAME_PAGE;

  e->file = NULL;
  e->writable = true;

  hash_insert(spt, &e->hash_elem);
}

/* Initialize S-page table entry for file
 *  usage: load_segment at userprog/process.c
 */
struct spte *
init_file_spte(struct hash *spt, void *_upage, struct file *_file, off_t _ofs, uint32_t _read_bytes, uint32_t _zero_bytes, bool _writable)
{
  struct spte *e;

  e = (struct spte *)malloc(sizeof *e);

  e->upage = _upage;
  e->kpage = NULL;

  e->file = _file;
  e->file_offset = _ofs;
  e->read_bytes = _read_bytes;
  e->zero_bytes = _zero_bytes;
  e->writable = _writable;

  e->status = FILE_PAGE;

  hash_insert(spt, &e->hash_elem);

  return e;
}

/* Initialize S-page table entry for zero
 *  usage: page_fault at userprog/exception.c
 */
void
init_zero_spte (struct hash *spt, void *upage)
{
  struct spte *e;
  e = (struct spte *) malloc (sizeof *e);
  
  e->status = ZERO_PAGE;
  e->kpage = NULL;
  e->upage = upage;
  
  e->file = NULL;
  e->writable = true;
  hash_insert (spt, &e->hash_elem);
}

/* Lazy loading implementation
 *  usage: page_fault at userprog/exception.c
 *
 */
bool load_page(struct hash *spt, void *upage)
{
  struct spte *e;
  uint32_t *pagedir;
  void *kpage;

  struct spte pe;
  struct hash_elem *elem;
  pe.upage = upage;
  elem = hash_find(spt, &pe.hash_elem);
  if(elem==NULL) {
    //printf("load_page hash find null");
    sys_exit(-1);
  }
  else e = hash_entry(elem, struct spte, hash_elem);
  if (e == NULL) {
    //printf("load_page hash entry null");
    sys_exit(-1);
  }

  kpage = falloc_get_page(PAL_USER, upage);
  if (kpage == NULL) {
    //printf("load_page kpage null");
    sys_exit(-1);
  }

  bool was_holding_lock = lock_held_by_current_thread(&file_lock);

  switch (e->status)
  {
  case ZERO_PAGE:
    memset(kpage, 0, PGSIZE);
    break;
  case SWAP_PAGE:
    // swap_in(e, kpage);
    break;
  case FILE_PAGE:
    if (!was_holding_lock) lock_acquire(&file_lock);

    // almost same code at #else part at load_segment
    if (file_read_at(e->file, kpage, e->read_bytes, e->file_offset) != (int)e->read_bytes)
    {
      falloc_free_page(kpage);
      lock_release(&file_lock);
      //printf("load_page file error");
      sys_exit(-1);
    }
    memset(kpage + e->read_bytes, 0, e->zero_bytes);
  
    if (!was_holding_lock) lock_release(&file_lock);
    break;

  case FRAME_PAGE:
    // is already loaded.
    return true;
  }

  pagedir = thread_current()->pagedir;
  /* Add the page to the process's address space. */
  if (!pagedir_set_page(pagedir, upage, kpage, e->writable))
  {
    falloc_free_page(kpage);
    //printf("load_page pagedir set fail");
    sys_exit(-1);
  }

  e->kpage = kpage;
  e->status = FRAME_PAGE;

  return true;
}

/* search given upage within spt. */
struct spte *
get_spte(struct hash *spt, void *upage)
{
  struct spte e;
  struct hash_elem *elem;
  e.upage = upage;
  elem = hash_find(spt, &e.hash_elem);
  if(elem==NULL) return NULL;
  else return hash_entry(elem, struct spte, hash_elem);
}

/* delete page */
void page_delete(struct hash *spt, struct spte *entry)
{
  hash_delete(spt, &entry->hash_elem);
  free(entry);
}