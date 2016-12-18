#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char** argv)
{
    char *cursor = argv[1];

    int fd = open("/dev/leftpad", O_RDWR);
    printf("fd: %i\n", fd);
    write(fd, argv[1], strlen(argv[1]));

    while (*cursor) {
        *cursor = 0;
    }

    read(fd, argv[1], strlen(argv[1]));
    printf("Read: %s\n", argv[1]);

    return 0;
}
