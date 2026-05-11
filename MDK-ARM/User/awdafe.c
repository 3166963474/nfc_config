#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#define MAX_PATH 512

// 统计单个文件的行数
int countLines(const char *filename) {
    FILE *fp;
    int count = 0;
    int ch;
    
    fp = fopen(filename, "r");
    if (fp == NULL) {
        fprintf(stderr, "无法打开文件: %s\n", filename);
        return 0;
    }
    
    while ((ch = fgetc(fp)) != EOF) {
        if (ch == '\n') {
            count++;
        }
    }
    
    fclose(fp);
    return count;
}

// 检查文件扩展名是否为.c或.h
int isCFile(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (ext != NULL) {
        if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0) {
            return 1;
        }
    }
    return 0;
}

// 递归遍历目录并统计代码行数
void countCodeLines(const char *path, int *totalLines, int *fileCount) {
    DIR *dir;
    struct dirent *entry;
    struct stat statbuf;
    char fullPath[MAX_PATH];
    
    dir = opendir(path);
    if (dir == NULL) {
        fprintf(stderr, "无法打开目录: %s\n", path);
        return;
    }
    
    while ((entry = readdir(dir)) != NULL) {
        // 跳过当前目录和父目录
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // 构建完整路径
        snprintf(fullPath, sizeof(fullPath), "%s/%s", path, entry->d_name);
        
        // 获取文件信息
        if (stat(fullPath, &statbuf) == 0) {
            if (S_ISDIR(statbuf.st_mode)) {
                // 如果是目录，递归处理
                countCodeLines(fullPath, totalLines, fileCount);
            } else if (S_ISREG(statbuf.st_mode) && isCFile(entry->d_name)) {
                // 如果是.c或.h文件，统计行数
                int lines = countLines(fullPath);
                *totalLines += lines;
                (*fileCount)++;
                printf("%s: %d 行\n", fullPath, lines);
            }
        }
    }
    
    closedir(dir);
}

int main() {
    char currentPath[MAX_PATH] = ".";
    int totalLines = 0;
    int fileCount = 0;
    
    printf("开始统计C代码行数...\n\n");
    
    countCodeLines(currentPath, &totalLines, &fileCount);
    
    printf("\n========================================\n");
    printf("统计结果:\n");
    printf("文件数量: %d 个\n", fileCount);
    printf("总代码行数: %d 行\n", totalLines);
    printf("========================================\n");
    
    return 0;
}