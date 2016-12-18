#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char** argv)
{
    int fd = open("/dev/leftpad", O_RDWR);
    write(fd, argv[1], strlen(argv[1]));
    read(fd, argv[1], strlen(argv[1]));
    printf("Read: %s\n", argv[1]);
    return 0;
}
