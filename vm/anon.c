/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "include/threads/mmu.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in(struct page *page, void *kva);
static bool anon_swap_out(struct page *page);
static void anon_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void vm_anon_init(void)
{
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1, 1);

	list_init(&swap_table);
	lock_init(&swap_table_lock);

	/* 1페이지 당 필요한 sector 개수?
	hdd 경우 disk_sector가 512byte의 고정적 크기를 가짐
	가상 페이지의 경우 4kb니까 총 8개의 disk_sector에 나뉘어 저장하게 됨 */

	/* swap_size: 스왑 디스크 안에서 만들 수 있는 총 스왑 슬롯 개수
	-> 스왑 공간 크기 / 1페이지 당 필요한 sector 개수 */
	disk_sector_t swap_size = disk_size(swap_disk) / 8;
	for (disk_sector_t i = 0; i < swap_size; i++)
	{
		/* 만들 수 있는 slot 개수 만큼 만들어 swap_table에 세팅 */
		struct slot *slot = (struct slot *)malloc(sizeof(struct slot));
		slot->page = NULL;
		slot->slot_num = i;

		lock_acquire(&swap_table_lock);
		list_push_back(&swap_table, &slot->swap_elem);
		lock_release(&swap_table_lock);
	}
}

/* Initialize the file mapping */
bool anon_initializer(struct page *page, enum vm_type type, void *kva)
{
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	/* anon_initializer가 호출되는 건 page가 매핑된 상태이므로 swap_slot을 차지하지 않는 상태 */
	anon_page->slot_num = -1;
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in(struct page *page, void *kva)
{
	struct anon_page *anon_page = &page->anon;
	/* swap_in 할 페이지가 저장된 slot num */
	disk_sector_t page_slot_num = anon_page->slot_num;
	struct list_elem *e;
	struct slot *slot;

	lock_acquire(&swap_table_lock);
	/* swap_table을 돌며 해당 slot num을 찾아 읽어오기 */
	for (e = list_begin(&swap_table); e != list_end(&swap_table); e = list_next(e))
	{
		slot = list_entry(e, struct slot, swap_elem);
		/* 타겟 페이지가 저장된 slot이라면 */
		if (slot->slot_num == page_slot_num)
		{
			/* swap_disk에서 읽어와 kva+DISK_SECTOR_SIZE * i에 담음 */
			for (int i = 0; i < 8; i++)
			{
				disk_read(swap_disk, page_slot_num * 8 + i, kva + DISK_SECTOR_SIZE * i);
			}
			/* 이제 swap in 했으니까 swap_table에서 제거 */
			slot->page = NULL;
			/* 해당 페이지는 이제 swap_table의 slot을 차지하지 않음 */
			anon_page->slot_num = -1;
			lock_release(&swap_table_lock);
			return true;
		}
	}
	lock_release(&swap_table_lock);
	return false;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out(struct page *page)
{
	if (page == NULL)
	{
		return false;
	}

	struct anon_page *anon_page = &page->anon;
	struct list_elem *e;
	struct slot *slot;

	lock_acquire(&swap_table_lock);
	/* swap_table을 돌며 빈 슬롯을 찾아 페이지 저장(First-Fit) */
	for (e = list_begin(&swap_table); e != list_end(&swap_table); e = list_next(e))
	{
		slot = list_entry(e, struct slot, swap_elem);
		/* slot의 page가 NULL인 슬롯 = 빈 슬롯 */
		if (slot->page == NULL)
		{
			/* 해당 슬롯에 page의 내용 저장 */
			for (int i = 0; i < 8; i++)
			{
				disk_write(swap_disk, slot->slot_num * 8 + i, page->va + DISK_SECTOR_SIZE * i);
			}

			/* 페이지에 swap_table 위치(슬롯 번호) 저장 & slot->page에 해당 페이지 저장 */
			anon_page->slot_num = slot->slot_num;
			slot->page = page;
			/* page와 매핑되어있었던 frame과의 연결 끊기 */
			page->frame->page = NULL;
			page->frame = NULL;
			/* 가상 메모리 목록에서도 지우기 */
			pml4_clear_page(thread_current()->pml4, page->va);
			lock_release(&swap_table_lock);
			return true;
		}
	}
	lock_release(&swap_table_lock);
	PANIC("No more empty slots on disk");
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy(struct page *page)
{
	struct anon_page *anon_page = &page->anon;
	struct list_elem *e;
	struct slot *slot;

	lock_acquire(&swap_table_lock);
	for (e = list_begin(&swap_table); e != list_end(&swap_table); e = list_next(e))
	{
		slot = list_entry(e, struct slot, swap_elem);

		if (slot->slot_num == anon_page->slot_num)
		{
			slot->page = NULL;
			break;
		}
	}
	lock_release(&swap_table_lock);
}
