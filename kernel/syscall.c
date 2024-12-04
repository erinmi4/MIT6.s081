#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

// Fetch the uint64 at addr from the current process.
// 该函数用于从当前进程中获取存储在地址 addr 处的 uint64 类型的数据
int
fetchaddr(uint64 addr, uint64 *ip)
{
  // 获取当前进程的指针
  struct proc *p = myproc();
  // 检查地址是否超出进程的内存大小范围
  if(addr >= p->sz || addr+sizeof(uint64) > p->sz)
    return -1;
  // 从进程的页表中复制数据到 ip 所指向的位置
  if(copyin(p->pagetable, (char *)ip, addr, sizeof(*ip))!= 0)
    return -1;
  return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Returns length of string, not including nul, or -1 for error.
// 该函数用于从当前进程中获取存储在地址 addr 处的以空字符结尾的字符串
// 并将其复制到 buf 中，返回字符串的长度（不包括空字符），若出错则返回 -1
int
fetchstr(uint64 addr, char *buf, int max)
{
  // 获取当前进程的指针
  struct proc *p = myproc();
  // 从进程的页表中复制字符串到 buf 中，若出错返回错误码
  int err = copyinstr(p->pagetable, buf, addr, max);
  if(err < 0)
    return err;
  // 返回字符串的长度
  return strlen(buf);
}

// Return the nth word-sized system call argument as an integer.
// 该函数用于返回第 n 个字大小的系统调用参数作为整数
static uint64
argraw(int n)
{
  // 获取当前进程的指针
  struct proc *p = myproc();
  switch (n) {
  case 0:
    // 获取进程的 a0 寄存器的值
    return p->trapframe->a0;
  case 1:
    // 获取进程的 a1 寄存器的值
    return p->trapframe->a1;
  case 2:
    return p->trapframe->a2;
  case 3:
    return p->trapframe->a3;
  case 4:
    return p->trapframe->a4;
  case 5:
    return p->trapframe->a5;
  }
  // 若 n 超出范围，触发 panic 并返回 -1
  panic("argraw");
  return -1;
}

// Fetch the nth 32-bit system call argument.
// 该函数用于获取第 n 个 32 位系统调用参数
int
argint(int n, int *ip)
{
  // 调用 argraw 函数获取参数并存储到 ip 所指向的位置
  *ip = argraw(n);
  return 0;
}

// Retrieve an argument as a pointer.
// Doesn't check for legality, since copyin/copyout will do that.
// 该函数用于将第 n 个系统调用参数作为指针获取，不检查合法性，因为 copyin/copyout 会进行检查
int
argaddr(int n, uint64 *ip)
{
  // 调用 argraw 函数获取参数并存储到 ip 所指向的位置
  *ip = argraw(n);
  return 0;
}

// Fetch the nth word-sized system call argument as a null-terminated string.
// Copies into buf, at most max.
// Returns string length if OK (including nul), -1 if error.
// 该函数用于将第 n 个字大小的系统调用参数作为以空字符结尾的字符串获取
// 并将其复制到 buf 中，最多复制 max 个字符
// 若成功返回字符串长度（包括空字符），若出错则返回 -1
int
argstr(int n, char *buf, int max)
{
  uint64 addr;
  // 调用 argaddr 函数获取参数地址
  if(argaddr(n, &addr) < 0)
    return -1;
  // 调用 fetchstr 函数获取字符串
  return fetchstr(addr, buf, max);
}


// 声明外部函数，这些函数是系统调用的具体实现，将在其他文件中定义
extern uint64 sys_chdir(void);
extern uint64 sys_close(void);
extern uint64 sys_dup(void);
extern uint64 sys_exec(void);
extern uint64 sys_exit(void);
extern uint64 sys_fork(void);
extern uint64 sys_fstat(void);
extern uint64 sys_getpid(void);
extern uint64 sys_kill(void);
extern uint64 sys_link(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_mknod(void);
extern uint64 sys_open(void);
extern uint64 sys_pipe(void);
extern uint64 sys_read(void);
extern uint64 sys_sbrk(void);
extern uint64 sys_sleep(void);
extern uint64 sys_unlink(void);
extern uint64 sys_wait(void);
extern uint64 sys_write(void);
extern uint64 sys_uptime(void);

// 定义一个函数指针数组，存储系统调用的函数指针，根据系统调用号进行索引
static uint64 (*syscalls[])(void) = {
  // 系统调用号 SYS_fork 对应的函数是 sys_fork
  [SYS_fork]    sys_fork,
  // 系统调用号 SYS_exit 对应的函数是 sys_exit
  [SYS_exit]    sys_exit,
  // 系统调用号 SYS_wait 对应的函数是 sys_wait
  [SYS_wait]    sys_wait,
  // 系统调用号 SYS_pipe 对应的函数是 sys_pipe
  [SYS_pipe]    sys_pipe,
  // 系统调用号 SYS_read 对应的函数是 sys_read
  [SYS_read]    sys_read,
  // 系统调用号 SYS_kill 对应的函数是 sys_kill
  [SYS_kill]    sys_kill,
  // 系统调用号 SYS_exec 对应的函数是 sys_exec
  [SYS_exec]    sys_exec,
  // 系统调用号 SYS_fstat 对应的函数是 sys_fstat
  [SYS_fstat]   sys_fstat,
  // 系统调用号 SYS_chdir 对应的函数是 sys_chdir
  [SYS_chdir]   sys_chdir,
  // 系统调用号 SYS_dup 对应的函数是 sys_dup
  [SYS_dup]     sys_dup,
  // 系统调用号 SYS_getpid 对应的函数是 sys_getpid
  [SYS_getpid]  sys_getpid,
  // 系统调用号 SYS_sbrk 对应的函数是 sys_sbrk
  [SYS_sbrk]    sys_sbrk,
  // 系统调用号 SYS_sleep 对应的函数是 sys_sleep
  [SYS_sleep]   sys_sleep,
  // 系统调用号 SYS_uptime 对应的函数是 sys_uptime
  [SYS_uptime]  sys_uptime,
  // 系统调用号 SYS_open 对应的函数是 sys_open
  [SYS_open]    sys_open,
  // 系统调用号 SYS_write 对应的函数是 sys_write
  [SYS_write]   sys_write,
  // 系统调用号 SYS_mknod 对应的函数是 sys_mknod
  [SYS_mknod]   sys_mknod,
  // 系统调用号 SYS_unlink 对应的函数是 sys_unlink
  [SYS_unlink]  sys_unlink,
  // 系统调用号 SYS_link 对应的函数是 sys_link
  [SYS_link]    sys_link,
  // 系统调用号 SYS_mkdir 对应的函数是 sys_mkdir
  [SYS_mkdir]   sys_mkdir,
  // 系统调用号 SYS_close 对应的函数是 sys_close
  [SYS_close]   sys_close,
};

// 系统调用的入口函数，根据系统调用号调用相应的系统调用函数
void
syscall(void)
{
  int num;
  // 获取当前进程的指针
  struct proc *p = myproc();

  // 从进程的 trapframe 的 a7 寄存器中获取系统调用号
  num = p->trapframe->a7;
  // 检查系统调用号是否有效，并且对应的系统调用函数是否存在
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    // 调用相应的系统调用函数，并将结果存储在进程的 trapframe 的 a0 寄存器中
    p->trapframe->a0 = syscalls[num]();
  } else {
    // 如果系统调用号无效，打印错误信息，并将结果设置为 -1
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
