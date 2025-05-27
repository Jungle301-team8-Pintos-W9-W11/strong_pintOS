#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

// ✅
#include "filesys/filesys.h" 	// filesys_* func
#include "filesys/file.h"		// file_* func
#include "threads/vaddr.h"		// is_user_vaddr
// #include "lib/user/syscall.h" 	// pid_t
#include "threads/palloc.h" 	// palloc_get_page
#include "lib/stdio.h" 			// predefined fd
#include "threads/synch.h" 		// lock

// ✅
// pid_t는 프로세스 ID를 표현할 때 사용하는 타입
typedef int pid_t;   

void syscall_entry (void);                  // 어셈블리 레벨에서 syscall 명령어가 실행되면 진입하는 함수
void syscall_handler (struct intr_frame *); // 시스템 콜 번호를 분석하고 실제 함수를 호출하는 로직


struct lock filesys_lock;   
// 파일 시스템 접근 시 동기화를 보장하기 위한 전역 락 변수
// 파일을 열거나 읽고 쓸 때 여러 프로세스가 동시에 접근하면 안되기 때문에 이 락을 걸어야 한다.

// ✅✅
void get_argument(void *rsp, int argc, void *argv[]);
void halt (void);
void exit (int status);
pid_t fork (const char *thread_name, struct intr_frame *f);
int exec (const char *file);
int wait (tid_t pid);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned length);
int write (int fd, const void *buffer, unsigned length);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */


// ✅
static int fdt_add_fd(struct file *f); 
static struct file *fdt_get_file(int fd); 
static void fdt_remove_fd(int fd);
static void check_string(const char* str);


#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

