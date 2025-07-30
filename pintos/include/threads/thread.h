#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H
#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/synch.h"
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif

/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */
#define FDPAGES 3
#define FDCOUNT_LIMIT FDPAGES * (1 << 9) // 페이지 크기 4kb / 파일 포인터 8바이트 = 512
#define FD_MAX 64

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
	/* Owned by thread.c. */
	
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */

	int original_priority; 		  // 기부 받기 전 우선 순위
	struct lock *waiting_lock;    // 대기 중인 락
	struct list donations;        // 기부 받은 리스트들
	struct list_elem donation_elem; // 기부자로 들어갈때 쓰는 연결점

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */
	int64_t wakeup_tick; 				// tick이 되면 깨어나야 함

	/* userprog thread field*/
	struct file *runn_file;

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                    /* Page map level 4 */
	int exit_status; //자식의 종료 상태
	
	struct list child_list;
	struct list_elem child_elem;      // 부모의 child_list에 들어갈 때 사용하는 연결 노드

	struct file **fdt; // file descriptor table
	int next_fd; // 다음에 사용할 file descriptor 번호

	struct semaphore load_sema; // 프로세스 로드 완료를 기다리는 세마포어
	struct semaphore exit_sema; //부모와 동기화용 세마포어
	struct semaphore wait_sema; // 자식이 종료되면 부모가 기다릴 세마포어

	struct file *running; // 현재 실행 중인 파일

 
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

/* 자식 프로세스 구조체 */
struct child_status {
	tid_t child_tid; // 자식 TID
	int exit_status; // 자식 종료 코드
	bool has_been_waited; // 부모가 wait()을 했는지 확인하는
	bool is_exited; // 자식이 종료됐는지 여부 (이 원소가)
	struct semaphore wait_sema; // 부모가 자식을 기다릴 때 사용하는 세마포어
	struct list_elem elem; // 부모의 child_list에 연결될 리스트 노드
	};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

bool cmp_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);
bool cmp_priority(const struct list_elem *a, const struct list_elem *b, void *aux);
void do_iret (struct intr_frame *tf);

#endif /* threads/thread.h */
