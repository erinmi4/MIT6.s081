// Saved registers for kernel context switches.
struct context {
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

// Per-CPU state.
struct cpu {
  // 指向当前在该 CPU 上运行的进程的指针，如果没有进程在该 CPU 上运行，则为 NULL
  struct proc *proc;          
  // 存储上下文信息，在调用 swtch() 函数进行上下文切换以进入调度器时会使用到这个 context 结构体
  // 上下文信息通常包括寄存器的值，如返回地址、栈指针等，在进程切换时，当前进程的执行状态会被保存在这个 context 结构体中，以便后续恢复执行
  struct context context;     
  // 表示 push_off() 函数嵌套的深度，push_off() 函数可能用于关闭中断，noff 变量可以帮助跟踪中断被关闭的层次，以便在适当的时候重新启用中断
  int noff;                   
  // 存储在调用 push_off() 函数之前中断是否启用，当 push_off() 函数被调用时，可能会关闭中断，intena 会记录之前的中断状态，以便在需要时恢复到之前的中断状态
  int intena;                 
};


extern struct cpu cpus[NCPU];

// per-process data for the trap handling code in trampoline.S.

/*该结构体用于存储与陷阱处理相关的进程数据。spsr
它保存了用户进程在陷阱发生时的各种寄存器状态，
以便在陷阱处理完成后能够准确地恢复用户进程的执行，
确保用户进程在陷阱处理前后的执行状态的一致性和正确性。*/
// sits in a page by itself just under the trampoline page in the
// user page table. not specially mapped in the kernel page table.
// the sscratch register points here.
// uservec in trampoline.S saves user registers in the trapframe,
// then initializes registers from the trapframe's
// kernel_sp, kernel_hartid, kernel_satp, and jumps to kernel_trap.
// usertrapret() and userret in trampoline.S set up
// the trapframe's kernel_*, restore user registers from the
// trapframe, switch to the user page table, and enter user space.
// the trapframe includes callee-saved user registers like s0-s11 because the
// return-to-user path via usertrapret() doesn't return through
// the entire kernel call stack.
struct trapframe {
  /*   0 */ uint64 kernel_satp;   // kernel page table
  /*   8 */ uint64 kernel_sp;     // top of process's kernel stack
  /*  16 */ uint64 kernel_trap;   // usertrap()
  /*  24 */ uint64 epc;           // saved user program counter
  /*  32 */ uint64 kernel_hartid; // saved kernel tp
  /*  40 */ uint64 ra;
  /*  48 */ uint64 sp;
  /*  56 */ uint64 gp;
  /*  64 */ uint64 tp;
  /*  72 */ uint64 t0;
  /*  80 */ uint64 t1;
  /*  88 */ uint64 t2;
  /*  96 */ uint64 s0;
  /* 104 */ uint64 s1;
  /* 112 */ uint64 a0;
  /* 120 */ uint64 a1;
  /* 128 */ uint64 a2;
  /* 136 */ uint64 a3;
  /* 144 */ uint64 a4;
  /* 152 */ uint64 a5;
  /* 160 */ uint64 a6;
  /* 168 */ uint64 a7;
  /* 176 */ uint64 s2;
  /* 184 */ uint64 s3;
  /* 192 */ uint64 s4;
  /* 200 */ uint64 s5;
  /* 208 */ uint64 s6;
  /* 216 */ uint64 s7;
  /* 224 */ uint64 s8;
  /* 232 */ uint64 s9;
  /* 240 */ uint64 s10;
  /* 248 */ uint64 s11;
  /* 256 */ uint64 t3;
  /* 264 */ uint64 t4;
  /* 272 */ uint64 t5;
  /* 280 */ uint64 t6;
};

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
struct proc {
  // 用于进程的同步和互斥操作的自旋锁
  struct spinlock lock;

  // p->lock must be held when using these:
  // 进程状态，使用枚举类型表示，可能的状态有 UNUSED、USED、SLEEPING、RUNNABLE、RUNNING、ZOMBIE 等
  enum procstate state;        
  // 如果不为零，进程正在该通道上睡眠，可能用于进程间的同步
  void *chan;                  
  // 如果不为零，表示进程已被杀死
  int killed;                  
  // 存储进程的退出状态，将返回给父进程的 wait 操作
  int xstate;                  
  // 进程的唯一标识符，用于在系统中区分不同的进程
  int pid;                    

  // wait_lock must be held when using this:
  // 指向父进程的指针，用于建立进程间的父子关系
  struct proc *parent;         

  // these are private to the process, so p->lock need not be held.
  // 进程内核栈的虚拟地址
  uint64 kstack;               
  // 进程内存的大小（以字节为单位）
  uint64 sz;                   
  // 用户页表，用于将虚拟地址映射到物理地址
  pagetable_t pagetable;       
  // 指向 trapframe 结构体的指针，用于存储陷阱处理相关的数据
  struct trapframe *trapframe; 
  // 存储进程的上下文信息，在调用 swtch() 函数运行进程时会使用该上下文
  struct context context;     
  // 一个文件指针数组，用于存储进程打开的文件
  struct file *ofile[NOFILE];  
  // 指向当前目录的 inode 指针，用于表示进程的当前工作目录
  struct inode *cwd;           
  // 进程的名称，主要用于调试目的
  char name[16];               
};

