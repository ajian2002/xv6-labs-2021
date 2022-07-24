
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
int main(int argc, char **argv)
{
    int pid = 0;
    int pip[2];
    pipe(pip);
    if(0==(pid=fork())){
        // child process
        char b;
        if (0 < read(pip[0], &b, 1))
        {
            printf("%d: received ping\n", getpid());
            write(pip[1], &b, 1);
            exit(0);
        }
        else
        {
            exit(-1);
        }
    
    }else{
        // father process
        char b = 'a';
        write(pip[1], &b, 1);
        wait(&pid);
        if (0 < read(pip[0], &b, 1))
        {
            printf("%d: received pong\n", pid);
            write(pip[1], &b, 1);
            exit(0);
        }
        else
        {
            exit(-1);
        }
    }
    
    
    exit(0);
}