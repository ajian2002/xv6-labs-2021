
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
char* fmtname(char* path)
{
    static char buf[DIRSIZ + 1];
    char* p;

    // Find first character after last slash.
    for (p = path + strlen(path); p >= path && *p != '/'; p--)
        ;
    p++;

    // Return blank-padded name.
    if (strlen(p) >= DIRSIZ) return p;
    memmove(buf, p, strlen(p));
    memset(buf + strlen(p), 0, DIRSIZ - strlen(p));
    return buf;
}

int find(char* path, const char* name)
{
    // printf("path=%s[%d],name=%s[%d],fmtname=%s[%d]\n", path, strlen(path), name, strlen(name),
    //        fmtname(path), strlen(fmtname(path)));

    int fd = 0;
    struct dirent de;
    struct stat st;
    if (memcmp(fmtname(path), name, strlen(name)) == 0)
    {
        printf("%s\n", path);
    }
    if ((fd = open(path, 0)) < 0)
    {
        fprintf(2, " cannot open [%s][%d]%d\n", path, strlen(path), fd);
        return -1;
    }
    if (fstat(fd, &st) < 0)
    {
        fprintf(2, " cannot stat [%s][%d]\n", path, strlen(path));
        close(fd);
        return -1;
    }
    switch (st.type)
    {
        case T_DIR:  // Directory
            while (read(fd, &de, sizeof(de)) == sizeof(de))
            {
                if (de.inum == 0) continue;
                // printf("de.name=%s[%d]\n", de.name,strlen(de.name));
                if (memcmp(de.name, ".", 1) == 0 || memcmp(de.name, "..", 2) == 0)
                {
                    continue;
                }

                char newPath[512] = {0};
                strcpy(newPath, path);
                strcpy(newPath + strlen(newPath), "/");
                strcpy(newPath + strlen(newPath), de.name);

                find(newPath, name);
            }
            break;
        case T_FILE:
        default:
            break;
    }
    close(fd);
    return 0;
}
int main(int argc, char** argv)
{
    if (argc != 3)
    {
        printf("Usage: find [path] [name]\n");
        exit(-1);
    }
    char* path = argv[1];
    char* name = argv[2];
    find(path, name);
    exit(0);
}