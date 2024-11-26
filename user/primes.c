#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// 判断是否为素数
int is_prime(int num) {
    if (num < 2) {
        return 0;
    }
    for (int i = 2; i <= num / 2; i++) {
        if (num % i == 0) {
            return 0;
        }
    }
    return 1;
}

// 筛选素数的主逻辑
void filter_primes(int pipefd[2]) {
    int prime;
    // 从管道读取第一个素数
    if (read(pipefd[0], &prime, sizeof(int)) == 0) {
        close(pipefd[0]);
        exit(0); // 无数据，退出
    }

    // 输出当前素数
    printf("prime %d\n", prime);

    // 创建新管道
    int new_pipefd[2];
    pipe(new_pipefd);

    if (fork() == 0) {
        // 子进程继续处理剩余数字
        close(pipefd[0]);   // 关闭父管道的读取端
        close(new_pipefd[1]); // 关闭新管道的写入端
        filter_primes(new_pipefd);
    } else {
        // 父进程筛选非倍数数字并写入新管道
        close(new_pipefd[0]); // 关闭新管道的读取端
        int num;
        while (read(pipefd[0], &num, sizeof(int)) > 0) {
            if (num % prime != 0) {
                write(new_pipefd[1], &num, sizeof(int));
            }
        }
        close(pipefd[0]);     // 关闭父管道的读取端
        close(new_pipefd[1]); // 关闭新管道的写入端
        wait(0);              // 等待子进程结束
    }
}

int main(int argc, char const *argv[]) {
    if (argc != 1) {
        fprintf(2, "Usage: primes\n");
        exit(1);
    }

    int pipefd[2];
    pipe(pipefd);

    if (fork() == 0) {
        // 子进程筛选素数
        close(pipefd[1]); // 关闭写入端
        filter_primes(pipefd);
    } else {
        // 父进程写入数字 2-35
        close(pipefd[0]); // 关闭读取端
        for (int i = 2; i <= 35; i++) {
            write(pipefd[1], &i, sizeof(int));
        }
        close(pipefd[1]); // 关闭写入端
        wait(0);          // 等待子进程结束
    }

    exit(0);
}
