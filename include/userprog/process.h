#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

/* lazy_load_segment에 넘겨줄 보조 인자들을 저장할 구조체 */
struct lazy_load_arg
{
    struct file *file;   /* file 내용이 담긴 파일 객체 */
    off_t ofs;           /* 페이지에서 읽기 시작할 위치 */
    uint32_t read_bytes; /* 페이지에서 읽어야 하는 바이트 수 */
    uint32_t zero_bytes; /* 페이지에서 read_bytes만큼 읽고 남은 공간을 0으로 채워야 하는 바이트 수 */
};

tid_t process_create_initd(const char *file_name);
tid_t process_fork(const char *name, struct intr_frame *if_);
int process_exec(void *f_name);
int process_wait(tid_t);
void process_exit(void);
void process_activate(struct thread *next);
/* Project(2) */
void argument_stack(char **argvs, int argc, struct intr_frame *if_);
struct thread *get_child_process(int pid);
int process_add_file(struct file *f);
struct file *process_get_file(int fd);
void process_close_file(int fd);
#endif /* userprog/process.h */
