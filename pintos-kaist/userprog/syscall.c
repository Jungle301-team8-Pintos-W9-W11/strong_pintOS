#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "userprog/process.h"
#include "threads/palloc.h"

typedef int pid_t;
struct lock filesys_lock;

/* 함수 원형 선언 */
void halt(void);
void exit(int status);
pid_t fork (const char *thread_name, struct intr_frame *f);
int exec(const char *cmd_line);
int wait(pid_t pid);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
int dup2(int oldfd, int newfd);
bool check_addr(const void *addr);


void syscall_entry (void);
void syscall_handler (struct intr_frame *);

bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
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

	lock_init(&filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	switch(f->R.rax){
		case 0:
			// HALT
			halt();
			break;
		case 1:
			// EXIT
			exit(f->R.rdi);
			break;
		case 2:
			// FORK
			f->R.rax = fork(f->R.rdi, f);
			break;
		case 3:
			// EXEC
			f->R.rax = exec(f->R.rdi);
			break;
		case 4:
			// WAIT
			f->R.rax = wait(f->R.rdi);
			break;
		case 5:
			// CREATE
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		case 6:
			// REMOVE
			f->R.rax = remove(f->R.rdi);
			break;
		case 7:
			// OPEN
			f->R.rax = open(f->R.rdi);
			break;
		case 8:
			// FILESIZE
			f->R.rax = filesize(f->R.rdi);
			break;
		case 9:
			// READ
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case 10:
			// WRITE
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case 11:
			// SEEK
			seek(f->R.rdi, f->R.rsi);
			break;
		case 12:
			// TELL
			f->R.rax = tell(f->R.rdi);
			break;
		case 13:
			// CLOSE
			close(f->R.rdi);
			break;
		case 14:
			// MMAP
			break;
		case 15:
			// MUNMAP
			break;
		case 16:
			// CHDIR
			break;
		case 17:
			// MKDIR
			break;
		case 18:
			// READDIR
			break;
		case 19:
			// ISDIR
			break;
		case 20:
			// INUMBER
			break;
		case 21:
			// SYMLINK
			break;
		case 22:
			// DUP2
			break;
		case 23:
			// MOUNT
			break;
		case 24:
			// UMOUNT
			break;
		default:
			// Unknown system call
			break;
	}



	// printf ("system call!\n");
	// thread_exit ();
}


void halt(void){
	power_off(); 
}

void exit(int status){ // !
	struct thread *cur = thread_current();
	cur->exit_status = status; 
	printf("%s: exit(%d)\n", cur->name, status);
	if(cur->running_file){
			file_allow_write(cur->running_file);
	}
	thread_exit();
	
}

pid_t fork (const char *thread_name, struct intr_frame *f){ // !??????


	if(!check_addr(thread_name)){
		exit(-1);
	}
	return process_fork(thread_name, f);
}

// int exec(const char *cmd_line) {
//     char *cmd_copy = palloc_get_page(0);
//     if (cmd_copy == NULL){
		
// 		return -1;
// 	}
        
//     strlcpy(cmd_copy, cmd_line, PGSIZE);

//     int result = process_exec(cmd_copy);
//     palloc_free_page(cmd_copy);

//     return result;
// }

int exec (const char *cmd_line){
	if(!check_addr(cmd_line)){
	
		exit(-1);
	}

	char *fn_copy = palloc_get_page(PAL_ZERO);
	
	if (fn_copy == NULL)// 메모리 할당 불가 시
		return -1;

	strlcpy(fn_copy, cmd_line, PGSIZE);

	if (process_exec(fn_copy) == -1) // [process_exec] 'load (file_name, &_if);' -> load 실패 시
		return -1;
	
	return 0;
}

int wait (pid_t pid){ // !
	return process_wait(pid);
}

bool create (const char *file, unsigned initial_size){

	if(!check_addr(file)){

		exit(-1);
	}
	lock_acquire(&filesys_lock);
	bool res = filesys_create(file, initial_size);
	lock_release(&filesys_lock);
	return res;
}

bool remove (const char *file){
	lock_acquire(&filesys_lock);
	bool res = filesys_remove(file);
	lock_release(&filesys_lock);
	return res;
}

