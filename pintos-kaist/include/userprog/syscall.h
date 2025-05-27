#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

void halt(void);
void exit(int status);
int write(int fd, const void *buffer, unsigned size);

// pid_t fork (const char *thread_name);
int exec (const char *file);
// int wait (pid_t pid);

int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
void seek (int fd, unsigned position);
void close (int fd);
unsigned tell (int fd);


#endif /* userprog/syscall.h */
