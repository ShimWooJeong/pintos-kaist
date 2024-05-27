#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/file.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081			/* Segment selector msr */
#define MSR_LSTAR 0xc0000082		/* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void syscall_init(void)
{
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
							((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			  FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	lock_init(&filesys_lock);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
	/* 스택 포인터가 유저 영역인 지 확인 -> 저장된 인자 값이 포인터일 경우 유저 영역의 주소인 지 확인 */
	/* stack에서 syscall number get */
	int syscall_number = f->R.rax;

	/* 해당 syscall number에 해당하는 syscall 호출 */
	/* rax = system call number */
	/* 인자 순서 = %rdi, %rsi, %rdx, %r10, %r8, %r9 */
	switch (syscall_number)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	case SYS_FORK:
		f->R.rax = fork(f->R.rdi);
		break;
	case SYS_EXEC:
		f->R.rax = exec(f->R.rdi);
		break;
	case SYS_WAIT:
		f->R.rax = wait(f->R.rdi);
		break;
	case SYS_CREATE:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		break;
	case SYS_OPEN:
		f->R.rax = open(f->R.rdi);
		break;
	case SYS_FILESIZE:
		f->R.rax = filesize(f->R.rdi);
		break;
	case SYS_READ:
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK:
		seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL:
		f->R.rax = tell(f->R.rdi);
		break;
	case SYS_CLOSE:
		close(f->R.rdi);
		break;
	default:
		thread_exit();
		break;
	}

	// printf("system call!\n");
}

/* 주소 유효성 검사: 포인터가 가리키는 주소가 유저 영역인지 확인 */
/* 잘못된 접근일 경우 프로세스 종료(exit(-1)) */
/* block의 일부가 이런 영역들 중 하나인 블록을 가리키는 포인터라면 조건 추가해야 할 듯... 저게 먼데 */
void check_address(void *addr)
{
	if (!is_user_vaddr(addr) || addr == NULL || pml4_get_page(thread_current()->pml4, addr) == NULL)
	{
		exit(-1);
	}
}

void halt(void)
{
	/* power_off() 호출해 Pintos 종료 */
	power_off();
}

void exit(int status)
{
	/* 현재 동작중인 유저 프로그램 종료 */
	/* kernel에 상태를 return 하며 종료 */
	/* 부모 프로세스가 현재 유저 프로그램의 종료를 기다리던 중이라면 = 종료되면서 return될 그 상태를 기다림 */
	/* return 값 rax에 넣기 */
	/* status = 0 (성공), != 0 (error) */
	struct thread *t = thread_current();
	t->exit_status = status;
	printf("%s: exit(%d)\n", t->name, status);
	thread_exit();
}

pid_t fork(const char *thread_name)
{
	/* 현재 프로세스의 복제본인 새 프로세스 생성 */
	/* 마지막에 하기 */
}

int exec(const char *file)
{
	check_address(file);
	/* 현재 프로세스가 cmd_line에서 이름이 주어지는 실행 가능한 프로세스로 변경 */
	/* 이 때, 주어진 인자 전달 -> 성공 시 반환 x, 실패 시 exit state -1 반환 및 프로세스 종료 */
	/* 이 함수는 호출한 스레드의 이름은 바꾸지 않음 & file descriptor는 호출 시 열린 상태로 있다 */
}

int wait(pid_t pid)
{
	/* 마지막에 하기 */
}

bool create(const char *file, unsigned initial_size)
{
	check_address(file);
	/* file을 이름으로 하고, 크기가 initial_size인 새로운 파일 생성, 여는 건 X */
	/* 성공 시 true, 실패 시 false 반환 */
	return filesys_create(file, initial_size);
}

bool remove(const char *file)
{
	check_address(file);
	/* file이라는 이름을 가진 파일 삭제 */
	/* 성공 시 true, 실패 시 false 반환 */
	/* 파일이 열려있는지 닫혀있는지 여부와 관계없이 삭제될 수 있음 */
	return filesys_remove(file);
}

int open(const char *file)
{
	check_address(file);
	/* file이라는 이름을 가진 파일을 열기 */
	/* 성공 시, 파일 식별자로 불리는 비음수 정수(0 이상) 반환, 실패 시 -1 반환 */
	lock_acquire(&filesys_lock);
	struct file *open_f = filesys_open(file);

	if (open_f == NULL)
	{
		return -1;
	}

	int fd = process_add_file(open_f);

	if (fd == -1)
	{
		file_close(open_f);
		return -1;
	}
	lock_release(&filesys_lock);
	return fd;
}

int filesize(int fd)
{
	/* fd로서 열려 있는 파일의 크기가 몇 바이트인 지 반환 */
	struct file *open_f = process_get_file(fd);

	if (open_f == NULL)
	{
		return -1;
	}

	return file_length(open_f);
}

int read(int fd, void *buffer, unsigned size)
{
	check_address(buffer);
	int read_bytes;

	/* buffer 안에 fd로 열려있는 파일로부터 size 바이트를 읽고 읽어낸 바이트의 수를 반환 */
	/* 파일 끝에서 시도하면 0, 파일이 읽어질 수 없었다면 -1 반환 */
	if (fd == 0)
	{
		unsigned char *read_buf = buffer;
		/* 표준 입력, 키보드의 데이터를 읽어 버퍼에 저장 */
		for (read_bytes = 0; read_bytes <= size; read_bytes++)
		{
			char c = input_getc(); /* input_getc(): 키보드로부터 입력받은 문자 반환 함수  */
			*read_buf++ = c;	   /* 버퍼에 입력받은 문자 저장 */
			if (c == '/0')
			{
				break;
			}
		}
		return read_bytes;
	}
	else if (fd == 1) /* 표준 출력 */
	{
		return -1;
	}
	else
	{
		struct file *open_f = process_get_file(fd);
		lock_acquire(&filesys_lock);				  /* 파일에 동시 접근이 발생할 수 있기 때문에 lock 걸고 읽기 */
		read_bytes = file_read(open_f, buffer, size); /* 파일의 데이터를 size만큼 읽어 buffer에 저장 후 */
		lock_release(&filesys_lock);
	}

	return read_bytes; /* 읽은 바이트 수 return */
}

int write(int fd, const void *buffer, unsigned size)
{
	check_address(buffer);
	int write_bytes;

	/* buffer로부터 open file fd로 size 바이트를 적어줌 */
	if (fd == 0) /* 표준 입력 */
	{
		return -1;
	}
	else if (fd == 1)
	{
		/* 표준 출력, 버퍼에 저장된 값을 console에 출력 후 버퍼 크기 반환 */
		putbuf(buffer, size);
		return size;
	}
	else
	{
		/* 버퍼에 저장된 데이터를 크기만큼 파일에 기록 후 기록한 바이트 수 반환 */
		struct file *open_f = process_get_file(fd);
		lock_acquire(&filesys_lock);
		write_bytes = file_write(open_f, buffer, size);
		lock_release(&filesys_lock);
	}

	return write_bytes;
}

void seek(int fd, unsigned position)
{
}

unsigned
tell(int fd)
{
}

void close(int fd)
{
	struct file *close_file = process_get_file(fd);

	if (close_file == NULL)
	{
		return;
	}
	process_close_file(fd);
	file_close(close_file);
}
