#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(2, "Usage: xargs <command> [args...]\n");
        exit(1);
    }

    // 准备初始命令参数
    char *cmd[MAXARG];
    int i;
    for (i = 1; i < argc && i < MAXARG - 1; i++) {
        cmd[i - 1] = argv[i];
    }

    // 读取标准输入
    char buf[512];
    int buf_index = 0;
    char c;

    while (read(0, &c, 1) == 1) {
        if (c == '\n') {
            // 遇到换行符时，解析完整的一行
            buf[buf_index] = '\0'; // 将读取的行变成字符串
            buf_index = 0;

            // 在命令参数数组中添加当前行
            cmd[i - 1] = buf; // 当前行作为命令的附加参数
            cmd[i] = 0;       // 以 NULL 结束参数列表

            // 创建子进程执行命令
            if (fork() == 0) {
                exec(cmd[0], cmd);
                fprintf(2, "xargs: exec %s failed\n", cmd[0]);
                exit(1);
            } else {
                wait(0); // 父进程等待子进程完成
            }
        } else {
            buf[buf_index++] = c; // 将字符加入当前行
        }
    }

    exit(0);
}
