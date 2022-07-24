
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void doChild(int p[2])
{
    int x, n, r;
    x = n = r = 0;
    close(p[1]);
    if ((n = read(p[0], &r, sizeof(int))) != 0)
    {
        printf("prime %d\n", r);
    }
    else
    {
        exit(0);
    }
    int p1[2];
    pipe(p1);
    if (fork() == 0)
    {
        doChild(p1);
    }
    else
    {
        while ((n = read(p[0], &x, sizeof(int))) != 0)
        {
            if (x % r != 0)
            {
                write(p1[1], &x, sizeof(int));
            }
        }
        close(p1[1]);
        wait(0);
    }
}
int main(int argc, char **argv)
{
    int p[2];
    pipe(p);
    int pid = 0;
    if (0 == (pid = fork()))
    {
        // child
        doChild(p);
    }
    else
    {
        close(p[0]);
        for (int i = 2; i <= 35; i++)
        {
            write(p[1], &i, sizeof(int));
        }
        close(p[1]);
        wait(0);
    }
    exit(0);
}
