#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    // 检查命令行参数数量
    if (argc != 2) {
        fprintf(2, "Usage: sleep seconds\n");
        exit(1);
    }

    // 将输入的字符串转换为整数
    int time = atoi(argv[1]);
    if (time < 0) {
        fprintf(2, "sleep: invalid time '%s'\n", argv[1]);
        exit(1);
    }

    // 调用系统调用实现延时
    sleep(time);

    // 程序正常退出
    exit(0);
}
