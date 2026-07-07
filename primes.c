#include "kernel/types.h"
#include "user/user.h"

void primes(int read_fd){
    int first;
    if(read(read_fd, &first, sizeof(first)) == 0){
        close(read_fd);
        exit(0);
    }

    printf("prime %d\n", first);

    int p[2];
    pipe(p);
    int pid = fork();
    if(pid == 0){
        close(p[1]);
        close(read_fd);
        primes(p[0]);
        exit(0);
    }
    else{
        close(p[0]);

        int num;
        while(read(read_fd, &num, sizeof(num)) > 0){
            if(num % first != 0){
                write(p[1], &num, sizeof(num));
            }
        }

        close(p[1]);
        close(read_fd);
        wait(0);
        exit(0);
    }
}

int main(){
    int p[2];
    pipe(p);

    int pid = fork();

    if(pid == 0){
        close(p[1]);
        primes(p[0]);
        exit(0);
    }
    else{
        close(p[0]);
        for(int i = 2; i <= 35; i++){
            write(p[1], &i, sizeof(i));
        }
        close(p[1]);
        wait(0);
        exit(0);
    }
}