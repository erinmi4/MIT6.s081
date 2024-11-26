#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

// 提取路径最后部分的文件名
char* fmtname(char *path) {
    static char buf[DIRSIZ + 1];
    char *p;

    // 找到路径最后的文件名部分
    for (p = path + strlen(path); p >= path && *p != '/'; p--)
        ;
    p++;

    // 返回格式化的文件名
    if (strlen(p) >= DIRSIZ)
        return p;
    memmove(buf, p, strlen(p));
    buf[strlen(p)] = '\0'; // 确保以 null 结束
    return buf;
}

// 递归查找目标文件
void find(char *path, char *target) {
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    // 打开目录
    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    // 获取文件或目录的状态
    if (fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch (st.type) {
    case T_FILE:
        // 如果是文件并匹配目标，打印完整路径
        if (strcmp(fmtname(path), target) == 0) {
            printf("%s\n", path);
        }
        break;

    case T_DIR:
        // 如果是目录，递归处理
        if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
            fprintf(2, "find: path too long\n");
            break;
        }
        //先对目录的名称处理
        strcpy(buf, path);
        p = buf + strlen(buf);
        *p++ = '/';

        // 遍历目录
        while (read(fd, &de, sizeof(de)) == sizeof(de)) {
            if (de.inum == 0)
                continue;

            // 忽略特殊目录 . 和 ..
            if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
                continue;

            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;

            if (stat(buf, &st) < 0) {
                fprintf(2, "find: cannot stat %s\n", buf);
                continue;
            }

            // 递归调用 find
            find(buf, target);
        }
        break;
    }
    close(fd);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(2, "Usage: find [directory] [filename]\n");
        exit(1);
    }

    // 依次查找每个目标文件
    for (int i = 2; i < argc; i++) {
        find(argv[1], argv[i]);
    }

    exit(0);
}
