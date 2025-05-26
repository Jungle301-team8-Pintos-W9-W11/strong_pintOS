#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

#include "filesys/filesys.h" // filesys_* func
#include "filesys/file.h"		 // file_* func
#include "threads/vaddr.h"	 // is_user_vaddr
// #include "lib/user/syscall.h" 	// pid_t
#include "threads/palloc.h" // palloc_get_page
#include "lib/stdio.h"			// predefined fd

/* userprog/syscall.h */
void syscall_entry(void);
void syscall_handler(struct intr_frame *);

static struct file *find_file_by_fd(int fd);
void remove_file_from_fdt(int fd);
int add_file_to_fdt(struct file *file);

void halt(void);
void exit(int status);
// pid_t fork(const char *thread_name, struct intr_frame *f);
// int exec(const char *file);
// int wait(tid_t pid);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned length);
int write(int fd, const void *buffer, unsigned length);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);

unsigned tell(int fd);

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
#define FDCOUNT_LIMIT 128

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

	lock_init(&filesys_lock); // 파일 역시 공유 자원이기때문에 lock 초기화
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
void syscall_handler(struct intr_frame *f UNUSED)
{
	// TODO: Your implementation goes here.
	struct thread *curr = thread_current();
	switch (f->R.rax)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	// case SYS_FORK:
	// 	f->R.rax = fork(f->R.rdi);
	// 	break;
	case SYS_EXEC:
		if (exec(f->R.rdi) == -1)
			exit(-1);
	// 	break;
	// case SYS_WAIT:
	// 	f->R.rax = wait(f->R.rdi);
	// 	break;
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
		break;
	}
	// printf ("system call!\n");
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
	lock_acquire(&filesys_lock);
	bool result = filesys_create(file, initial_size);
	lock_release(&filesys_lock);
	return result;
}
// userprog/syscall.c
bool remove(const char *file)
{
	check_address(file);
	return filesys_remove(file);
}

/*
	열려고 하는 해당 파일을 poninter로 받음
	파일 열기 실패시 -1, 성공시 file descriptor 값 반환
	각 프로세스는 독립된 fd 값 소유
	하나의 파일이더라도 파일이 2번 이상 열리면 새 fd 반환
*/
int open(const char *file)
{
	check_address(file);
	struct file *open_file = filesys_open(file);

	if (open_file == NULL)
	{
		return -1;
	}
	// fd table에 file추가
	int fd = add_file_to_fdt(open_file);

	// fd table 가득 찼을경우
	if (fd == -1)
	{
		file_close(open_file);
	}
	return fd;
}
int filesize(int fd)
{
	struct file *open_file = find_file_by_fd(fd);
	if (open_file == NULL)
	{
		return -1;
	}
	return file_length(open_file);
}

int read(int fd, void *buffer, unsigned size)
{
	check_address(buffer);
	off_t read_byte;
	uint8_t *read_buffer = buffer;
	if (fd == 0)
	{
		char key;
		for (read_byte = 0; read_byte < size; read_byte++)
		{
			key = input_getc();
			*read_buffer++ = key;
			if (key == '\0')
			{
				break;
			}
		}
	}
	else if (fd == 1)
	{
		return -1;
	}
	else
	{
		struct file *read_file = find_file_by_fd(fd);
		if (read_file == NULL)
		{
			return -1;
		}
		lock_acquire(&filesys_lock);
		read_byte = file_read(read_file, buffer, size);
		lock_release(&filesys_lock);
	}
	return read_byte;
}

int write(int fd, const void *buffer, unsigned size)
{
	check_address(buffer);

	int write_result;

	if (fd == 0) // stdin
	{
		exit(-1);
	}
	else if (fd == 1) // stdout
	{
		putbuf(buffer, size);
		return size;
	}
	else
	{
		struct file *write_file = find_file_by_fd(fd);
		if (write_file == NULL)
		{
			exit(-1);
		}
		lock_acquire(&filesys_lock);
		off_t write_result = file_write(write_file, buffer, size);
		lock_release(&filesys_lock);
		return write_result;
	}
}

void seek(int fd, unsigned position)
{
	struct file *seek_file = find_file_by_fd(fd);
	// 0,1,2는 이미 정의되어 있음
	if (fd < 2)
	{
		return;
	}
	file_seek(seek_file, position);
}

/*
open file인 fd에서 읽히거나 써질 의 다음 바이트 위치를 리턴

*/
unsigned
tell(int fd)
{
	struct thread *curr = thread_current();
	struct file *file = curr->fdt[fd];
	if (file == NULL)
	{
		exit(-1);
	}
	return file_tell(file);
}

void close(int fd)
{
	struct file *fileobj = find_file_by_fd(fd);
	if (fileobj == NULL)
	{
		return;
	}

	remove_file_from_fdt(fd);
}

// pid_t fork(const char *thread_name)
// { // !
// }

// userprog/syscall.c
int exec(char *file_name)
{
	check_address(file_name);
	int file_size = strlen(file_name) + 1;
	char *fn_copy = palloc_get_page(PAL_ZERO);
	if (fn_copy == NULL)
	{
		exit(-1);
	}
	strlcpy(fn_copy, file_name, file_size); // file 이름만 복사
	if (process_exec(fn_copy) == -1)
	{
		return -1;
	}
	NOT_REACHED();
	return 0;
}

// int wait(pid_t pid)
// { // !
// }

int dup2(int oldfd, int newfd)
{
}

/*
	포인터가 가리키는 주소 영역이 사용자 영역인지 확인
	잘못된 영역(커널 영역) 이면 종료
*/
void check_address(void *addr)
{
	// kernel VM 못가게, 할당된 page가 존재하도록(빈공간접근 못하게)
	struct thread *cur = thread_current();
	if (is_kernel_vaddr(addr) || pml4_get_page(cur->pml4, addr) == NULL)
	{
		exit(-1);
	}
}

static struct file *find_file_by_fd(int fd)
{
	struct thread *cur = thread_current();
	if (cur->fdt == NULL)
		return NULL;
	if (fd < 0 || fd >= FDCOUNT_LIMIT)
	{
		return NULL;
	}
	return cur->fdt[fd];
}

int add_file_to_fdt(struct file *file)
{
	struct thread *cur = thread_current();
	struct file **fdt = cur->fdt;
	if (cur->fdt == NULL)
		return -1;
	// Find open spot from the front
	//  fd 위치가 제한 범위 넘지않고, fd table의 인덱스 위치와 일치한다면
	while (cur->next_fd < FDCOUNT_LIMIT && fdt[cur->next_fd])
	{
		cur->next_fd++;
	}

	// error - fd table full
	if (cur->next_fd >= FDCOUNT_LIMIT)
		return -1;

	fdt[cur->next_fd] = file;
	return cur->next_fd;
}

void remove_file_from_fdt(int fd)
{
	struct thread *cur = thread_current();
	if (cur->fdt == NULL)
		return;
	// error : invalid fd
	if (fd < 0 || fd >= FDCOUNT_LIMIT)
		return;

	cur->fdt[fd] = NULL;
}
