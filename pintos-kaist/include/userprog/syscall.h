#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init(void);
#include "threads/synch.h" // lock

struct lock filesys_lock;
#endif /* userprog/syscall.h */
