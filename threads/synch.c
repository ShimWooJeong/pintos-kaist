/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void sema_init(struct semaphore *sema, unsigned value)
{
	ASSERT(sema != NULL);
	sema->value = value;
	list_init(&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void sema_down(struct semaphore *sema)
{
	enum intr_level old_level;

	ASSERT(sema != NULL);
	ASSERT(!intr_context());

	old_level = intr_disable();
	while (sema->value == 0)
	{
		// list_push_back(&sema->waiters, &thread_current()->elem);
		list_insert_ordered(&sema->waiters, &thread_current()->elem, compare_priority, 0);
		thread_block();
	}
	sema->value--;
	intr_set_level(old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool sema_try_down(struct semaphore *sema)
{
	enum intr_level old_level;
	bool success;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level(old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void sema_up(struct semaphore *sema)
{
	enum intr_level old_level;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (!list_empty(&sema->waiters))
	{
		/* sema_up 하기 전에, donation이나 set_priority 등에 의해 priority가 변경됐을 수 있으니 waiters 정렬 한 번 해주기 */
		list_sort(&sema->waiters, compare_priority, 0);
		thread_unblock(list_entry(list_pop_front(&sema->waiters),
								  struct thread, elem));
	}
	sema->value++;
	/* sema의 value가 높아짐으로 인해 wait하던 thread가 가져가서 ready로 갔을 수 있으니 yield 호출 */
	thread_compare_yield();
	intr_set_level(old_level);
}

static void sema_test_helper(void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void sema_self_test(void)
{
	struct semaphore sema[2];
	int i;

	printf("Testing semaphores...");
	sema_init(&sema[0], 0);
	sema_init(&sema[1], 0);
	thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up(&sema[0]);
		sema_down(&sema[1]);
	}
	printf("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper(void *sema_)
{
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down(&sema[0]);
		sema_up(&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void lock_init(struct lock *lock)
{
	ASSERT(lock != NULL);

	lock->holder = NULL;
	sema_init(&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void lock_acquire(struct lock *lock)
{
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(!lock_held_by_current_thread(lock));

	struct thread *curr = thread_current();
	struct thread *lock_holder_t = lock->holder;

	if (!thread_mlfqs)
	{
		if (lock->holder != NULL)
		{
			curr->wait_on_lock = lock;
			list_insert_ordered(&lock_holder_t->donations, &curr->donation_elem, cmp_donation, 0);
			donate_priority(); /* advanced scheduler 사용할 때 우선순위 donation 금지 */
		}
	}

	sema_down(&lock->semaphore);
	curr->wait_on_lock = NULL;
	lock->holder = thread_current();
}

/* for nested donation
/* 하위에 연결된 모든 스레드에서 donation이 일어나기 때문에
/* wait_on_lock이 NULL이 아니라면, 해당 thread가 lock을 기다리고 있다는 뜻이니까
/* 해당 lock을 점유하고 있는 holder에게 priority를 기부하는 걸 깊이 8까지 반복
*/
void donate_priority(void)
{
	int depth;
	struct thread *cur = thread_current();

	for (depth = 0; depth < 8; depth++)
	{
		if (!cur->wait_on_lock)
			break;
		struct thread *holder = cur->wait_on_lock->holder;
		holder->priority = cur->priority;
		cur = holder;
	}
}

bool cmp_donation(struct list_elem *a, struct list_elem *b, void *aux UNUSED)
{
	return list_entry(a, struct thread, donation_elem)->priority > list_entry(b, struct thread, donation_elem)->priority;
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool lock_try_acquire(struct lock *lock)
{
	bool success;

	ASSERT(lock != NULL);
	ASSERT(!lock_held_by_current_thread(lock));

	success = sema_try_down(&lock->semaphore);
	if (success)
		lock->holder = thread_current();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void lock_release(struct lock *lock)
{
	ASSERT(lock != NULL);
	ASSERT(lock_held_by_current_thread(lock));

	if (!thread_mlfqs)
	{
		remove_donations(lock); /* donations에서 release할 lock을 기다리고 있는 thread 목록 제거 */
		update_donate_priority();
	}

	lock->holder = NULL;
	sema_up(&lock->semaphore);
}

void remove_donations(struct lock *lock)
{
	struct thread *lock_holder_t = lock->holder;

	struct list_elem *remove_elem = list_begin(&lock_holder_t->donations);

	while (remove_elem != list_end(&lock_holder_t->donations))
	{
		struct thread *remove_t = list_entry(remove_elem, struct thread, donation_elem);
		if (remove_t->wait_on_lock == lock)
		{
			remove_elem = list_remove(&remove_t->donation_elem);
		}
		else
		{
			remove_elem = list_next(remove_elem);
		}
	}
}

void update_donate_priority(void)
{
	struct thread *curr = thread_current();

	curr->priority = curr->origin_priority;
	/* 처음에 donations가 비어있다면 origin_priority로 갱신하고, 비어있지 않다면 donations 목록에서 가장 높은 우선순위로 갱신하는 코드로 짰는데 fail이 뜸
	/* 그래서 애초에 origin으로 바꾼 후에 비어있지 않다면 다시 갱신하는 방식으로 수정
	/* 이렇게 해야 하는 이유는 donations에 current의 original_priority보다 작은 priority를 가진 thread가 있는 경우를 고려하기 위함?
	*/

	if (!list_empty(&curr->donations))
	{
		list_sort(&curr->donations, cmp_donation, 0); /* donations에 있는 thread들의 priority가 변경됐을 수 있으니 재정렬 후, 가장 높은 우선순위 뽑아오기 */
		struct thread *donate_t = list_entry(list_begin(&curr->donations), struct thread, donation_elem);

		if (curr->priority < donate_t->priority)
		{
			curr->priority = donate_t->priority;
		}
	}
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool lock_held_by_current_thread(const struct lock *lock)
{
	ASSERT(lock != NULL);

	return lock->holder == thread_current();
}

/* One semaphore in a list. */
struct semaphore_elem
{
	struct list_elem elem;		/* List element. */
	struct semaphore semaphore; /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void cond_init(struct condition *cond)
{
	ASSERT(cond != NULL);

	list_init(&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void cond_wait(struct condition *cond, struct lock *lock)
{
	struct semaphore_elem waiter;

	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	sema_init(&waiter.semaphore, 0);
	// list_push_back(&cond->waiters, &waiter.elem);
	list_insert_ordered(&cond->waiters, &waiter.elem, cmp_condition, 0);
	lock_release(lock);
	sema_down(&waiter.semaphore);
	lock_acquire(lock);
}

bool cmp_condition(struct list_elem *a, struct list_elem *b, void *aux UNUSED)
{
	struct semaphore_elem *sa = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *sb = list_entry(b, struct semaphore_elem, elem);

	struct list *list_a = &(sa->semaphore.waiters);
	struct list *list_b = &(sb->semaphore.waiters);

	struct thread *a_thread = list_entry(list_begin(list_a), struct thread, elem);
	struct thread *b_thread = list_entry(list_begin(list_b), struct thread, elem);

	int priority_a = a_thread->priority;
	int priority_b = b_thread->priority;

	return priority_a > priority_b;
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_signal(struct condition *cond, struct lock *lock UNUSED)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	if (!list_empty(&cond->waiters))
		list_sort(&cond->waiters, cmp_condition, 0);
	sema_up(&list_entry(list_pop_front(&cond->waiters),
						struct semaphore_elem, elem)
				 ->semaphore);
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_broadcast(struct condition *cond, struct lock *lock)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);

	while (!list_empty(&cond->waiters))
		cond_signal(cond, lock);
}
