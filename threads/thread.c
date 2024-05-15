#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;
static struct list sleep_list; //++sleep_list 구조체 선언
static struct list wait_list;  //++wait_list 구조체 선언

static int64_t global_t; //++global tick, sleep_list 중 가장 먼저 깨워야 할 스레드의 awake_t

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;   /* # of timer ticks spent idle. */
static long long kernel_ticks; /* # of timer ticks in kernel threads. */
static long long user_ticks;   /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4		  /* # of timer ticks to give each thread. */
static unsigned thread_ticks; /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread(thread_func *, void *aux);

static void idle(void *aux UNUSED);
static struct thread *next_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule(void);
static tid_t allocate_tid(void);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *)(pg_round_down(rrsp())))

// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = {0, 0x00af9a000000ffff, 0x00cf92000000ffff};

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
// 초기 stuct thread 초기화
void thread_init(void)
{
	ASSERT(intr_get_level() == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof(gdt) - 1,
		.address = (uint64_t)gdt};
	lgdt(&gdt_ds);

	/* Init the globla thread context */
	lock_init(&tid_lock);
	list_init(&ready_list);
	list_init(&sleep_list); //++sleep_list 구조체 초기화
	list_init(&wait_list);	//++wait_list 구조체 초기화
	list_init(&destruction_req);

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread();
	init_thread(initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void thread_start(void)
{
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init(&idle_started, 0);
	thread_create("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	intr_enable();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down(&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
// 각 타이머 tick에서 발생하는 타이머 인터럽트로부터 호출
// 스레드 통계 추적 & 타임 슬라이스 만료일 때 스케쥴러 작동
void thread_tick(void)
{
	struct thread *t = thread_current();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return();
}

// 매 시간마다 깨울 스레드를 찾는 건 비효율적 (깨울 스레드가 아무도 없을 수도 있잖아)
// 그래서 sleep된 스레드들 중 가장 먼저 일어나야 할 스레드의 시간을 구하는 함수 추가
// 갱신은 언제 하냐? = sleep_list에 담긴 스레드들의 wakeup_time 기준이니까 -> 재울 때(thread_sleep) & 깨울 때(thread_awake)
// 근데 재울 때는 그냥 현재 global이랑 새로 재울 애 비교만 하고 갱신
// 깨울 때는 해당 스레드가 sleep에서 빠지니까, 남은 sleep_list 순회해서 가장 작은 애로 갱신
// -> (근데 어차피 awake 할 때 순회하니까 그 때 global_tick 갱신해도 되는 거 아님??)
void update_global_tick(int64_t ticks)
{
	global_t = (global_t > ticks) ? ticks : global_t; // global_t가 더 크면 ticks로, 아니면 global_t로
}

// 가장 먼저 일어나야 할 스레드가 일어날 시간 = global tick 반환
// global_tick은 언제 쓰냐? = 가장 먼저 일어날 스레드의 시간인 거니까, 깨울 스레드를 고를 때! -> 즉, timer_interrupt에서
int64_t get_global_tick(void)
{
	return global_t;
}

/* Prints thread statistics. */
// 스레드 통계 출력
void thread_print_stats(void)
{
	printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
		   idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
// 새 스레드 생성 후 tid 반환
// name: 새 스레드 이름
// priority: 우선순위
// 함수의 단일 인자 aux 전달 해 function 실행함
tid_t thread_create(const char *name, int priority,
					thread_func *function, void *aux)
{
	struct thread *t;
	tid_t tid;

	ASSERT(function != NULL);
	/* Allocate thread. */
	t = palloc_get_page(PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	// create된 thread->status = THREAD_BLOCKED
	init_thread(t, name, priority);
	tid = t->tid = allocate_tid();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t)kernel_thread;
	t->tf.R.rdi = (uint64_t)function;
	t->tf.R.rsi = (uint64_t)aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* Add to run queue. */
	thread_unblock(t);
	running_compare_yield();

	return tid;
}
//++스레드 sleep/awake 함수 추가
void thread_sleep(int64_t ticks)
{
	// 스레드 상태 blocked로 설정하기
	// sleep_list에 추가하기
	enum intr_level old_level; // 인터럽트 상태 저장
	struct thread *current;	   // 현재 스레드
	current = thread_current();

	ASSERT(idle_thread != current); // idle_thread는 sleep 되면 안 됨
	old_level = intr_disable();		// 인터럽트 off -> 상호배제 막기 위해

	current->wakeup_t = ticks;												  // 일어날 시간 저장
	global_t = (global_t > current->wakeup_t) ? current->wakeup_t : global_t; //// wakeup_t랑 비교해서 global_t 갱신
	list_push_back(&sleep_list, &current->elem);							  // sleep_list에 현재 스레드 삽입
	thread_block();															  // 스레드 상태 blocked 설정 -> thread_block 함수 내에서 schedule() 호출함
	intr_set_level(old_level);												  // 인터럽트 상태 돌려줌
}

void thread_awake(int64_t ticks)
{
	// 스레드 상태 ready로 설정하기 -> unblock 함수 호출 시 ready로 상태 변경해줌
	// ready_list에 추가하기
	struct list_elem *search;

	search = list_begin(&sleep_list); // 리스트의 첫 요소

	while (search != list_end(&sleep_list)) // sleep_list 끝날 때까지 while로 탐색
	{
		struct thread *ready_thread = list_entry(search, struct thread, elem); // search 요소에 있는 thread
		// elem은 앞에 struct thread의 list_elem 이 elem

		if (ticks >= ready_thread->wakeup_t) // 일어날 시간 됐는지 확인(현재 시간이 일어날 시간보다 클 때)
		{
			search = list_remove(search); // 해당 스레드 sleep_list에서 제거 //search
			thread_unblock(ready_thread); // 스레드 unblock(상태도 변경해주고 ready_list에도 넣어줌)
										  // running_compare_yield(); // 왜 깨우고 나서 ready에 새로 들어갈 때 compare하면 안 되는거지 왜??????
		}
		else
		{
			search = list_next(search);					// 일어날 시간 아니라면 리스트의 다음 탐색
			update_global_tick(ready_thread->wakeup_t); // wakeup_t랑 비교해서 global_t 갱신
		}
	}
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
// 실행되고 있는 스레드를 실행 상태 -> blocked 상태로 전환
// 이렇게 전환된 스레드는 unblock이 호출되기 전까지 다시 동작하지 않음
// 너무 로우 레벨이라 이 함수 대신 동기화 기초요소 중 하나를 쓰는 게 낫다 함
void thread_block(void)
{
	ASSERT(!intr_context());
	ASSERT(intr_get_level() == INTR_OFF);
	thread_current()->status = THREAD_BLOCKED;
	schedule();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
// blocked된 상태의 스레드를 ready 상태로 전환 (= 다시 실행되도록 허가)
// 스레드가 기다리던 이벤트가 발생했을 때 호출 (Ex. 스레드가 대기하고 있던 락이 사용가능하게 된다던지)
void thread_unblock(struct thread *t)
{
	enum intr_level old_level;

	ASSERT(is_thread(t));
	old_level = intr_disable();
	ASSERT(t->status == THREAD_BLOCKED);
	// list_push_back(&ready_list, &t->elem);
	// ready_list에 input하는 unblock&yeild에서 push_back이 아닌 insert_ordered로
	//++우선순위 순으로 삽입 시 alarm_priority 테스트 케이스 passed
	list_insert_ordered(&ready_list, &t->elem, compare_priority, 0);
	t->status = THREAD_READY;
	intr_set_level(old_level);
}

bool compare_priority(struct list_elem *a, struct list_elem *b, void *aux UNUSED)
{
	return list_entry(a, struct thread, elem)->priority > list_entry(b, struct thread, elem)->priority;
	//++ 우선순위 비교해주는 함수 (list_insert_ordered에 인자로 넣어줌)
	// a가 더 크면 true, b가 더 크면 false 반환
}

/* Returns the name of the running thread. */
const char *
// 현재 실행중인 스레드의 name 반환
thread_name(void)
{
	return thread_current()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
// 현재 실행중인 스레드 반환
thread_current(void)
{
	struct thread *t = running_thread();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT(is_thread(t));
	ASSERT(t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
// 현재 실행중인 스레드의 tid 반환
tid_t thread_tid(void)
{
	return thread_current()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
// 현재 스레드가 종료되도록 함 (반환 x)
void thread_exit(void)
{
	ASSERT(!intr_context());

#ifdef USERPROG
	process_exit();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable();
	do_schedule(THREAD_DYING);
	NOT_REACHED();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
// 실행할 새 스레드를 선택하는 스케쥴러에게 CPU 제공
// 선택되는 새 스레드가 현재 스레드일 수도 있기 때문에,
// 특정 시간동안 스레드 실행상태를 유지하려 할 때, 이 함수에 의존x
// => 그니까 현재 스레드를 ready_list에 담아서 스케쥴러에게 선택을 맡기는(do_schedule) 느낌
void thread_yield(void)
{
	struct thread *curr = thread_current();
	enum intr_level old_level;

	ASSERT(!intr_context());

	old_level = intr_disable(); // 인터럽트 비활성화하고, 이전 인터럽트 상태로 되돌려줌
	if (curr != idle_thread)
	{
		// list_push_back(&ready_list, &curr->elem); // ready_list의 마지막에 insert
		//++ready_list에 넣을 때 우선순위로 정렬되도록 삽입 후, 실행 중인 스레드와 우선순위 비교
		list_insert_ordered(&ready_list, &curr->elem, compare_priority, 0);
	}
	do_schedule(THREAD_READY); // context switch
	intr_set_level(old_level); // interrupt 상태를 parameter로 전달한 상태로 설정 & 이전 interrupt 상태 반환
}

void running_compare_yield(void)
{
	//++실행 중인 애랑 우선순위 비교해서 yield 할지 말지
	// 이 함수는 그니까 running인 애의 priority가 변경됐을 때 호출해서 쓰는 건가??
	// 그래서 set_priority랑 create 할 때만 호출하는 거지
	// create 할 때 일단 running으로 가서 원래 사용하던 애가 ready로 빠짐?? -> 다시 yield 통해서 schedule 때려줘야 함??
	// 의문점 1) thread_create 할 때 running으로 먼저 가냐, ready로 먼저 가냐??
	// 의문점 2) ready_list에 새 스레드가 들어올 때, 실행 중인 current_thread랑 비교해서 만약 들어온 thread가 우선순위가 더 높으면, yield 해서 context switching이 발생해야 하는 게 아닌가?
	//			근데 왜 create에만 비교해서 yield를 해야 할까??
	//			thread_create해서 ready로 새 스레드가 들어오든 & awake해서 ready로 새 스레드가 들어오든 두 경우 모두 compare 해야 하는 것이 아닌가??
	if (!list_empty(&ready_list) && thread_current()->priority < list_entry(list_begin(&ready_list), struct thread, elem)->priority)
	{
		thread_yield();
	}
}

/* Sets the current thread's priority to NEW_PRIORITY. */
// 스레드의 우선순위를 설정
void thread_set_priority(int new_priority)
{
	thread_current()->priority = new_priority;
	//++우선 순위가 바뀌었다면, 실행 중인 애와 준비 중인 애의 우선순위 비교해 yield 될 수 있도록
	running_compare_yield();
}

/* Returns the current thread's priority. */
// 스레드의 우선순위를 가져옴
int thread_get_priority(void)
{
	return thread_current()->priority;
}

/* Sets the current thread's nice value to NICE. */
// 고급 스케쥴러를 위한 stubs(토막들)
void thread_set_nice(int nice UNUSED)
{
	/* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
// 고급 스케쥴러를 위한 stubs(토막들)
int thread_get_nice(void)
{
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the system load average. */
// 고급 스케쥴러를 위한 stubs(토막들)
int thread_get_load_avg(void)
{
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
// 고급 스케쥴러를 위한 stubs(토막들)
int thread_get_recent_cpu(void)
{
	/* TODO: Your implementation goes here */
	return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle(void *idle_started_ UNUSED)
{
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current();
	sema_up(idle_started);

	for (;;)
	{
		/* Let someone else run. */
		intr_disable();
		thread_block();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread(thread_func *function, void *aux)
{
	ASSERT(function != NULL);

	intr_enable(); /* The scheduler runs with interrupts off. */
	function(aux); /* Execute the thread function. */
	thread_exit(); /* If function() returns, kill the thread. */
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread(struct thread *t, const char *name, int priority)
{
	ASSERT(t != NULL);
	ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT(name != NULL);

	memset(t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy(t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t)t + PGSIZE - sizeof(void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run(void)
{
	if (list_empty(&ready_list))
		return idle_thread;
	else
		return list_entry(list_pop_front(&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void do_iret(struct intr_frame *tf)
{
	__asm __volatile(
		"movq %0, %%rsp\n"
		"movq 0(%%rsp),%%r15\n"
		"movq 8(%%rsp),%%r14\n"
		"movq 16(%%rsp),%%r13\n"
		"movq 24(%%rsp),%%r12\n"
		"movq 32(%%rsp),%%r11\n"
		"movq 40(%%rsp),%%r10\n"
		"movq 48(%%rsp),%%r9\n"
		"movq 56(%%rsp),%%r8\n"
		"movq 64(%%rsp),%%rsi\n"
		"movq 72(%%rsp),%%rdi\n"
		"movq 80(%%rsp),%%rbp\n"
		"movq 88(%%rsp),%%rdx\n"
		"movq 96(%%rsp),%%rcx\n"
		"movq 104(%%rsp),%%rbx\n"
		"movq 112(%%rsp),%%rax\n"
		"addq $120,%%rsp\n"
		"movw 8(%%rsp),%%ds\n"
		"movw (%%rsp),%%es\n"
		"addq $32, %%rsp\n"
		"iretq"
		: : "g"((uint64_t)tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch(struct thread *th)
{
	uint64_t tf_cur = (uint64_t)&running_thread()->tf;
	uint64_t tf = (uint64_t)&th->tf;
	ASSERT(intr_get_level() == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile(
		/* Store registers that will be used. */
		"push %%rax\n"
		"push %%rbx\n"
		"push %%rcx\n"
		/* Fetch input once */
		"movq %0, %%rax\n"
		"movq %1, %%rcx\n"
		"movq %%r15, 0(%%rax)\n"
		"movq %%r14, 8(%%rax)\n"
		"movq %%r13, 16(%%rax)\n"
		"movq %%r12, 24(%%rax)\n"
		"movq %%r11, 32(%%rax)\n"
		"movq %%r10, 40(%%rax)\n"
		"movq %%r9, 48(%%rax)\n"
		"movq %%r8, 56(%%rax)\n"
		"movq %%rsi, 64(%%rax)\n"
		"movq %%rdi, 72(%%rax)\n"
		"movq %%rbp, 80(%%rax)\n"
		"movq %%rdx, 88(%%rax)\n"
		"pop %%rbx\n" // Saved rcx
		"movq %%rbx, 96(%%rax)\n"
		"pop %%rbx\n" // Saved rbx
		"movq %%rbx, 104(%%rax)\n"
		"pop %%rbx\n" // Saved rax
		"movq %%rbx, 112(%%rax)\n"
		"addq $120, %%rax\n"
		"movw %%es, (%%rax)\n"
		"movw %%ds, 8(%%rax)\n"
		"addq $32, %%rax\n"
		"call __next\n" // read the current rip.
		"__next:\n"
		"pop %%rbx\n"
		"addq $(out_iret -  __next), %%rbx\n"
		"movq %%rbx, 0(%%rax)\n" // rip
		"movw %%cs, 8(%%rax)\n"	 // cs
		"pushfq\n"
		"popq %%rbx\n"
		"mov %%rbx, 16(%%rax)\n" // eflags
		"mov %%rsp, 24(%%rax)\n" // rsp
		"movw %%ss, 32(%%rax)\n"
		"mov %%rcx, %%rdi\n"
		"call do_iret\n"
		"out_iret:\n"
		: : "g"(tf_cur), "g"(tf) : "memory");
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status)
{
	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(thread_current()->status == THREAD_RUNNING);
	while (!list_empty(&destruction_req))
	{
		struct thread *victim =
			list_entry(list_pop_front(&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current()->status = status;
	schedule();
}

static void
schedule(void)
{
	struct thread *curr = running_thread();
	struct thread *next = next_thread_to_run();

	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(curr->status != THREAD_RUNNING);
	ASSERT(is_thread(next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate(next);
#endif

	if (curr != next)
	{
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread)
		{
			ASSERT(curr != next);
			list_push_back(&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch(next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid(void)
{
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire(&tid_lock);
	tid = next_tid++;
	lock_release(&tid_lock);

	return tid;
}
