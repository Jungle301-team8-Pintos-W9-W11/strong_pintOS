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

// ✅ sleep queue 정의
static struct list sleep_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;	 /* # of timer ticks spent idle. */
static long long kernel_ticks; /* # of timer ticks in kernel threads. */
static long long user_ticks;	 /* # of timer ticks in user programs. */

static int64_t global_tick = INT64_MAX; // ✅ local tick 중 가장 작은 것, 초기 비교위해 최댓값
// static int64_t get_global_tick();
// static void set_global_tick(int64_t ticks);

/* Scheduling. */
#define TIME_SLICE 4					/* # of timer ticks to give each thread. */
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
//  main 함수에서 호출, thread system 시작, initial thread 생성
//  thread stack의 top에 Push
// magic??
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
	list_init(&sleep_list); // ✅ thread init 시 sleep list 추가
	list_init(&destruction_req);

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread();
	init_thread(initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid();
}

/* Starts preemptive thread scheduling by enabling interrupts.
	 Also creates the idle thread. */
//  IDLE Thread 생성
void thread_start(void)
{
	/* Create the idle thread. */
	struct semaphore idle_started; // idle thread로 설정?
	sema_init(&idle_started, 0);
	thread_create("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	intr_enable();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down(&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
	 Thus, this function runs in an external interrupt context. */
//  매 tick 마다 timer interrupt 호출,
void thread_tick(void)
{
	struct thread *t = thread_current();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++; // 유휴 상태 ticks 갱신
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++; // 🚨 Kernal_ticks??

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE) // thread_ticks = of timer ticks since last yield.b
		intr_yield_on_return();
}

/* Prints thread statistics. */
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
tid_t thread_create(const char *name, int priority,
										thread_func *function, void *aux)
{
	struct thread *t; // 새로운 스레드
	tid_t tid;				// 스레드 ID

	ASSERT(function != NULL);

	/* Allocate thread. */
	t = palloc_get_page(PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
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
	/* compare the priorities of the currently running thread and the newly inserted one.
	Yield the CPU if the newly arriving thread has higher priority*/

	// 새로 생성된 스레드와 현재 Running 중인 스레드를 비교
	// 만약 새로 생성된 스레드의 우선순위가 높다면
	// Ready List head와도 비교?
	// 현재 스레드를 Block
	// 새롭게 들어오는 스레드가 RUNNING?
	int curr_priority = thread_get_priority(); // 현재 running 중인 스레드의 우선순위

	thread_unblock(t);

	recheck_readyQueue();

	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
	 again until awoken by thread_unblock().

	 This function must be called with interrupts turned off.  It
	 is usually a better idea to use one of the synchronization
	 primitives in synch.h. */
void thread_block(void)
{
	ASSERT(!intr_context());
	ASSERT(intr_get_level() == INTR_OFF);

	thread_current()->status = THREAD_BLOCKED; // BLOCK 상태로 변경
	schedule();																 // 상태 변경 반영
}

// list_insert_order의 Less 함수
bool cmp_priority(struct list_elem *l, struct list_elem *s, void *aux UNUSED)
{
	struct thread *newly = list_entry(l, struct thread, elem);
	struct thread *list_elem = list_entry(s, struct thread, elem);

	return newly->priority > list_elem->priority;
}

/* Transitions a blocked thread T to the ready-to-run state.
	 This is an error if T is not blocked.  (Use thread_yield() to
	 make the running thread ready.)

	 This function does not preempt the running thread.  This can
	 be important: if the caller had disabled interrupts itself,
	 it may expect that it can atomically unblock a thread and
	 update other data. */
void thread_unblock(struct thread *t) // wakeup 역할?
{
	enum intr_level old_level;

	ASSERT(is_thread(t));

	old_level = intr_disable();
	ASSERT(t->status == THREAD_BLOCKED); // Block이 아니면 ASSERT
	// list_push_back(&ready_list, &t->elem); // Ready list tail로 인자로 받은 thread push
	list_insert_ordered(&ready_list, &t->elem, cmp_priority, NULL);
	t->status = THREAD_READY;	 // BLOCK -> READY
	intr_set_level(old_level); // disabled 되었던 인터럽트 다시 abled
}

/* Returns the name of the running thread. */
const char *
thread_name(void)
{
	return thread_current()->name;
}

/* Returns the running thread.
	 This is running_thread() plus a couple of sanity checks.
	 See the big comment at the top of thread.h for details. */
struct thread *
thread_current(void)
{
	struct thread *t = running_thread();
	// printf("current Thread: t=%p, t->tid=%d, t->status=%d\n", t, t->tid, t->status);

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
tid_t thread_tid(void)
{
	return thread_current()->tid;
}

/* Deschedules the current thread and destroys it.  Never
	 returns to the caller. */
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
void thread_yield(void)
{
	if (thread_current() != idle_thread)
	{
		struct thread *curr = thread_current();
		enum intr_level old_level;

		ASSERT(!intr_context());

		old_level = intr_disable();
		if (curr != idle_thread)
			// ready list에 정렬된 상태로 push
			list_insert_ordered(&ready_list, &curr->elem, cmp_priority, NULL);
		do_schedule(THREAD_READY);
		intr_set_level(old_level);
	}
}

// ✅ 현재 저장된 글로벌 tick 가져오기(getter)
int64_t get_global_tick()
{
	return global_tick;
}

// ✅ 더 작은 Local tick 등장시 갱신(setter)
void set_global_tick(int64_t ticks)
{
	if (ticks < global_tick)
	{
		global_tick = ticks;
	}
}

// ✅
void thread_sleep(int64_t ticks)
{
	// 🚨  ticks => 일어나야할 시간
	/* if the current thread is not idle thread,
	change the state of the caller thread to BLOCKED,
	store the local tick to wake up,
	update the global tick if necessary,
	and call schedule() */
	/* When you manipulate thread list, disable interrupt! */

	struct thread *curr = thread_current(); // 재울 현재 스레드
	enum intr_level old_level;							// 인터럽트

	old_level = intr_disable(); // 인터럽트 중단
	if (curr != idle_thread)
	{
		curr->wakeup_tick = ticks;								// local tick 저장
		list_push_back(&sleep_list, &curr->elem); // sleep list에 추가
		set_global_tick(ticks);										// 더 작은 tick인지 검사(global tick 갱신)
	}
	thread_block(); // BLOCK 상태 업데이트
	// 5. 인터럽트 다시 활성화
	intr_set_level(old_level);
}

// ✅ sleep 인 thread를 Ready Queue 의 tail로 push
void thread_wakeup(int64_t ticks) // timer inturrput가 발생한 시각(12시 5분)
{
	struct list_elem *curr = list_begin(&sleep_list); // sleep 리스트의 첫 원소
	struct thread *curr_thread;												// 쓰레드 저장용 변수
	// 1. 리스트 순회
	while (curr != list_end(&sleep_list))
	{
		curr_thread = list_entry(curr, struct thread, elem); // list elem thread 확인
		if (curr_thread->wakeup_tick <= ticks)
		{
			curr = list_remove(curr);		 // sleep queue 에서 제거
			thread_unblock(curr_thread); // thread unblock
		}
		else // else, local tick이 global tick 보다 작은 상황, 깨울 프로세스가 없음
		{
			set_global_tick(curr_thread->wakeup_tick);
			curr = list_next(curr); // 다음 요소 local tick 검사하러 이동
		}
	}
}

/*ReadyQueue 변동시 head 체크 함수*/
void recheck_readyQueue()
{
	int new_priority = thread_current()->priority;
	struct thread *first_elem = list_entry(list_begin(&ready_list), struct thread, elem);

	if (!list_empty(&ready_list) && first_elem->priority > new_priority)
	{
		thread_yield(); // 현재 CPU 점유중이던 스레드, CPU 자원 양보
	}
}

void refresh_priority()
{
	// struct thread *curr = thread_current();

	// // donation 리스트가 empty -> origin_PRI로 설정
	// if (list_empty(&curr->donations))
	// {
	// 	curr->priority = curr->origin_priority;
	// }
	// else
	// {
	// 	// donation 리스트가 존재 -> 남아있는 스레드 중 가장 높은 우선순위를 가져와야함
	// 	list_sort(&curr->donations, cmp_donate_priority, NULL); // 정렬 후 가장 맨 앞에 스레드 가져오기
	// 	struct thread *first_one = list_entry(list_front(&curr->donations), struct thread, d_elem);
	// 	if (first_one->priority > curr->priority)
	// 	{
	// 		// 맨 앞 스레드가 우선순위가 높다면 변동
	// 		curr->priority = first_one->priority;
	// 	}
	// }

	struct thread *curr = thread_current();
	int new_calculated_priority = curr->origin_priority; // 계산을 위한 임시 변수, 기본값은 원래 우선순위

	if (!list_empty(&curr->donations))
	{
		list_sort(&curr->donations, cmp_donate_priority, NULL);
		struct thread *highest_donor = list_entry(list_front(&curr->donations), struct thread, d_elem);

		if (highest_donor->priority > new_calculated_priority)
		{
			new_calculated_priority = highest_donor->priority;
		}
	}
	curr->priority = new_calculated_priority; // 최종 계산된 우선순위를 반영
}

/* Sets the current thread's priority to NEW_PRIORITY. */
// priority Donate시 우선순위 setting?
void thread_set_priority(int new_priority)
{
	struct thread *curr = thread_current();

	curr->origin_priority = new_priority; // 고유의 우선순위를 새롭게 설정하는 것!

	// running 중인 thread의 변경 발생
	refresh_priority();
	// list_sort...?
	list_sort(&ready_list, cmp_priority, NULL);
	recheck_readyQueue();
}

/* Returns the current thread's priority. */
int thread_get_priority(void)
{
	return thread_current()->priority;
}

/* Sets the current thread's nice value to NICE. */
void thread_set_nice(int nice UNUSED)
{
	/* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
int thread_get_nice(void)
{
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the system load average. */
int thread_get_load_avg(void)
{
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
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
	t->origin_priority = priority;

	for (int i = 0; i < 64; i++)
	{
		t->fdt[i] = NULL;
	}

	list_init(&t->donations); // donation 리스트 시작
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
