/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "include/userprog/process.h"

static bool file_backed_swap_in(struct page *page, void *kva);
static bool file_backed_swap_out(struct page *page);
static void file_backed_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void vm_file_init(void)
{
}

/* Initialize the file backed page */
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva)
{
	/* Set up the handler : page에 file_backed_page에 대한 핸들러 설정 */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;

	/* 매핑을 해제할 때, 파일에 대한 수정사항을 기존 파일에 기록하기 위해 */
	/* 파일에 대한 정보를 페이지에 저장해두어야 함*/

	/* file_page에 파일에 대한 정보 추가 */
	struct lazy_load_arg *lazy_load_arg = (struct lazy_load_arg *)page->uninit.aux;
	file_page->file = lazy_load_arg->file;
	file_page->ofs = lazy_load_arg->ofs;
	file_page->read_bytes = lazy_load_arg->read_bytes;
	file_page->zero_bytes = lazy_load_arg->zero_bytes;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in(struct page *page, void *kva)
{
	struct file_page *file_page UNUSED = &page->file;
	/* lazy_load_segment 호출: 물리 frame 할당받아 페이지와 매핑해준 뒤, 디스크의 파일에서 물리 frame으로 데이터 미러링(복사) */
	return lazy_load_segment(page, file_page);
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out(struct page *page)
{
	struct file_page *file_page UNUSED = &page->file;

	/* 수정사항이 있었다면 파일(in disk)에 수정사항 적용해주고, dirty bit = 0으로 초기화 */
	if (pml4_is_dirty(thread_current()->pml4, page->va))
	{
		file_write_at(file_page->file, page->va, file_page->read_bytes, file_page->ofs);
		pml4_set_dirty(thread_current()->pml4, page->va, 0);
	}

	/* 페이지와 프레임 연결 끊음 */
	page->frame->page = NULL;
	page->frame = NULL;
	pml4_clear_page(thread_current()->pml4, page->va);
	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy(struct page *page)
{
	struct file_page *file_page UNUSED = &page->file;

	/* 수정사항이 있었다면 file_write_at으로 반영하고 dirty를 0으로 수정 */
	if (pml4_is_dirty(thread_current()->pml4, page->va))
	{
		file_write_at(file_page->file, page->va, file_page->read_bytes, file_page->ofs);
		pml4_set_dirty(thread_current()->pml4, page->va, 0);
	}

	/* 가상페이지 목록에서 제거 */
	pml4_clear_page(thread_current()->pml4, page->va);
}

/* Do the mmap */
void *
do_mmap(void *addr, size_t length, int writable,
		struct file *file, off_t offset)
{
	/* reopen()으로 해당 파일에 대한 새로운 파일 디스크립터 얻음 */
	struct file *f = file_reopen(file);
	/* 매핑 성공 시 반환할 가상 주소 */
	void *start_addr = addr;

	/* 매핑에 쓰인 총 페이지 수 */
	int total_page_count;

	/* 읽어올 길이가 PGSIZE보다 작을 시 페이지 수 1개 */
	if (length <= PGSIZE)
		total_page_count = 1;
	/* 일어올 길이가 PGSIZE로 나누어 떨어지지 않는다면 남은 부분을 적재하기 위해 나눈 몫에 +1 */
	else if (length % PGSIZE != 0)
		total_page_count = length / PGSIZE + 1;
	/* 일어올 길이가 PGSIZE로 나누어 떨어진다면 페이지 수는 나눈 몫 */
	else
		total_page_count = length / PGSIZE;

	size_t read_bytes = file_length(f) < length ? file_length(f) : length;
	size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;

	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(addr) == 0);	  // upage 페이지 정렬 확인
	ASSERT(offset % PGSIZE == 0); // offset 페이지 정렬 확인

	while (read_bytes > 0 || zero_bytes > 0)
	{
		/* 페이지 채우기 */
		/* page_read_bytes만큼을 읽고, page_zero_bytes만큼을 0으로 채움 */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct lazy_load_arg *lazy_load_arg = (struct lazy_load_arg *)malloc(sizeof(struct lazy_load_arg));
		lazy_load_arg->file = f;
		lazy_load_arg->ofs = offset;
		lazy_load_arg->read_bytes = page_read_bytes;
		lazy_load_arg->zero_bytes = page_zero_bytes;

		/* vm_alloc_page_with_initializer로 lazy loading할 페이지 생성 (type은 file-backed) */
		if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment, lazy_load_arg))
			return NULL;

		struct page *p = spt_find_page(&thread_current()->spt, start_addr);
		p->mapped_page_cnt = total_page_count;

		/* read bytes와 zero_bytes 감소시키고 가상 주소 증가 */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;
	}
	return start_addr;
}

/* Do the munmap */
void do_munmap(void *addr)
{
	struct supplemental_page_table *spt = &thread_current()->spt;
	struct page *p = spt_find_page(spt, addr);
	/* 같은 파일이 매핑된 페이지가 모두 해제되도록 총 매핑된 페이지 수를 가져와 전부 해제 */
	/* destroy 호출로 file_backed_destroy에서 파일의 수정사항을 기록하고 가상 페이지 목록에서 해당 페이지가 제거되도록 함 */
	int mapped_pages = p->mapped_page_cnt;

	for (int i = 0; i < mapped_pages; i++)
	{
		if (p)
		{
			destroy(p);
		}
		addr += PGSIZE;
		p = spt_find_page(spt, addr);
	}
}
