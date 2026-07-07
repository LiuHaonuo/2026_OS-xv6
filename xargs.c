#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

int split_line(char *line, char **args, int max_args) {
    int count = 0;
    int i = 0;
    
    while (line[i] != '\0' && count < max_args - 1) {
        while (line[i] == ' ' && line[i] != '\0') i++;
        if (line[i] == '\0') break;
        
        args[count++] = &line[i];
        
        while (line[i] != ' ' && line[i] != '\0') i++;
        if (line[i] == ' ') {
            line[i] = '\0';
            i++;
        }
    }
    args[count] = 0;
    return count;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(2, "xargs: need a command\n");
        exit(1);
    }
    
    char *cmd = argv[1];
    char *fixed_args[MAXARG];
    int fixed_argc = 0;
    
    for (int i = 2; i < argc; i++) {
        fixed_args[fixed_argc++] = argv[i];
    }
    
    char line[512];
    
    while (1) {
        int i = 0;
        char c;
        while (read(0, &c, 1) == 1) {
            if (c == '\n') {
                line[i] = '\0';
                break;
            }
            line[i++] = c;
            if (i >= sizeof(line) - 1) {
                line[i] = '\0';
                break;
            }
        }
        
        if (i == 0) break;
        if (line[0] == '\0') continue;
        
        char *args[MAXARG];
        int j = 0;
        
        for (int k = 0; k < fixed_argc; k++) {
            args[j++] = fixed_args[k];
        }
        
        j += split_line(line, &args[j], MAXARG - j);
        args[j] = 0;
        
        int pid = fork();
        if (pid == 0) {
            exec(cmd, args);
            fprintf(2, "xargs: exec %s failed\n", cmd);
            exit(1);
        } else {
            wait(0);
        }
    }
    
    exit(0);
}