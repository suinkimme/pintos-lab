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
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */

/// @brief 세마포어 자원 요청하고, 없으면 대기 상태로 전환
/// @param sema 요청할 세마포어 객체
void sema_down (struct semaphore *sema) 
{
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable (); // 인터럽트 비활성화

	// 세마포어 값이 0이면
	while (sema->value == 0) 
	{
		// waiters 리스트 뒤에 삽입
		// list_push_back (&sema->waiters, &thread_current ()->elem);

		list_insert_ordered(&sema->waiters, &thread_current()->elem, cmp_priority, NULL);  // +
		// 현재 스레드를 block 상태로 전환
    
		thread_block ();
	}

	// 세마포어 값 감소 (자원 획득)
	sema->value--;
	// 인터럽트 복원
	intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */

/// @brief 세마포어 값을 1 증가시키고, 대기중인 스레드가 있다면 선순위가 가장 높은 스레드를 깨우는 함수
/// @param sema 자원을 공유하는 세마포어 객체
void sema_up (struct semaphore *sema) 
{
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable (); // 인터럽트 비활성화

	if (!list_empty (&sema->waiters)) // 대기중인 스레드가 있다면
	{
		list_sort(&sema->waiters, cmp_priority, NULL); // ++ 리스트 재정렬
		thread_unblock (list_entry (list_pop_front (&sema->waiters), struct thread, elem)); // 리스트의 가장 앞 부분 스레드 깨움
	}

	sema->value++; // 세마포어 값을 증가시켜 자원 해제
	thread_maybe_yield(); 
	intr_set_level (old_level); // 인터럽트 활성화
}


static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
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
void 
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */

/// @brief lock을 획득할 때까지 대기하고 획득하면 소유자로 설정
/// @param lock 획득할 락
void lock_acquire (struct lock *lock) 
{
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	struct thread *cur_thread = thread_current();

	if (lock->holder) // 락을 즉시 획득할 수 없다면 (이미 다른 스레드가 보유 중이라면)
	{
		// 현재 스레드가 기다리고 있는 락 저장 
		cur_thread->waiting_lock = lock; 
		// 락의 소유자에게 우선순위 기부: donations 리스트에 현재 스레드를 삽입
		list_insert_ordered(&lock->holder->donations, &cur_thread->donation_elem, thread_cmp_donate_priority, NULL); 
		// 현재 락의 holder에게 우선순위 기부 
		// lock->holder->priority = cur_thread->priority;
		nested_donation();
	}

	// lock의 내부 세마포어를 down하여, lock을 획득할 수 있을 때까지 대기
	sema_down(&lock->semaphore);
	// 락 획득 성공 -> 기다리는 락 없음
	cur_thread->waiting_lock = NULL;
	// 세마포어 획득 후, 현재 스레드를 lock의 holder로 설정
	lock->holder = thread_current();
}

/// @brief 현재 스레드가 기다리고 있는 락을 따라가며 우선순위를 기부
/// @param  
void nested_donation(void)
{
	int depth;
	struct thread *cur_thread = thread_current(); 

	for (depth = 0; depth < 8; depth++) // 8단계 까지 중첩 기부 허용
	{
		if (!cur_thread->waiting_lock) // 기다리고 있는 락이 없다면
			break; // 중단
		
		// 현재 스레드가 기다리고 있는 락의 holder (우선순위 기부 대상)
		struct thread *holder = cur_thread->waiting_lock->holder; 	
		
		// 기부 대상의 우선순위가 현재 스레드보다 낮다면 기부 
		if (holder->priority < cur_thread->priority)
			holder->priority = cur_thread->priority; 
		
		// holder가 다른 락을 기다릴 수 있음
		cur_thread = holder;
	}
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool lock_try_acquire (struct lock *lock) 
{
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */

/// @brief 현재 락을 해제하고, 도네이션 우선순위를 정리한 뒤 대기 중인 스래드가 있다면 락을 넘김
/// @param lock 해제할 락
void lock_release (struct lock *lock) 
{
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	struct list_elem *e;
	struct thread *cur = thread_current();

	// donations 리스트 순회
	for (e = list_begin(&cur->donations); e != list_end(&cur->donations); e = list_next(e))
	{	
		struct thread *t = list_entry(e, struct thread, donation_elem);

		if (t->waiting_lock == lock) // 락이 있다면
			list_remove(&t->donation_elem); // 기부 회수
	}

	multiple_donation(); // 기부 정리

	lock->holder = NULL; // lock의 소유자를 NULL로 설정
	sema_up (&lock->semaphore); // lock 내부 세마포어를 up하여 다음 대기 중인 스레드에게 lock을 넘김
}

void multiple_donation(void)
{
	struct thread *cur = thread_current();

	// donations 리스트가 비었다면
	if (list_empty(&cur->donations))
	{
		// 우선순위 값을 original 우선순위로 복귀
		cur->priority = cur->original_priority;
	}
	// 비어있지 않다면
	else if (!list_empty(&cur->donations))
	{
		// 정렬 후 맨 앞의 값이 가장 크므로 list_sort -> front->priority 값으로 갱신
		// donations의 우선순위가 가장 높은 값으로 갱신
		list_sort(&cur->donations, thread_cmp_donate_priority, NULL);
		struct thread *front = list_entry(list_front(&cur->donations), struct thread, donation_elem);
		cur->priority = front->priority;
	}
}

bool thread_cmp_donate_priority (const struct list_elem *a, const struct list_elem *b, void *aux)
{
	return list_entry(a, struct thread, donation_elem)->priority >
			list_entry(b, struct thread, donation_elem)->priority;
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
	int priority; // semaphore에는 우선순위 정보가 없어 비교군이 없음
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
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

/// @brief 현재 락을 해제하고 cond가 signal될 때까지 대기, 이후 다시 락 획득
/// @param cond 기다릴 조건 변수
/// @param lock 반드시 호출 전에 보유 중이어야 하는 락
void cond_wait (struct condition *cond, struct lock *lock) 
{
	struct semaphore_elem waiter;
	
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);	// 조건 변수에서 대기할 개별 세마포어 초기화
	// list_push_back (&cond->waiters, &waiter.elem); // cond->waiters 리스트 뒤로 삽입 (FIFO)
	waiter.priority = thread_current()->priority;
	list_insert_ordered (&cond->waiters, &waiter.elem, cond_sema_priority, NULL); // +

	lock_release (lock); // 현재 보유중인 lock 해제
	sema_down (&waiter.semaphore); // 세마포어를 통해 block 상태로 진입
	lock_acquire (lock); // 깨어난 이후 다시 lock 획득
}

bool cond_sema_priority(const struct list_elem *a, const struct list_elem *b, void *aux)
{
	return list_entry (a, struct semaphore_elem, elem)->priority > list_entry (b, struct semaphore_elem, elem)->priority;
}
/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */

/// @brief 조건 변수에 대기 중인 스레드 중 우선순위가 가장 높은 스레드 깨우는 함수
/// @param cond signal을 받을 주건 변수
/// @param UNUSED 호출자가 소유하고 있는 락
void cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters)) // 조건 변수에 대기 중인 스레드가 있다면
	{
		// cond->waiters의 리스트를 우선순위 순으로 정렬
		list_sort(&cond->waiters, cond_sema_priority, NULL); // ++
		// 우선순위가 높은 대기자를 pop하고 해당 스레드 꺠움
		sema_up (&list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem)->semaphore);
	}
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
