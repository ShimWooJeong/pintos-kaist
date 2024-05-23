#ifndef THREADS_INTERRUPT_H
#define THREADS_INTERRUPT_H

#include <stdbool.h>
#include <stdint.h>

/* Interrupts on or off? */
// Intr_level이 가질 수 있는 값 = INTR_OFF, INTR_ON
enum intr_level
{
	INTR_OFF, /* 인터럽트 비활성화 상태 Interrupts disabled. */
	INTR_ON	  /* 인터럽트 활성화 상태 Interrupts enabled. */
};

enum intr_level intr_get_level(void);			 // 현재 인터럽트 상태 반환
enum intr_level intr_set_level(enum intr_level); // 현재 상태에 따라 인터럽트를 활성화/비활성화 하는 함수, 인터럽트의 이전 상태 반환
enum intr_level intr_enable(void);				 // 인터럽트 활성화, 인터럽트의 이전 상태 반환
enum intr_level intr_disable(void);				 // 인터럽트 비활성화, 인터럽트의 이전 상태 반환

/* Interrupt stack frame. */
struct gp_registers
{
	uint64_t r15;
	uint64_t r14;
	uint64_t r13;
	uint64_t r12;
	uint64_t r11;
	uint64_t r10;
	uint64_t r9;
	uint64_t r8;
	uint64_t rsi; /* 인덱스 레지스터 */
	uint64_t rdi; /* 인덱스 레지스터 */
	uint64_t rbp; /* 스택 메모리 시작 주소 */
	uint64_t rdx; /* 범용 레지스터 */
	uint64_t rcx; /* 범용 레지스터 */
	uint64_t rbx; /* 범용 레지스터 */
	uint64_t rax; /* 범용 레지스터 */
} __attribute__((packed));

struct intr_frame
{
	/* Pushed by intr_entry in intr-stubs.S.
	   These are the interrupted task's saved registers. */
	struct gp_registers R; /* 범용 레지스터 = 120byte */
	uint16_t es;
	uint16_t __pad1;
	uint32_t __pad2;
	uint16_t ds; /* 세그먼트 관리 */
	uint16_t __pad3;
	uint32_t __pad4;
	/* Pushed by intrNN_stub in intr-stubs.S. */
	uint64_t vec_no; /* Interrupt vector number. */
					 /* Sometimes pushed by the CPU,
						otherwise for consistency pushed as 0 by intrNN_stub.
						The CPU puts it just under `eip', but we move it here. */
	uint64_t error_code;
	/* Pushed by the CPU.
	   These are the interrupted task's saved registers. */
	uintptr_t rip; /* Program Counter */
	uint16_t cs;   /* 세그먼트 관리 */
	uint16_t __pad5;
	uint32_t __pad6;
	uint64_t eflags; /* cpu 상태 나타내는 정보 */
	uintptr_t rsp;	 /* Stack Pointer */
	uint16_t ss;
	uint16_t __pad7;
	uint32_t __pad8;
} __attribute__((packed));

typedef void intr_handler_func(struct intr_frame *);

void intr_init(void);
void intr_register_ext(uint8_t vec, intr_handler_func *, const char *name);
void intr_register_int(uint8_t vec, int dpl, enum intr_level,
					   intr_handler_func *, const char *name);
bool intr_context(void);
void intr_yield_on_return(void);

void intr_dump_frame(const struct intr_frame *);
const char *intr_name(uint8_t vec);

#endif /* threads/interrupt.h */