int open (const char *file){
	
	if(!check_addr(file)){
		exit(-1);
	}
	lock_acquire(&filesys_lock);
	struct file* file_1 = filesys_open(file);
	lock_release(&filesys_lock);
	struct thread *curr = thread_current();

	if(file_1 == NULL){
		return -1;
	}
	else{
		for(int i = 2; i < 64; i++){
			if(curr->fdt[i] == NULL){
				// 넣어주고
				curr->fdt[i] = file_1;
				return i;
			}

		}
	}
}

int filesize (int fd){
	if(fd<2 || fd > 63){

		exit(-1);
	} 
	struct thread* cur = thread_current();
	struct file *file = cur->fdt[fd];
	if(file == NULL){
		exit(-1);
	}
	lock_acquire(&filesys_lock);
	off_t res = file_length(file);
	lock_release(&filesys_lock);
	return res;
}

int read (int fd, void *buffer, unsigned size){
	
	if(!check_addr(buffer)){
		
		exit(-1);
	}


	if(fd == 0){
		for(int i = 0; i < size; i++){
			input_getc();	
		}
		
	}
	else if(fd == 1){
		
		exit(-1);
	}
	else if(fd < 1 || fd > 63){
		
		exit(-1);
	}
	else{
		struct thread *curr = thread_current();
		struct file *file = curr->fdt[fd];
		if(file == NULL){
			
			exit(-1);
		}
		lock_acquire(&filesys_lock);
		off_t res = file_read(file, buffer, size);
		lock_release(&filesys_lock);
		return res;
	}
	return -1;
}

int write(int fd, const void *buffer, unsigned size){
	if(!check_addr(buffer)){
	
		exit(-1);
	}

	if(fd==1){
		putbuf(buffer, size);
		return size;
	}
	else if ( fd<2 || fd>63 )
	{
		exit(-1);
	}
	else{
		struct thread *curr = thread_current();
		struct file *file = curr->fdt[fd];
		if(file == NULL){
			exit(-1);
		}
		if(curr->running_file == file){
			return 0;
		}
		lock_acquire(&filesys_lock);
		off_t res = file_write(file, buffer, size);
		lock_release(&filesys_lock);
		return res;
	}
	
	return -1;
}	

void seek (int fd, unsigned position){
	if( fd<2 || fd>63 ){ // fd 안되는 거
		exit(-1);
	}
	struct thread *curr = thread_current();
	struct file *file = curr->fdt[fd];
	if(file == NULL) {
		exit(-1);
	}
	lock_acquire(&filesys_lock);
	file_seek(file, position);
	lock_release(&filesys_lock);
}

unsigned tell (int fd){
	if( fd<2 || fd>63 ){ // fd 안되는 거
		exit(-1);
	}
	struct thread *curr = thread_current();
	struct file *file = curr->fdt[fd];
	if(file == NULL) {
		exit(-1);
	}
	lock_acquire(&filesys_lock);
	off_t res = file_tell(file);
	lock_release(&filesys_lock);
	return res;
}

void close (int fd){
	if( fd<2 || fd>63 ){ // fd 안되는 거
		exit(-1);
	}
	struct thread *curr = thread_current();
	struct file *file = curr->fdt[fd];
	if(file == NULL) {
		exit(-1);
	}
	if(curr->running_file == file){
			return 0;
	}
	lock_acquire(&filesys_lock);
	file_close(file);
	curr->fdt[fd] = NULL;
	lock_release(&filesys_lock);
}

int dup2(int oldfd, int newfd){

}

// void is_user(uint64_t vaddr){
// 	if(!is_user_vaddr(vaddr)){
// 		exit(-1);
// 	}
// }


bool check_addr(const void *addr){
	if (addr == NULL){
		return false;
	}
	if (!is_user_vaddr(addr)){
		return false;
	} 
	if (pml4_get_page(thread_current()->pml4, addr) == NULL){
		return false;
	} 
	return true;
}
// void check_addr(void *addr){
// 	struct thread *t = thread_current();
// 	if(is_kernel_vaddr(addr) || pml4_get_page(t->pml4, addr) == NULL){
// 		syscall_abnormal_exit(-1); //?
// 	}
// }

