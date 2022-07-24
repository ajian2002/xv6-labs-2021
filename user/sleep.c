#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        char* s = "usage: sleep seconds\n";
        fprintf(2, s);
        exit(-1);
    }
    int seconds = atoi(argv[1]);
    sleep(seconds);
    exit(0);
}