#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

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

typedef int pid_t;

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
			break;
		case 3:
			// EXEC
			break;
		case 4:
			// WAIT
			break;
		case 5:
			// CREATE
			// create(f->R.rdi, f->R.rsi);
			break;
		case 6:
			// REMOVE
			break;
		case 7:
			// OPEN
			break;
		case 8:
			// FILESIZE
			break;
		case 9:
			// READ
			break;
		case 10:
			// WRITE
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case 11:
			// SEEK
			break;
		case 12:
			// TELL
			break;
		case 13:
			// CLOSE
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
	printf("%s: exit(%d)\n", cur->name, status);
	thread_exit();
	
}

pid_t fork (const char *thread_name){ // !

}

int exec (const char *file){

}

int wait (pid_t pid){ // !

}

bool create (const char *file, unsigned initial_size){
	return filesys_create(*file, initial_size);
}

bool remove (const char *file){

}

int open (const char *file){

}

int filesize (int fd){
	return file_length(fd);
}

int read (int fd, void *buffer, unsigned size){

}

int write(int fd, const void *buffer, unsigned size){
	if(fd==1){
		putbuf(buffer, size);
		return size;
	}
	return -1;
}	

void seek (int fd, unsigned position){
	
}

unsigned tell (int fd){

}

void close (int fd){

}

int dup2(int oldfd, int newfd){

}
