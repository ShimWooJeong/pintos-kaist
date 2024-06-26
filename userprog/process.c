#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "userprog/syscall.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup(void);
static bool load(const char *file_name, struct intr_frame *if_);
static void initd(void *f_name);
static void __do_fork(void *);

/* General process initializer for initd and other process. */
static void
process_init(void)
{
	struct thread *current = thread_current();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t process_create_initd(const char *file_name)
{
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page(0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy(fn_copy, file_name, PGSIZE);

	char *save_ptr;
	file_name = strtok_r(file_name, " ", &save_ptr);
	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create(file_name, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page(fn_copy);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd(void *f_name)
{
#ifdef VM
	supplemental_page_table_init(&thread_current()->spt);
#endif

	process_init();

	if (process_exec(f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
/* 현재 프로세스를 복제해 새 프로세스를 생성 */
tid_t process_fork(const char *name, struct intr_frame *if_ UNUSED)
{
	/* 실행 중인 부모 프로세스의 contexts를 복사한다. */
	/* 굳이 복사를 하는 이유는 if 정보를 직접 넘길 시, 부모 프로세스가 계속 실행하며 값이 변할 수 있기 때문에 */
	/* 부모 프로세스의 parent_if에 if값을 복사한 후, 부모 프로세스를 __do_fork의 인자로 담아준다. */
	struct thread *t = thread_current();
	memcpy(&t->parent_if, if_, sizeof(struct intr_frame));

	/* 인자로 받은 name으로 된 새 스레드 생성 */
	tid_t pid = thread_create(name, PRI_DEFAULT, __do_fork, t);

	if (pid == TID_ERROR)
	{
		return TID_ERROR;
	}

	/* 부모 프로세스의 자식 리스트에서 방금 새로 생성된 자식 프로세스 가져오기 */
	struct thread *child = get_child_process(pid);
	/* 자식 프로세스의 __do_fork를 대기 */
	/* __do_fork에서 복제가 완료되면 sema_up을 해줌으로써 대기 이탈되며 종료 */
	sema_down(&child->fork_sema);

	return pid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte(uint64_t *pte, void *va, void *aux)
{
	struct thread *current = thread_current();
	struct thread *parent = (struct thread *)aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if (is_kernel_vaddr(va))
	{
		// printf("-------------------in duplicate_pte1\n");
		return true;
	}

	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page(parent->pml4, va);
	if (parent_page == NULL)
	{
		printf("-------------------in duplicate_pte2\n");
		return false;
	}

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	newpage = palloc_get_page(PAL_USER | PAL_ZERO);
	if (newpage == NULL)
	{
		printf("-------------------in duplicate_pte3\n");
		return false;
	}

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy(newpage, parent_page, PGSIZE);
	writable = is_writable(pte);

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page(current->pml4, va, newpage, writable))
	{
		/* 6. TODO: if fail to insert page, do error handling. */
		palloc_free_page(newpage);
		printf("-------------------in duplicate_pte4\n");
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork(void *aux)
{
	struct intr_frame if_;
	struct thread *parent = (struct thread *)aux;
	struct thread *current = thread_current();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if = &parent->parent_if;
	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	memcpy(&if_, parent_if, sizeof(struct intr_frame));

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
	{
		printf("------------------1\n");
		goto error;
	}

	process_activate(current);
#ifdef VM
	supplemental_page_table_init(&current->spt);
	if (!supplemental_page_table_copy(&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each(parent->pml4, duplicate_pte, parent))
	{
		printf("------------------2\n");
		goto error;
	}
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/

	if (parent->next_fd == FDTCOUNT_LIMIT)
	{
		printf("------------------3\n");
		goto error;
	}
	current->fdt[0] = parent->fdt[0];
	current->fdt[1] = parent->fdt[1];

	for (int i = 2; i < FDTCOUNT_LIMIT; i++)
	{
		struct file *f = parent->fdt[i];
		if (f == NULL)
		{
			continue;
		}
		current->fdt[i] = file_duplicate(f);
	}
	current->next_fd = parent->next_fd;
	sema_up(&current->fork_sema);
	if_.R.rax = 0;
	process_init();

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret(&if_);
error:
	succ = false;
	sema_up(&current->fork_sema);
	printf("--------------------------------in__do_fork TID_ERROR\n");
	exit(TID_ERROR);
	// thread_exit();
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int process_exec(void *f_name)
{
	char *file_name = f_name;
	bool success;

	tid_t tid;

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup();

	/* 여기서 passing하고 load에는 실행 파일 이름만 넘겨줌 */
	/* file_name을 passing해 tokenize & argument_list 생성 */
	char *token, *save_ptr;
	char *argvs[128]; /* 인자 담을 list */
	int argc = 0;	  /* 인자 개수 */

	token = strtok_r(file_name, " ", &save_ptr);
	/* strtok_r 함으로써 file_name = 실행파일 이름으로 업데이트 */
	/* strtok_r 함수는 원본 문자열을 변경함 -> " "를 \0으로 대체 */
	/* 그래서 원본인 file_name이 첫 인자 문자열이 됨 = args-single */
	argvs[argc] = token;
	// printf("\n\nargvs[%d] : %s\n", argc, argvs[argc]);

	while (token != NULL)
	{
		token = strtok_r(NULL, " ", &save_ptr);
		argc++;
		argvs[argc] = token;
		// printf("argvs[%d] : %s\n", argc, argvs[argc]);
	}

	/* And then load the binary */
	/* load로 넘겨주는 인자 file_name = 실행파일 이름 */
	lock_acquire(&filesys_lock);
	success = load(file_name, &_if);
	lock_release(&filesys_lock);

	if (!success)
	{
		palloc_free_page(file_name);
		return -1;
	}

	/* user stack에 프로그램 이름 & 인자들 저장하는 함수 */
	/* 마지막 인자로 intr_frame을 넘겨주고, rsi&rdi 레지스터에 할당하는 것도 argu_stack에서 하도록 */
	argument_stack(argvs, argc, &_if);

	// hex_dump(_if.rsp, _if.rsp, USER_STACK - _if.rsp, true);

	/* If load failed, quit. */
	palloc_free_page(file_name);

	/* Start switched process. */
	do_iret(&_if);
	NOT_REACHED();
}

void argument_stack(char **argvs, int argc, struct intr_frame *if_)
{
	char *argv_addr[128]; /* 프로그램 이름 및 인자의 '주소'를 저장할 포인터 배열 */

	/* 프로그램 이름 및 인자 push */
	int i, j;
	for (i = argc - 1; i > -1; i--)
	{
		int len = strlen(argvs[i]) + 1; /* +1을 하는 이유: '\0'이 포함된 인자 문자열의 길이 */
		if_->rsp -= len;
		memcpy(if_->rsp, argvs[i], len);
		argv_addr[i] = if_->rsp;
		// printf("argv_address[%d]: %p\n", i, &argv_addr[i]);
	}

	/* word-align = 8의 배수 맞춰주기 */
	while (if_->rsp % 8 != 0)
	{
		if_->rsp -= sizeof(uint8_t);
		*(uint8_t *)if_->rsp = 0;
	}

	/* 전체 문자열의 끝을 나타내는 것 같음, 0으로 set 해주기 */
	if_->rsp -= sizeof(char *);
	memset(if_->rsp, 0, sizeof(char *));

	/* 프로그램 이름 및 인자의 '주소' push */
	for (i = argc - 1; i > -1; i--)
	{
		if_->rsp -= sizeof(char *);
		memcpy(if_->rsp, &argv_addr[i], sizeof(char *));
	}

	/* fake address(0) push */
	/* 원래는 return_addr, 즉 함수를 호출하는 부분의 다음 수행 명령어 주소를 저장 */
	if_->rsp -= sizeof(void *);
	memset(if_->rsp, 0, sizeof(void *));

	/* rdi레지스터에 argc(문자열의 개수) push */
	if_->R.rdi = argc;
	/* rsi레지스터에 argvs의 첫 주소 push */
	/* fake_addr 위에 argvs[0]이니까 fake_addr 크기만큼 더한 위치 */
	if_->R.rsi = if_->rsp + sizeof(void *);
}

/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int process_wait(tid_t child_tid UNUSED)
{
	/* 자식 프로세스가 종료될 때까지 기다리고, 종료 상태 반환 */

	/* 자식 프로세스 받아오기 */
	struct thread *child = get_child_process(child_tid);

	if (child == NULL)
	{
		return -1;
	}

	/* 자식 프로세스가 종료될 때까지 대기 = 해당 프로세스가 종료될 때 sema_up */
	sema_down(&child->wait_sema);
	/* 자식 프로세스가 종료되었으면 자식 리스트에서 해당 자식 삭제 */
	list_remove(&child->child_elem);
	/* 자식의 대기 상태 이탈 */
	sema_up(&child->exit_sema);

	return child->exit_status;
}

/* Exit the process. This function is called by thread_exit (). */
void process_exit(void)
{
	struct thread *curr = thread_current();

	/* 해당 프로세스의 fdt의 모든 값을 0으로 만들어줌, 즉 모든 열린 파일 닫기 */
	for (int i = 0; i < FDTCOUNT_LIMIT; i++)
	{
		close(i);
	}

	/* fd table 메모리 해제 */
	palloc_free_multiple(curr->fdt, FDT_PAGES);

	/* 실행 중인 파일 닫기 */
	file_close(curr->running_f);
	process_cleanup();

	/* 자식 종료를 대기하는 부모 프로세스를 대기 상태에서 이탈되도록 */
	sema_up(&curr->wait_sema);
	/* 부모 프로세스가 자식을 리스트에서 지울 수 있도록 대기 */
	sema_down(&curr->exit_sema);
}

/* Free the current process's resources. */
static void
process_cleanup(void)
{
	struct thread *curr = thread_current();

#ifdef VM
	supplemental_page_table_kill(&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL)
	{
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate(NULL);
		pml4_destroy(pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void process_activate(struct thread *next)
{
	/* Activate thread's page tables. */
	pml4_activate(next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update(next);
}

/* Project(2) Process hierarchy */
struct thread *get_child_process(int pid)
{
	/* 자식 리스트에 접근하여 프로세스 디스크립터 검색 */
	/* 해당 pid가 존재하면 프로세스 디스크립터 반환 */
	/* 리스트에 존재하지 않으면 NULL 리턴 */
	struct thread *t = thread_current();
	struct list_elem *e = list_begin(&t->child_list);

	for (e; e != list_end(&t->child_list); e = list_next(e))
	{
		struct thread *t = list_entry(e, struct thread, child_elem);
		if (t->tid == pid)
			return t;
	}
	return NULL;
}

/* fdt에 파일을 추가하는 함수 */
int process_add_file(struct file *f)
{
	/* 파일 객체에 대한 파일 디스크립터 생성 */
	struct thread *t = thread_current();
	/* 파일 객체를 파일 디스크립터 테이블에 추가 */
	/* 파일 디스크립터의 최대값 1 증가 next_fd++ */
	/* 파일 디스크립터 반환 */
	struct file **fdt = t->fdt;
	int fd = t->next_fd;

	while (fd < FDTCOUNT_LIMIT && fdt[fd] != NULL) /* fdt의 빈자리 탐색 */
	{
		fd++;
	}

	if (fd >= FDTCOUNT_LIMIT) /* 예외처리 - FDT 꽉 찼을 경우 */
	{
		return -1;
	}

	t->next_fd = fd;
	t->fdt[fd] = f;

	return fd;
}

/* fdt에서 fd에 해당하는 파일 반환하는 함수 */
struct file *process_get_file(int fd)
{
	struct thread *t = thread_current();
	struct file **fdt = t->fdt;
	/* 프로세스의 파일 디스크립터 테이블을 검색하여 파일 객체의 주소를 반환, 없으면 NULL 반환 */

	if (fd >= FDTCOUNT_LIMIT || fd < 2)
	{
		return NULL;
	}

	return fdt[fd];
}

/* fdt에서 fd에 해당하는 index에 NULL을 할당해 파일과의 연결 끊기 */
void process_close_file(int fd)
{
	struct thread *t = thread_current();
	struct file **fdt = t->fdt;

	if (fd >= FDTCOUNT_LIMIT || fd < 2)
	{
		return NULL;
	}

	fdt[fd] = NULL;
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL 0			/* Ignore. */
#define PT_LOAD 1			/* Loadable segment. */
#define PT_DYNAMIC 2		/* Dynamic linking info. */
#define PT_INTERP 3			/* Name of dynamic loader. */
#define PT_NOTE 4			/* Auxiliary info. */
#define PT_SHLIB 5			/* Reserved. */
#define PT_PHDR 6			/* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr
{
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR
{
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack(struct intr_frame *if_);
static bool validate_segment(const struct Phdr *, struct file *);
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage,
						 uint32_t read_bytes, uint32_t zero_bytes,
						 bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load(const char *file_name, struct intr_frame *if_)
{
	struct thread *t = thread_current();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	// hex_dump(&file_ofs, )
	/* Allocate and activate page directory. */
	t->pml4 = pml4_create();
	if (t->pml4 == NULL)
		goto done;
	process_activate(thread_current());

	// lock_acquire(&filesys_lock);
	/* Open executable file. */
	file = filesys_open(file_name);
	if (file == NULL)
	{
		printf("load: %s: open failed\n", file_name);
		goto done;
	}

	/* Read and verify executable header. */
	if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr || memcmp(ehdr.e_ident, "\177ELF\2\1\1", 7) || ehdr.e_type != 2 || ehdr.e_machine != 0x3E // amd64
		|| ehdr.e_version != 1 || ehdr.e_phentsize != sizeof(struct Phdr) || ehdr.e_phnum > 1024)
	{
		printf("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++)
	{
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length(file))
			goto done;
		file_seek(file, file_ofs);

		if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type)
		{
		case PT_NULL:
		case PT_NOTE:
		case PT_PHDR:
		case PT_STACK:
		default:
			/* Ignore this segment. */
			break;
		case PT_DYNAMIC:
		case PT_INTERP:
		case PT_SHLIB:
			goto done;
		case PT_LOAD:
			if (validate_segment(&phdr, file))
			{
				bool writable = (phdr.p_flags & PF_W) != 0;
				uint64_t file_page = phdr.p_offset & ~PGMASK;
				uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
				uint64_t page_offset = phdr.p_vaddr & PGMASK;
				uint32_t read_bytes, zero_bytes;
				if (phdr.p_filesz > 0)
				{
					/* Normal segment.
					 * Read initial part from disk and zero the rest. */
					read_bytes = page_offset + phdr.p_filesz;
					zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) - read_bytes);
				}
				else
				{
					/* Entirely zero.
					 * Don't read anything from disk. */
					read_bytes = 0;
					zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
				}
				if (!load_segment(file, file_page, (void *)mem_page,
								  read_bytes, zero_bytes, writable))
					goto done;
			}
			else
				goto done;
			break;
		}
	}

	t->running_f = file;
	file_deny_write(file);

	/* Set up stack. */
	if (!setup_stack(if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	// file_close(file); /* load 하고 done, file_close해버리니까 주석 처리 */
	// lock_release(&filesys_lock);
	return success;
}

/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment(const struct Phdr *phdr, struct file *file)
{
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t)file_length(file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr((void *)phdr->p_vaddr))
		return false;
	if (!is_user_vaddr((void *)(phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page(void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment(struct file *file, off_t ofs, uint8_t *upage,
			 uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(upage) == 0);
	ASSERT(ofs % PGSIZE == 0);

	file_seek(file, ofs);
	while (read_bytes > 0 || zero_bytes > 0)
	{
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page(PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read(file, kpage, page_read_bytes) != (int)page_read_bytes)
		{
			palloc_free_page(kpage);
			return false;
		}
		memset(kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page(upage, kpage, writable))
		{
			printf("fail\n");
			palloc_free_page(kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack(struct intr_frame *if_)
{
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page(PAL_USER | PAL_ZERO);
	if (kpage != NULL)
	{
		success = install_page(((uint8_t *)USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page(kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page(void *upage, void *kpage, bool writable)
{
	struct thread *t = thread_current();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page(t->pml4, upage) == NULL && pml4_set_page(t->pml4, upage, kpage, writable));
}

#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

bool lazy_load_segment(struct page *page, void *aux)
{
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */

	struct lazy_load_arg *lazy_load_arg = (struct lazy_load_arg *)aux;

	/* file의 offset을 lazy_load_arg의 ofs으로 이동시킴(읽기 시작할 위치) */
	file_seek(lazy_load_arg->file, lazy_load_arg->ofs);

	/* 페이지에 매핑된 물리 메모리의 프레임에 이동시킨 ofs에서부터 파일의 데이터를 읽어옴 */
	/* 제대로 읽어오지 못 했다면 -> 해당 페이지를 Free 시키고 False 반환 */
	if (file_read(lazy_load_arg->file, page->frame->kva, lazy_load_arg->read_bytes) != (int)(lazy_load_arg->read_bytes))
	{
		palloc_free_page(page->frame->kva);
		return false;
	}
	/* 남는 부분은 0으로 초기화 */
	memset(page->frame->kva + lazy_load_arg->read_bytes, 0, lazy_load_arg->zero_bytes);

	return true;
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment(struct file *file, off_t ofs, uint8_t *upage,
			 uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(upage) == 0);
	ASSERT(ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0)
	{
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		/* lazy_load_segment에 인자로 전달해 줄 보조 인자를 담아주기 */
		struct lazy_load_arg *lazy_load_arg = (struct lazy_load_arg *)malloc(sizeof(struct lazy_load_arg));
		lazy_load_arg->file = file;
		lazy_load_arg->ofs = ofs;
		lazy_load_arg->read_bytes = page_read_bytes;
		lazy_load_arg->zero_bytes = page_zero_bytes;

		if (!vm_alloc_page_with_initializer(VM_ANON, upage,
											writable, lazy_load_segment, lazy_load_arg))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
		ofs += page_read_bytes;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
/* 프로세스가 실행될 때 load()에서 호출, STACK의 페이지를 생성하는 함수 */
static bool
setup_stack(struct intr_frame *if_)
{
	bool success = false;
	/* 스택은 아래로 커지니까, 스택 시작점 USER_STACK에서 PGSIZE만큼 아래로 내려간 지점에서 페이지 생성 = stack_bottom */
	void *stack_bottom = (void *)(((uint8_t *)USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */
	/* stack_bottm에 스택 매핑 후 페이지 요청 */
	/* 성공 시, rsp를 그에 맞게 설정 */
	/* 또한 페이지가 스택에 있음을 표시해야 함 */

	/* 1) stack_bottom에 페이지 할당 받음 (스택은 아래로 커지니까) */
	/* VM_MARKER_0: 스택이 저장된 메모리 페이지 식별 */
	if (vm_alloc_page(VM_ANON | VM_MARKER_0, stack_bottom, 1))
	{
		/* 할당받은 페이지에 물리 프레임 매핑 */
		success = vm_claim_page(stack_bottom);
		if (success)
		{
			/* rsp 변경 */
			if_->rsp = USER_STACK;
			thread_current()->stack_bottom = stack_bottom;
		}
	}

	return success;
}
#endif /* VM */
