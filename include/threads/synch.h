#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* A counting semaphore. */
struct semaphore
{
	unsigned value;		 /* Current value. */
	struct list waiters; /* List of waiting threads. */
};

void sema_init(struct semaphore *, unsigned value); /* 새로운 세마포어 구조체인 sema를 주어진 초기값으로 초기화 */
void sema_down(struct semaphore *);					/* "down" or "P" 연산을 sema에 실행. 세마의 값이 양수가 될 때까지 기다렸다가 양수가 되면 1만큼 빼게 된다. */
bool sema_try_down(struct semaphore *);				/* sema에 "down" or "P" 연산을 기다리지 않고 시도. 성공적으로 감소 시 true 리턴, 이미 0이었으면 false 리턴 */
void sema_up(struct semaphore *);					/* "up" or "V" 연산을 sema에 실행. 만약 기다리는 스레드가 있다면 그들 중 하나를 깨움(다른 동기화 함수들과 다르게 외부 인터럽트 핸들러 안에서 호출)*/
void sema_self_test(void);

/* Lock. - 초기값을 1로 갖는 세마포어 */
struct lock
{
	struct thread *holder;		/* Thread holding lock (for debugging). */
	struct semaphore semaphore; /* Binary semaphore controlling access. */
};

void lock_init(struct lock *);						   /* 새로운 lock 구조체 초기화 */
void lock_acquire(struct lock *);					   /* 현재 쓰레드에서 lock 획득. 현재의 lock owner가 lock을 놓아주기를 기다림 */
bool lock_try_acquire(struct lock *);				   /* 기다리지 않고 현재 쓰레드가 lock을 획득하도록 시도. 성공 여부 리턴 */
void lock_release(struct lock *);					   /* lock을 놓아준다. */
bool lock_held_by_current_thread(const struct lock *); /* running 상태의 스레드의 lock 소유 여부 */

/* Condition variable. */
struct condition
{
	struct list waiters; /* List of waiting threads. */
};

void cond_init(struct condition *);						/* cond를 새로운 컨디션 변수로 초기화 */
void cond_wait(struct condition *, struct lock *);		/* lock을 놓아주고, cond가 다른 코드로부터 신호 받길 기다림 */
														/* cond가 신호를 받으면 return 전에 lock을 다시 획득 (이 함수 콜하기 전 꼭 lock 갖고 있기) */
void cond_signal(struct condition *, struct lock *);	/* cond를 기다리는 스레드가 있다면, 기다리는 스레드 중 하나 깨움 (이 함수 콜하기 전 꼭 lock 갖고 있기) */
void cond_broadcast(struct condition *, struct lock *); /* cond를 기다리는 스레드가 있다면, 모든 스레드를 깨움 (이 함수 콜하기 전 꼭 lock 갖고 있기) */

bool cmp_condition(struct list_elem *a, struct list_elem *b, void *aux);
bool cmp_donation(struct list_elem *a, struct list_elem *b, void *aux);
void remove_donations(struct lock *lock);
void update_donate_priority(void);
/* Optimization barrier.
 *
 * The compiler will not reorder operations across an
 * optimization barrier.  See "Optimization Barriers" in the
 * reference guide for more information.*/
#define barrier() asm volatile("" : : : "memory")

#endif /* threads/synch.h */
