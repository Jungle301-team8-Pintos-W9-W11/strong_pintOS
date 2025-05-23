#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081					/* Segment selector msr */
#define MSR_LSTAR 0xc0000082				/* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */
#define FDCOUNT_LIMIT 1 << 9

typedef int pid_t;

void syscall_init(void)
{
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
													((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
						FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
/*
인자 들어오는 순서:
1번째 인자: %rdi
2번째 인자: %rsi
3번째 인자: %rdx
4번째 인자: %r10
5번째 인자: %r8
6번째 인자: %r9
*/
void syscall_handler(struct intr_frame *f)
{
	// TODO: Your implementation goes here.
	// printf ("system call!\n");

	char *fn_copy;

	switch (f->R.rax)
	{ // rax is the system call number
	case SYS_HALT:
		halt(); // pintos를 종료시키는 시스템 콜
		break;
	case SYS_EXIT:
		exit(f->R.rdi); // 현재 프로세스를 종료시키는 시스템 콜
		break;
	case SYS_FORK:
		f->R.rax = fork(f->R.rdi);
		break;
	case SYS_EXEC:
		break;
	case SYS_WAIT:
		break;
	case SYS_CREATE:
		break;
	case SYS_REMOVE:
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
		break;
	case SYS_CLOSE:
		close(f->R.rdi);
		break;
	default:
		exit(-1);
		break;
	}
	// thread_exit ();
}

void halt(void)
{
	power_off();
}

void exit(int status)
{ // !
	struct thread *cur = thread_current();
	printf("%s: exit(%d)\n", cur->name, status);
	thread_exit();
}

/*
	파일 생성 성공시 true, 실패시 false
	pass create-normal only
*/
bool create(const char *file, unsigned initial_size)
{
	check_address(file);
	if (file == NULL)
		exit(-1);

	if (filesys_create(*file, initial_size))
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool remove(const char *file)
{
	check_address(file);
	if (filesys_remove(file))
	{
		return true;
	}
	else
	{
		return false;
	}
}

/*
	열려고 하는 해당 파일을 poninter로 받음
	파일 열기 실패시 -1, 성공시 file descriptor 값 반환
	각 프로세스는 독립된 fd 값 소유
	하나의 파일이더라도 파일이 2번 이상 열리면 새 fd 반환
*/
int open(const char *file)
{
	check_address(file); // 주소 유효성 검사

	struct file *open_file = filesys_open(file); // 파일 시스템에서 파일 열기
	struct thread *curr = thread_current();

	if (open_file == NULL)
	{
		return -1;
	}
	else
	{
		for (int i = 2; i < 64; i++)
		{
			if (curr->fdt[i] == NULL)
			{
				curr->fdt[i] = open_file;
				return i;
			}
		}
	}
}

int filesize(int fd)
{
	return file_length(fd); //
}

int read(int fd, void *buffer, unsigned size)
{
}

int write(int fd, const void *buffer, unsigned size)
{
	if (fd == 1)
	{
		putbuf(buffer, size);
		return size;
	}
	return -1;
}

void seek(int fd, unsigned position)
{
}
/*
open file인 fd에서 읽히거나 써질 의 다음 바이트 위치를 리턴

*/
unsigned tell(int fd)
{
	struct thread *curr = thread_current();
	struct file *file = curr->fdt[fd];
	if (file == NULL)
	{
		exit(-1);
	}
	file_tell(file);
}

void close(int fd)
{
}

pid_t fork(const char *thread_name)
{ // !
}

int exec(const char *file)
{
}

int wait(pid_t pid)
{ // !
}

int dup2(int oldfd, int newfd)
{
}

/*
	포인터가 가리키는 주소 영역이 사용자 영역인지 확인
	잘못된 영역(커널 영역) 이면 종료
*/
void check_address(void *addr)
{
	struct thread *t = thread_current();
	if (!is_user_vaddr(addr) || addr == NULL)
	{
		exit(-1);
	}
}