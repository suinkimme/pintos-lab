#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/palloc.h"
#include "lib/user/syscall.h"
#include "userprog/process.h"
#include "lib/kernel/stdio.h"
#include "lib/kernel/hash.h"

#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/synch.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

bool check_address(void *addr);

//syscall 함수선언
void halt(void);
void exit(int status);
tid_t fork(const char *thread_name);
int exec (const char *cmd_line);
int wait(tid_t pid);
int write(int fd, const void *buffer, unsigned size);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file_name);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
void close(int fd);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */
#define FDT_MAX_SIZE 64 //최대 파일 디스크립터 수
#define FD_START 2 //0:stdin, 1:stdout

struct lock filesys_lock;

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

    // lock init
    lock_init(&filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
int sys_number = f->R.rax;

    // Argument 순서
    // %rdi %rsi %rdx %r10 %r8 %r9
    switch (sys_number) {
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
            f->R.rax = process_wait(f->R.rdi);
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
            exit(-1);
    }
}

bool check_address(void *addr)
{
    //커널주소인지 확인, pml4_get_page가 현재 프로세스 주소 공간에 매핑되어 있는지
    if(addr == NULL || is_kernel_vaddr(addr) || !pml4_get_page(thread_current()->pml4, addr)){
        return false;
    }
    return true;
}

void halt(void)
{
    power_off();
}

void exit (int status)
{
    struct thread *cur = thread_current();
    cur->exit_status = status;
    printf("%s: exit(%d)\n", cur->name, status);
    sema_up(&cur->exit_sema); //부모가 wait일 수 있으니 세마포어로 깨워줌
    thread_exit();
}

pid_t fork (const char *thread_name)
{

}

int exec (const char *cmd_line)
{
    //1. 주소 유효성 검사
    if(!check_address(cmd_line)){
        exit(-1);
    }

    //2. 복사본 생성
    char *cmd_line_copy;  

    //3. 페이지 할당 
    cmd_line_copy = palloc_get_page(0); 
    if(cmd_line_copy == NULL) //할당 실패시
        return -1;

    //4. 커널 페이지에 명령어 복사
    strlcpy(cmd_line_copy, cmd_line, PGSIZE);

    //5. 프로세스 교체
    int result = process_exec(cmd_line_copy);

    //6. 실패시 종료
    if (result = -1)
        return -1;
        
    NOT_REACHED(); 

    return -1; 
}

int wait (pid_t pid)
{
    return process_wait(pid);
}

bool create (const char *file, unsigned initial_size)
 {  
    bool success;
    if (!check_address(file)) {
        exit(-1);
    }
    // filesys.c 에 정의된 함수 사용
    lock_acquire(&filesys_lock);
    success = filesys_create(file, initial_size);
    lock_release(&filesys_lock);

    return success;
}

bool remove (const char *file)
{
    bool success;
    if (!check_address(file)) {
        exit(-1);
    }
    // filesys.c 에 정의된 함수 사용
    lock_acquire(&filesys_lock);
    success = filesys_remove(file);
    lock_release(&filesys_lock);

    return success;
}

int open (const char *file) {

    // 1. 유저 주소 유효성 검사
    if(!check_address(file)){
        exit(-1);
    }

    // 2. 파일 이름을 위한 커널 페이지 할당
    char *file_name_copy = palloc_get_page(0);
    if (file_name_copy == NULL)
        return -1;

    // 3. 파일 이름 복사
    strlcpy(file_name_copy, file, PGSIZE);

    // 4. 파일 열기
    struct file *file_ptr = filesys_open(file_name_copy);

    // 복사한 이름 페이지는 이제 필요 없으므로 해제
    palloc_free_page(file_name_copy);

    if (file_ptr == NULL)
        return -1;

    struct thread *curr = thread_current();

    // 5. fd 테이블이 없다면 새로 할당
    if (curr->fdt == NULL) {
        curr->fdt = palloc_get_page(PAL_ZERO);
        if (curr->fdt == NULL) {
            file_close(file_ptr);
            return -1;
        }
    }

    // 6. fd 번호 찾기 (2번부터 시작)
    for (int fd = 2; fd < FD_MAX; fd++) {
        if (curr->fdt[fd] == NULL) {
            curr->fdt[fd] = file_ptr;
            return fd;
        }
    }

    // 7. 빈 fd가 없으면 실패 처리
    file_close(file_ptr);
    return -1;
}

int filesize (int fd)
{
    struct thread *curr = thread_current();

     // 1. fd 유효성 검사
    if (fd < 2 || fd >= FD_MAX){
        exit(-1);
    }

    // 2. 파일 찾기
    if (curr->fdt[fd] == NULL){
        exit(-1);
    }

    struct file *file = curr->fdt[fd];

    return file_length(file);

}   

int read (int fd, void *buffer, unsigned size)
{
    if (!check_address(buffer)) {
        exit(-1);
    }


    if (fd == 0) {
        return input_getc();
    }

    struct thread *curr = thread_current();
    struct file *f = curr->fdt[fd];
    if (f == NULL) {
        return -1;
    }

    return file_read(f, buffer, size);
}

int write (int fd, const void *buffer, unsigned size)
{
    if (!check_address(buffer)) {
        exit(-1);
    }


    if (fd == 1)
    {
        putbuf(buffer, size);
        return size;
    }

    struct thread *curr = thread_current();
    struct file *f = curr->fdt[fd];
    if (f == NULL) {
        return -1;
    }

    return file_write(f, buffer, size);
}


void seek (int fd, unsigned position)
{
    struct thread *curr = thread_current();
  
    //1. fd 유효성 검사
    if (fd < 2 || fd >= FD_MAX) return;

    //2. 파일 찾기
    if (curr->fdt[fd] == NULL) return; 
    
    //3. 파일 덮어씌우기
    file_seek(curr->fdt[fd], position);
}

unsigned tell (int fd)
{
     struct thread *curr = thread_current();

     // 1. fd 유효성 검사
    if (fd < 2 || fd >= FD_MAX){
        exit(-1);
    }

    // 2. 파일 찾기
    if (curr->fdt[fd] == NULL){
        exit(-1);
    }

    struct file *file = curr->fdt[fd];

    return file_tell(file);
}

void close (int fd)
{
    struct thread *curr = thread_current();

     // 1. fd 유효성 검사
    if (fd < 2 || fd >= FD_MAX){
        exit(-1);
    }

    // 2. 파일 찾기
    if (curr->fdt[fd] == NULL){
        exit(-1);
    }

    //3. 파일 닫기
    file_close(curr->fdt[fd]); //curr->fdt[fd] 가 가리키는 파일을 free

    //4. fdt슬롯 비우기
    curr->fdt[fd] = NULL;
}



