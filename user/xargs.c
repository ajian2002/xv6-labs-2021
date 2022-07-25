#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"

char* getString(int argc, char* argv[])
{
    char* string = malloc(1024);
    memset(string, 0, 1024);
    for (int i = 1; i < argc; i++)
    {
        memcpy(&string[strlen(string)], argv[i], strlen(argv[i]));
        memcpy(&string[strlen(string)], " ", 1);
    }
    // printf("string=[%s]\n", string);
    return string;
}

char** split(char* begin, int* line, char* string)
{
    for (int j = 0; j < strlen(begin); j++)
    {
        if (begin[j] != '\n')
        {
            continue;
        }
        (*line)++;
    }

    char** result = (char**)malloc(sizeof(char*) * (*line));
    for (int i = 0; i < *line; i++)
    {
        result[i] = (char*)malloc(128);
        memset(result[i], 0, 128);
        memcpy(result[i] + strlen(result[i]), string, strlen(string));
        memcpy(result[i] + strlen(result[i]), " ", 1);
    }
    int number = 0;
    int last = 0;
    for (int i = 0; i < strlen(begin); i++)
    {
        if (begin[i] != '\n')
        {
            last++;
        }
        else
        {
            memcpy(&result[number][strlen(result[number])], &begin[i - last], last);
            memcpy(&result[number][strlen(result[number])], " ", 1);
            number++;
            last = 0;
        }
    }
    // printf("split result\n");
    // for (int i = 0; i < *line; i++)
    // {
    //     printf("[%d]:[%s]\n", i, result[i]);
    // }
    // printf("split result over\n");
    return result;
}

char** getExec(char* s)
{
    // printf("s=[%s]\n", s);
    char** result = malloc(sizeof(char*) * 128);
    int num = 0;
    int len = 0;
    for (int i = 0; i < strlen(s); i++)
    {
        if (s[i] != ' ' && s[i] != '\n')
        {
            if (len == 0)
            {
                result[num] = malloc(sizeof(char) * 128);
                len++;
            }
            else
            {
                len++;
                continue;
            }
        }
        else
        {
            if (len != 0)
            {
                memcpy(result[num], &s[i - len], len);
                // printf("num=%d,result=%s\n", num, result[num]);
                num++;
            }
            len = 0;
        }
    }
    // printf("exec init\n");
    // for (int i = 0; i < num; i++)
    // {
    //     printf("[%d]:[%s]\n", i, result[i]);
    // }
    // printf("exec over\n");
    return (char**)result;
}

int main(int argc, char** argv)
{
    int n = 0;
    char tempbuf[256];
    char temp[256];
    while ((n = read(0, temp, sizeof(temp))) > 0)
    {
        memcpy(tempbuf + strlen(tempbuf), temp, strlen(temp));
        memset(temp, 0, strlen(temp));
    }
    int line = 0;
    // int ss;
    char* string = getString(argc, argv);
    char** ppppp = (char**)split(tempbuf, &line, string);
    free(string);
    for (int i = 0; i < line; i++)
    {
        // char* s = ppppp[i];
        char** tempargv = getExec(ppppp[i]);

        if (fork() == 0)
        {
            exec(tempargv[0], tempargv);
            exit(0);
        }
        else
        {
            wait(0);
            free(ppppp[i]);
        }
    }
    free(ppppp);
    exit(0);
}