// PintOS가 사용자 프로그램으로부터 시스템 콜 요청을 받을 준비를 하는 초기화 함수
// x86-64 CPU에서 시스템 콜을 처리하려면 몇 가지 MSR을 설정해야 한다.
void
syscall_init (void) {
	write_msr(MSR_STAR, 
			((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	lock_init(&filesys_lock); // ✅
}

/* The main system call interface */
// ✅
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	struct thread *curr = thread_current(); // 현재 실행 중인 커널 스레드 구조체 포인터를 가져옴.
	switch (f->R.rax) 
	{

    // 종료 콜. power_off()를 호출해 가상 머신을 종료함.
	case SYS_HALT: 
		halt (); 
		break;

    // 현재 프로세스를 종료시킴. 종료 코드는 rdi에 들어 있음.
	case SYS_EXIT:
		exit (f->R.rdi);
		break;

    // 현재 프로세스를 복제함. 인자는 부모의 cmd_line. 
    // 결과로 자식의 tid를 리턴하므로, f->R.rax에 결과 저장.
	case SYS_FORK:
		f->R.rax = fork (f->R.rdi, f);
		break;

    // 주어진 파일명(rdi)으로 실행 파일을 실행시도.
    // 실패 시 즉시 exit(-1)
	case SYS_EXEC:
		if (exec (f->R.rdi) == -1)
			exit (-1);
		break;
    
    // wait(tid) 호출.
    // 자식 프로세스가 종료될 때까지 대기 후 exit status 반환.
	case SYS_WAIT:
		f->R.rax = wait (f->R.rdi);
		break;

    // 파일 생성 syscall
    // 인자: rdi: 파일 이름(char *), rsi: 크기(size)
    // 리턴값: 성공 여부
	case SYS_CREATE:
		f->R.rax = create (f->R.rdi, f->R.rsi);
		break;

    // 파일 제거 syscall
	case SYS_REMOVE:
		f->R.rax = remove (f->R.rdi);
		break;

    // 파일 오픈 후 fd 리턴
	case SYS_OPEN:
		f->R.rax = open (f->R.rdi);
		break;
    
    // 주어진 fd의 파일 크기를 반환
	case SYS_FILESIZE:
		f->R.rax = filesize (f->R.rdi);
		break;

    // 읽기 syscall
    // 먼저 유저 공간의 버퍼가 안전한지 확인
	case SYS_READ:
		check_buffer(f->R.rsi, f->R.rdx, 0);
		f->R.rax = read (f->R.rdi, f->R.rsi, f->R.rdx);
		break;
    
    // 쓰기 syscall
    // 먼저 유저 공간 버퍼 유효성 확인
	case SYS_WRITE:
		check_buffer(f->R.rsi, f->R.rdx, 1);
		f->R.rax = write (f->R.rdi, f->R.rsi, f->R.rdx);
		break;
    
    // fd 기준으로 커서 이동
	case SYS_SEEK:
		seek (f->R.rdi, f->R.rsi);
		break;

    // fd의 현재 위치를 반환
	case SYS_TELL:
		f->R.rax = tell (f->R.rdi);
		break;
    
    // fd 닫기
	case SYS_CLOSE:
		close (f->R.rdi);
		break;
    
    // 정의되지 않은 syscall 번호가 들어왔을 경우 커널에서 프로세스를 죽임
	default:
		exit (-1);
		break;
	}
	// printf ("system call!\n");
	// thread_exit ();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ✅
// 전체 시스템(시뮬레이터)을 종료하는 기능을 담당.
void 
halt(void) {
	power_off(); // power_off() 함수는 QEMU 시뮬레이터에게 종료 신호를 보내는 내부 함수
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ✅
// 유저 프로그램의 종료 요청, 또는 예외 처리 시 강제 종료를 위해 호출
void 
exit(int status) {
	struct thread *curr = thread_current();
	curr->exit_status = status;
	printf("%s: exit(%d)\n", curr->name, status);
	thread_exit();
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ✅
// fork()는 현재 프로세스를 복제(clone)하여 자식 프로세스를 생성
// rdi: 자식의 이름 (char *), f: 부모의 레지스터 상태 (struct intr_frame *)
pid_t fork (const char *thread_name, struct intr_frame *f) {
	check_string(thread_name);              // 자식에게 전달할 문자열 thread_name의 유효성 체크 필요 (check_string)
	//check_address(thread_name);
	return process_fork(thread_name, f);    // 실제 복제는 process_fork() 함수에 위임
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ✅
// exec()는 현재 프로세스를 완전히 다른 실행파일로 치환함.
// 주로 fork 직후 자식이 새 프로그램을 실행할 때 사용됨
int exec (const char *file){
	check_string(file);                         // file 문자열이 유저 주소인지, NULL 아닌지, 페이지 매핑이 있는지 검증
	//check_address(file);

	int size = strlen(file) + 1;                // 문자열 길이 측정 (널 문자 포함), 이후 복사할 크기를 미리 확보

	char *fn_copy = palloc_get_page(PAL_ZERO);  // 커널 힙에서 1페이지(4KB)를 할당받아 실행 파일명을 안전하게 복사할 준비
                                                // PAL_ZERO → 할당된 페이지 전체를 0으로 초기화

	if (fn_copy == NULL)                        // 메모리 할당 실패 시 즉시 종료 → 시스템 안정성 보장
		exit(-1);

	strlcpy(fn_copy, file, size);               // 유저가 넘긴 file 문자열을 커널 메모리로 안전하게 복사

	if (process_exec(fn_copy) == -1)            // process_exec()는 현재 프로세스의 페이지 테이블을 비우고, 새 ELF 실행 파일을 로딩함
		return -1;
	
	return 0;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ✅
// 부모 프로세스가 자식의 종료를 기다릴 때 시스템 콜로 호출
int wait(tid_t pid){
    return process_wait(pid);
            // child_list에서 해당 자식 존재 여부 확인
            // 자식이 종료되지 않았으면 현재 스레드를 BLOCKED로 만들고 기다림
            // 자식이 종료되면 exit_status를 받아서 리턴
            // 재호출 방지 (한 번 wait한 자식은 다시 wait 못함)
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ✅
// 유저 프로그램이 파일을 생성하려고 할 때 호출
bool 
create(const char *file, unsigned initial_size){        // file: 생성할 파일의 이름 (유저 공간 문자열), initial_size: 파일의 초기 크기 (바이트 단위)

    check_string(file);                                 // 문자열 포인터가 유효한 사용자 주소인지 확인
    lock_acquire(&filesys_lock);                        // 전역 락 획득 → 다른 쓰레드가 동시에 파일 시스템에 접근하지 못하게 막음
    bool result = filesys_create(file, initial_size);   // 파일 시스템 내부 함수 호출, 실제로 디렉토리 항목에 파일 추가 및 디스크에 inode 생성
    lock_release(&filesys_lock);                        // 락 해제 → 다른 쓰레드들이 파일 시스템 접근 가능
    return result;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ✅
// 유저 프로그램이 파일을 삭제할 때 실행
bool remove(const char *file){          // 유저가 넘긴 파일 이름을 받아 해당 파일 삭제 시도

    check_string(file);                 // 유저 주소가 유효한지 확인
    lock_acquire(&filesys_lock);        // 전역 락 획득 → 다른 쓰레드가 동시에 파일 시스템에 접근하지 못하게 막음
    bool res = filesys_remove(file);    // 주어진 이름을 가진 파일이 존재하면 제거하고 true 반환, 없으면 false 반환
    lock_release(&filesys_lock);        // 락 해제 → 다른 쓰레드들이 파일 시스템 접근 가능
    return res;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ✅
// 유저 프로세스에서 파일을 열고, 해당 파일을 식별할 수 있는 파일 디스크립터(FD) 를 반환
int 
open(const char *file) {

    check_string(file);             // 유저 주소가 유효한지 확인
    lock_acquire(&filesys_lock);    // 전역 락 획득 → 다른 쓰레드가 동시에 파일 시스템에 접근하지 못하게 막음
    struct file *target_file = filesys_open(file);  // 파일 이름을 기준으로 디스크에서 inode를 찾아 해당 파일 구조체를 반환

    // 파일이 존재하지 않는 경우, 락을 해제한 후 -1 반환
    if (target_file == NULL) {
        lock_release(&filesys_lock);
        return -1;
    }

    int fd = fdt_add_fd(target_file);
    // 현재 스레드의 FDT(File Descriptor Table)에 파일을 등록
    // fdt_add_fd()는 비어있는 FD 번호를 찾아 등록하고, 해당 번호를 반환

    // FDT가 가득 찼거나 할당 실패 시 → 열린 파일 닫아 메모리 누수 방지
    if (fd == -1) {
        file_close(target_file);
    }

    lock_release(&filesys_lock);    // 락 해제 → 다른 쓰레드들이 파일 시스템 접근 가능

    return fd;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ✅
// 유저가 오픈된 파일의 크기를 알고 싶을 때 filesize(fd)를 호출
int 
filesize (int fd){
    struct file *target_file = fdt_get_file(fd);    // 현재 스레드의 FDT에서 fd에 해당하는 파일 구조체 조회

    // 파일이 닫혔거나 잘못된 fd면 실패 처리
    if (target_file == NULL)
        return -1;

    lock_acquire(&filesys_lock);            // 전역 락 획득 → 다른 쓰레드가 동시에 파일 시스템에 접근하지 못하게 막음
    int size = file_length(target_file);    // 해당 파일의 inode 정보를 통해 크기 조회
    lock_release(&filesys_lock);            // 락 해제 → 다른 쓰레드들이 파일 시스템 접근 가능

    return size;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ✅
int 
read(int fd, void *buffer, unsigned size) {
    // 1. buffer 주소 유효성 체크 (항상 가장 먼저!)
    check_address(buffer);

    // 2. STDIN (키보드 입력)
    if (fd == STDIN_FILENO) {
        unsigned char *buf = buffer;
        for (unsigned i = 0; i < size; i++)
            buf[i] = input_getc();
        return size;
    }

    // 3. STDOUT에 read 요청 or fd < 0 or fd < 2는 무효
    if (fd < 2)
        return -1;

    // 4. 파일 객체 가져오기
    struct file *file = fdt_get_file(fd);
    if (file == NULL)
        return -1;

    // 5. 실제 파일 읽기
    lock_acquire(&filesys_lock);
    int read_bytes = file_read(file, buffer, size);
    lock_release(&filesys_lock);
    return read_bytes;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ✅
int 
write(int fd, const void *buffer, unsigned size) {
    // 1. buffer 주소 유효성 체크
    check_address((void *)buffer);

    // 2. STDOUT
    if (fd == STDOUT_FILENO) {
        putbuf(buffer, size);
        return size;
    }

    // 3. STDIN에 write 요청 or fd < 0 or fd < 2는 무효
    if (fd < 2)
        return -1;

    // 4. 파일 객체 가져오기
    struct file *file = fdt_get_file(fd);
    if (file == NULL)
        return -1;

    // 5. 실제 파일 쓰기
    lock_acquire(&filesys_lock);
    int write_bytes = file_write(file, buffer, size);
    lock_release(&filesys_lock);
    return write_bytes;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ✅
void 
seek (int fd, unsigned position){
    struct file *target_file = fdt_get_file(fd);
    if (fd <= STDOUT_FILENO || target_file == NULL)
        return;
    lock_acquire(&filesys_lock);      
    file_seek(target_file, position);
    lock_release(&filesys_lock);      
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ✅
unsigned 
tell (int fd){
    struct file *target_file = fdt_get_file(fd);
    if (fd <= STDOUT_FILENO || target_file == NULL)
        return 0; 
    lock_acquire(&filesys_lock);      
    unsigned pos = file_tell(target_file);
    lock_release(&filesys_lock);      
    return pos;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ✅
void
close(int fd) {
    struct file *target_file = fdt_get_file(fd);
    if (fd <= STDOUT_FILENO || target_file == NULL)
        return;
    fdt_remove_fd(fd);
    lock_acquire(&filesys_lock);
    file_close(target_file);
    lock_release(&filesys_lock);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ✅
static int 
fdt_add_fd(struct file *f) {
	struct thread *curr = thread_current();
	struct file **fdt = curr->fdt;

	while (curr->next_fd < FDCOUNT_LIMIT && fdt[curr->next_fd]) {
		curr->next_fd++;
	}

	if (curr->next_fd >= FDCOUNT_LIMIT)
		return -1;

	fdt[curr->next_fd] = f; 
	return curr->next_fd;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ✅
static struct file *
fdt_get_file(int fd) {
	struct thread *curr = thread_current();
	if (fd < STDIN_FILENO || fd >= FDCOUNT_LIMIT) { 
		return NULL;
	}
	return curr->fdt[fd]; 
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ✅
static void 
fdt_remove_fd(int fd) {
	struct thread *curr = thread_current();

	if (fd < STDIN_FILENO || fd >= FDCOUNT_LIMIT) 
		return;
	
	curr->fdt[fd] = NULL; 
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ✅
static void check_string(const char* str) {
    check_address((void*)str);
    while (1) {
        check_address((void*)str);
        if (*str == '\0') break;
        str++;
    }
}
