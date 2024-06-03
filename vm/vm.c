/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "include/lib/kernel/hash.h"
#include "include/lib/kernel/list.h"
#include "include/threads/vaddr.h"
#include "include/threads/mmu.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void)
{
	vm_anon_init();
	vm_file_init();
#ifdef EFILESYS /* For project 4 */
	pagecache_init();
#endif
	register_inspect_intr();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type(struct page *page)
{
	int ty = VM_TYPE(page->operations->type);
	switch (ty)
	{
	case VM_UNINIT:
		return VM_TYPE(page->uninit.type);
	default:
		return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
									vm_initializer *init, void *aux)
{

	ASSERT(VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page(spt, upage) == NULL)
	{
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		/* TODO: Insert the page into the spt. */
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
/* spt에서 va 찾고, page 반환 */
struct page *
spt_find_page(struct supplemental_page_table *spt UNUSED, void *va UNUSED)
{
	struct page *page = NULL;
	/* TODO: Fill this function. */
	page = malloc(sizeof(struct page));

	struct hash_elem *e;

	/* 할당한 page의 va 값 할당 후, 해당 va에 해당하는 elem 찾기 */
	page->va = va;
	e = hash_find(&spt, &page->bucket_elem);
	/* hash_elem이 page 내에 있어서 여기서 free(page)하면 안 되겠는데, 나중에도 안 해줘도 되나??? */
	/* 해당하는 hash_elem이 있으면, 그 hash_entry로 해당 페이지 반환 */
	return e != NULL ? hash_entry(e, struct page, bucket_elem) : NULL;
}

/* Insert PAGE into spt with validation. */
/* spt에 page 삽입 + 유효성 검사(= spt에서 va가 존재하는지) */
bool spt_insert_page(struct supplemental_page_table *spt UNUSED,
					 struct page *page UNUSED)
{
	/* 유효성 검사?: 동일한 va값이 있으면 안 되기 때문에 spt에 삽입할 page의 va값이 있는지 검사 = hash_insert에서 해줌 */

	/* hash_insert를 해서 동일 va값이 없었다면, 즉 반환값이 NULL이라면 삽입 성공 true 반환 */
	/* 동일한 va값이 존재한다면, 반환값이 NULL이 아니기 떄문에 false 반환 */
	return hash_insert(&spt->spt_hash, &page->bucket_elem) == NULL ? true : false;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page)
{
	vm_dealloc_page(page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim(void)
{
	struct frame *victim = NULL;
	/* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame(void)
{
	struct frame *victim UNUSED = vm_get_victim();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame(void)
{
	struct frame *frame = NULL;
	/* TODO: Fill this function. */

	/* 유저 풀에서 새로운 물리 메모리 할당 받음 */
	/* palloc_get_page가 kva를 반환 */
	/* physical adress = virtual adress + KERN_BASE(커널은 모두 공유) */
	void *kva = palloc_get_page(PAL_USER);

	if (kva == NULL)
	{
		/* kva가 NULL이라면 할당 실패, 물리 메모리 공간 꽉 찼다는 의미 */
		/* 나중에 SWAP-OUT 처리 필요 */
		PANIC("SWAP OUT");
	}

	/* 프레임 동적 할당 후 멤버 초기화 */
	frame = malloc(sizeof(struct frame));
	frame->kva = kva;

	ASSERT(frame != NULL);
	ASSERT(frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth(void *addr UNUSED)
{
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp(struct page *page UNUSED)
{
}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED,
						 bool user UNUSED, bool write UNUSED, bool not_present UNUSED)
{
	struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	return vm_do_claim_page(page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page)
{
	destroy(page);
	free(page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page(void *va UNUSED)
{
	struct page *page = NULL;
	struct thread *curr = thread_current();

	/* spt에서 va에 해당하는 page를 가져와 vm_do_claim_page()로 매핑하기 */
	page = spt_find_page(&curr->spt, va);
	if (page == NULL)
	{
		return false;
	}

	return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page(struct page *page)
{
	struct frame *frame = vm_get_frame();

	/* Set links, 페이지와 프레임 매핑 */
	frame->page = page;
	page->frame = frame;

	/* 가상주소와 물리주소를 매핑한 정보를 페이지 테이블에 추가 */
	struct thread *curr = thread_current();
	pml4_set_page(curr->pml4, page->va, frame->kva, page->writable);

	return swap_in(page, frame->kva);
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED)
{
	/* spt 초기화 */
	hash_init(spt, hash_func, less_func, NULL);
}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
								  struct supplemental_page_table *src UNUSED)
{
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED)
{
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}

/* --------- 추가 함수 --------- */
uint64_t hash_func(const struct hash_elem *e, void *aux)
{
	/* 해싱하는 함수 */
	/* hash_elem으로 page 가져오기 */
	const struct page *page_ = hash_entry(e, struct page, bucket_elem);
	/* page의 va(가상주소)를 key값으로 해 해싱한 후 bucket_idx 값 반환 */
	return hash_bytes(&page_->va, sizeof(page_->va));
}

bool less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
	/* hash_elem으로 가져온 두 페이지의 가상주소를 기준으로 비교하는 함수 */
	const struct page *a_page = hash_entry(a, struct page, bucket_elem);
	const struct page *b_page = hash_entry(b, struct page, bucket_elem);

	return a_page->va < b_page->va;
}