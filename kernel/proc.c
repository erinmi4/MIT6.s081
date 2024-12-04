#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
// 定义一个自旋锁 wait_lock，用于确保等待的父进程的唤醒不会丢失，在使用 p->parent 时遵循内存模型，必须在任何 p->lock 之前获取
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
// 为每个进程的内核栈分配一个页面，将其映射到内存的高位，后面跟着一个无效的保护页
void
proc_mapstacks(pagetable_t kpgtbl) {
  struct proc *p;
  
  // 遍历进程表 proc 中的每个进程
  for(p = proc; p < &proc[NPROC]; p++) {
    // 为进程分配一个物理地址
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    // 计算进程内核栈的虚拟地址
    uint64 va = KSTACK((int) (p - proc));
    // 将虚拟地址映射到物理地址，设置为可读写
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table at boot time.
// 在启动时初始化进程表
void
procinit(void)
{
  struct proc *p;
  
  // 初始化 pid_lock 自旋锁，名称为 "nextpid"
  initlock(&pid_lock, "nextpid");
  // 初始化 wait_lock 自旋锁，名称为 "wait_lock"
  initlock(&wait_lock, "wait_lock");
  // 遍历进程表 proc 中的每个进程
  for(p = proc; p < &proc[NPROC]; p++) {
      // 初始化进程的锁，名称为 "proc"
      initlock(&p->lock, "proc");
      // 计算进程的内核栈地址
      p->kstack = KSTACK((int) (p - proc));
  }
}


// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
// 该函数用于获取当前 CPU 的标识符，必须在中断禁用的情况下调用，以防止与进程被移动到不同 CPU 时发生竞争条件
int
cpuid()
{
  // 读取 tp 寄存器的值作为 CPU 的标识符
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
// 该函数用于返回当前 CPU 的 cpu 结构体指针，调用时必须禁用中断
struct cpu*
mycpu(void) {
  // 获取当前 CPU 的标识符
  int id = cpuid();
  // 根据标识符获取对应的 cpu 结构体指针
  struct cpu *c = &cpus[id];
  return c;
}


// Return the current struct proc *, or zero if none.
/*
获取当前正在运行的进程 (struct proc*) 的函数实现。
它的核心思想是通过当前 CPU 的 struct cpu 结构体访问与其绑定的进程结构体 struct proc。
*/
struct proc*
myproc(void) {
  push_off(); //禁用当前 CPU 的中断。禁用中断可以防止在访问期间被中断打断，确保代码的原子性和一致性。
  struct cpu *c = mycpu();//mycpu() 返回当前 CPU 的 struct cpu *，是一个描述 CPU 状态的结构体。
  struct proc *p = c->proc;
  pop_off();
  return p;
}

// allocpid 函数用于分配一个唯一的进程标识符（PID）
int
allocpid() {
    // 存储要分配的 PID
    int pid;
    
    // 加锁，防止并发分配 PID 时出现冲突
    acquire(&pid_lock);
    // 将当前的 nextpid 作为新的 PID 分配
    pid = nextpid;
    // 更新 nextpid 的值，为下一次分配做准备
    nextpid = nextpid + 1;
    // 释放锁
    release(&pid_lock);

    return pid;
}


// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  // 遍历进程表 proc 中的每个进程
  for(p = proc; p < &proc[NPROC]; p++) {
    // 加锁，防止并发修改进程状态
    acquire(&p->lock);
    // 检查进程是否处于 UNUSED 状态
    if(p->state == UNUSED) {
      // 找到未使用的进程，跳转到 found 标签处进行后续处理
      goto found;
    } else {
      // 释放锁，继续查找下一个进程
      release(&p->lock);
    }
  }
  // 未找到未使用的进程，返回 0
  return 0;

found:
  // 为找到的进程分配一个唯一的进程标识符（PID）
  p->pid = allocpid();
  // 将进程状态设置为 USED，表示该进程正在使用中
  p->state = USED;

  // Allocate a trapframe page.
  // 为进程分配一个 trapframe 页面，用于存储陷阱处理相关的数据
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    // 若分配失败，释放进程资源
    freeproc(p);
    // 释放锁
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  // 为进程创建一个空的用户页表
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    // 若创建用户页表失败，释放进程资源
    freeproc(p);
    // 释放锁
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  // 初始化进程的上下文，将返回地址设置为 forkret，用于从内核空间返回用户空间
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  // 设置进程的栈指针，指向进程内核栈的顶部
  p->context.sp = p->kstack + PGSIZE;

  return p;
}


// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
//释放一个进程的相关资源，
static void
freeproc(struct proc *p)
{
  // 释放进程的 trapframe 页面，如果该页面存在
  if(p->trapframe)
    kfree((void*)p->trapframe);
  // 将 trapframe 指针置为 0，避免悬空指针
  p->trapframe = 0;
  // 释放进程的页表及相关物理内存
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  // 将页表指针置为 0，避免悬空指针
  p->pagetable = 0;
  // 将进程的内存大小置为 0
  p->sz = 0;
  // 将进程的 PID 置为 0
  p->pid = 0;
  // 将父进程指针置为 0
  p->parent = 0;
  // 清空进程名称，将第一个字符置为 0
  p->name[0] = 0;
  // 将进程的等待通道置为 0
  p->chan = 0;
  // 将进程的 killed 标志置为 0
  p->killed = 0;
  // 将进程的退出状态置为 0
  p->xstate = 0;
  // 将进程状态设置为 UNUSED，表示该进程未被使用
  p->state = UNUSED;
}


// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
//为给定的进程创建一个用户页表。
pagetable_t
proc_pagetable(struct proc *p)
{
  // 声明一个页表变量
  pagetable_t pagetable;

  // An empty page table.
  // 创建一个空的页表
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  // 将跳板代码映射到最高用户虚拟地址，仅在进出用户空间时由管理程序使用，因此不设置 PTE_U（用户可访问）
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    // 如果映射失败，释放页表及相关资源
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
  // 将 trapframe 映射到 TRAMPOLINE 下方，用于 trampoline.S
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    // 如果映射失败，先取消 TRAMPOLINE 的映射，再释放页表及相关资源
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}


// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&p->lock);
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}
