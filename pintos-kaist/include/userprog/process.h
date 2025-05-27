#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_create_initd(const char *file_name);
tid_t process_fork(const char *name, struct intr_frame *if_);
int process_exec(void *f_name);
int process_wait(tid_t);
void process_exit(void);
void process_activate(struct thread *next);

struct child *get_child_by_tid(tid_t tid);
struct child *init_child(tid_t tid);

struct fork_args
{
  struct thread *parent;
  struct intr_frame *pf;
  struct child *child_info;
};

#endif /* userprog/process.h */