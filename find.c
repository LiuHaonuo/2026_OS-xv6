#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char* fmtname(char *path) {
    static char buf[DIRSIZ + 1];
    char *p;

    for(p = path + strlen(path); *p != '/' && p >= path; p--){
        ;
    }
    p++;

    strcpy(buf, p);
    return buf;
} 

void find(char *current_path, char *target_name) {
    int fd;
    char path[512], *p;
    struct dirent de;
    struct stat st;

    if((fd = open(current_path, 0)) < 0){
        fprintf(2, "ls: cannot open %s\n", current_path);
        return;
    }

    if(fstat(fd, &st) < 0){
        fprintf(2, "ls: cannot stat %s\n", current_path);
        close(fd);
        return;
    }

    if(st.type == T_FILE){
        if(strcmp(fmtname(current_path), target_name) == 0){
            printf("%s\n", current_path);
        }
        close(fd);
        return;
    }

    if(st.type == T_DIR){
        if(strlen(current_path) + 1 + DIRSIZ + 1 > sizeof(path)){
            fprintf(2, "find: path too long\n");
            close(fd);
            return;
        }

        while(read(fd, &de, sizeof(de))) {
            if(de.inum == 0){
                continue;
            }

            if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0){
                continue;
            }

            strcpy(path, current_path);
            p = path + strlen(path);
            *p++ = '/';
            strcpy(p, de.name);

            find(path, target_name);
        }
    }

    close(fd);
}

int main(int argc, char *argv[]) {
    if(argc != 3){
        fprintf(2, "Usage: find <path> <target_name>\n");
        exit(1);
    }

    find(argv[1], argv[2]);
    exit(0);
}