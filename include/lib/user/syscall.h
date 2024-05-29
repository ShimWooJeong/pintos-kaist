#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdbool.h>
#include "threads/synch.h"
#include "threads/thread.h"

typedef int pid_t;
typedef int off_t;

struct lock filesys_lock; /* 파일 접근 시 필요한 락 */

void syscall_init(void);
void syscall_entry(void);
void check_address(void *addr);
void halt(void);
void exit(int status);
pid_t fork(const char *thread_name, struct intr_frame *f);
int exec(const char *file);
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

#endif /* userprog/syscall.h */
