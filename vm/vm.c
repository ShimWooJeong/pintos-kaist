/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "include/lib/kernel/hash.h"
#include "include/lib/kernel/list.h"
#include "include/threads/vaddr.h"
#include "include/threads/mmu.h"
#include "include/userprog/process.h"

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
	list_init(&frame_table);
	lock_init(&frame_table_lock);
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
/* page 구조체를 생성하고 적절한 초기화 함수 설정 */
/* 해당 함수 구현 후엔, 페이지 생성 시 해당 함수를 사용해 생성 */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
									vm_initializer *init, void *aux)
{
	ASSERT(VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current()->spt;

	/* Check wheter the upage is already occupied or not. */
	/* upage가 이미 사용 중인지 확인 */
	if (spt_find_page(spt, upage) == NULL)
	{
		/* 페이지 생성 */
		struct page *p = (struct page *)malloc(sizeof(struct page));

		/* 페이지 타입에 따라 초기화 함수를 가져와 담기 */
		bool (*page_initializer)(struct page *, enum vm_type, void *);

		switch (VM_TYPE(type))
		{
		case VM_ANON:
			page_initializer = anon_initializer;
			break;

		case VM_FILE:
			page_initializer = file_backed_initializer;
			break;
		}

		/* 생성한 페이지를 uninit page로 초기화 */
		uninit_new(p, upage, init, type, aux, page_initializer);

		/* 페이지 구조체의 필드 수정 */
		/* 필드 값을 수정할 땐 uninit 호출 이후에 해야 한다 = uninit_new 함수 안에서 구조체 내용이 모두 새로 할당되기 때문에 */
		p->writable = writable;

		/* 생성한 페이지 spt에 추가 */
		return spt_insert_page(spt, p);
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
	/* pg_round_down: 가장 가까운 페이지 경계로 반올림 */
	/* 사용자가 원하는 임의의 가상 주소에 접근 시, 해당 주소가 포함된 page의 시작 주소를 찾기 위해 진행 */
	page->va = pg_round_down(va);
	e = hash_find(&spt->spt_hash, &page->bucket_elem);
	free(page);
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
	// hash_delete(&spt->spt_hash, &page->bucket_elem);
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
	struct page *page = NULL;
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
	frame->page = NULL;

	lock_acquire(&frame_table_lock);
	list_push_back(&frame_table, &frame->frame_elem);
	lock_release(&frame_table_lock);

	ASSERT(frame != NULL);
	ASSERT(frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void

vm_stack_growth(void *addr UNUSED)
{
	/* addr을 PGSIZE 만큼 내려 anon page를 할당 */
	vm_alloc_page(VM_ANON | VM_MARKER_0, pg_round_down(addr), 1);
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

	if (addr == NULL)
	{
		return false;
	}

	if (is_kernel_vaddr(addr))
	{
		return false;
	}

	/* 접근한 메모리의 물리 프레임이 존재하지 않는 경우, not_present = true */
	if (not_present)
	{
		void *rsp = f->rsp;
		if (!user)
			rsp = thread_current()->rsp;

		/* stack growth로 처리할 수 있는 경우 */
		/* (USER_STACK - (1 << 20): 스택의 최대 크기 경계 주소 */
		/* 1 << 20 = 2 ^ 20 = 1MB */
		if (USER_STACK - (1 << 20) <= rsp - 8 && rsp - 8 == addr && addr <= USER_STACK)
		{
			vm_stack_growth(addr);
		}
		else if (USER_STACK - (1 << 20) <= rsp && rsp <= addr && addr <= USER_STACK)
		{
			vm_stack_growth(addr);
		}

		/* spt에서 해당 가상 주소에 해당하는 페이지 반환 */
		page = spt_find_page(spt, addr);
		if (page == NULL)
		{
			return false;
		}
		/* 쓰기가 불가능한 페이지(0)에 write를 요청한 경우 */
		if (write == 1 && page->writable == 0)
		{
			return false;
		}
		return vm_do_claim_page(page);
	}
	return false;
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
	hash_init(&spt->spt_hash, hash_func, less_func, NULL);
}

/* Copy supplemental page table from src to dst */
/* 자식 프로세스 생성할 때 spt 복사하는 함수 */
/* src를 dst에 복사 */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
								  struct supplemental_page_table *src UNUSED)
{
	struct hash_iterator i;
	hash_first(&i, &src->spt_hash);
	/* src 각각의 페이지들을 반복문으로 복사 */
	while (hash_next(&i))
	{
		/* 현재 src_page의 속성들 */
		struct page *src_page = hash_entry(hash_cur(&i), struct page, bucket_elem);
		enum vm_type type = src_page->operations->type;
		void *upage = src_page->va;
		bool writable = src_page->writable;

		/* page가 uninit type이라면  */
		if (type == VM_UNINIT)
		{
			/* uninit type의 페이지 생성 및 초기화 */
			vm_initializer *init = src_page->uninit.init;
			void *aux = src_page->uninit.aux;
			vm_alloc_page_with_initializer(VM_ANON, upage, writable, init, aux);
			continue;
		}

		/* page가 file_backed type이라면  */
		if (type == VM_FILE)
		{
			struct lazy_load_arg *file_aux = malloc(sizeof(struct lazy_load_arg));
			file_aux->file = src_page->file.file;
			file_aux->ofs = src_page->file.ofs;
			file_aux->read_bytes = src_page->file.read_bytes;
			file_aux->zero_bytes = src_page->file.zero_bytes;

			if (!vm_alloc_page_with_initializer(type, upage, writable, NULL, file_aux))
			{
				return false;
			}

			struct page *file_page = spt_find_page(dst, upage);
			file_backed_initializer(file_page, type, NULL);
			file_page->frame = src_page->frame;

			pml4_set_page(thread_current()->pml4, file_page->va, src_page->frame->kva, src_page->writable);
			continue;
		}

		/* page가 anon type이라면 */
		if (!vm_alloc_page(type, upage, writable))
		{
			return false;
		}

		/* 부모 type의 초기화 함수를 담은 uninit page로 초기화 후 */
		/* page fault 처리 (vm_claim_page) 후 memcpy */
		if (!vm_claim_page(upage))
		{
			return false;
		}

		struct page *dst_page = spt_find_page(dst, upage);
		memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
	}
	return true;
}

/* Free the resource hold by the supplemental page table */
/* 프로세스가 종료될 때와 실행 중 process_cleanup을 호출할 때 */
/* 페이지 항목들을 순회하며 페이지 type에 맞는 destroy 함수 호출 */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED)
{
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */

	hash_clear(&spt->spt_hash, hash_page_destroy);
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

void hash_page_destroy(struct hash_elem *e, void *aux)
{
	struct page *page = hash_entry(e, struct page, bucket_elem);
	destroy(page);
	free(page);
}