#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdbool.h>

void syscall_init (void);
bool check_address(void *addr);

#endif /* userprog/syscall.h */
