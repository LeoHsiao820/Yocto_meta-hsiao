#include <stdio.h>
#include <stdlib.h> 
#include <linux/string.h>
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"


#define TRUE            (0)
#define FALSE           (1)
#define BUFFER_SIZE     (256)


static char usrdata[] = {"usr data!"};


/*
 * main - the entry of 3_chardevtest_app
 * argc: argument count
 * argv: argument vector. 
 * ./3_chardevtest_app /dev/chardevbase <1/2> 1: read 2: write
 * return: 0 success; 1 fail
 * 
 * */
int main(int argc, char *argv[])
{
    char *filename;
    int fd;
    int ret = TRUE;
    char read_buffer[BUFFER_SIZE];
    char write_buffer[BUFFER_SIZE];

    if (argc != 3) {
        printf("Error Usage\n");
        ret = FALSE;
        return ret;
    }

    filename = argv[1];
    
    fd = open(filename, O_RDWR);
    if (fd < 0) {
        printf("Error open %s\n", filename);
        ret = FALSE;
        return ret;
    }

    if (atoi(argv[2]) == 1) 
    {
        ret = read(fd, read_buffer, 10);
        if (ret < 0) 
        {
            printf("Error read %s\n", filename);
            ret = FALSE;
            close(fd);
            return ret;
        }
        else 
        {
            printf("Read data: %s\n", read_buffer);
        }
    }
    else if (atoi(argv[2]) == 2) 
    {
        memcpy(write_buffer, usrdata, sizeof(usrdata));
        ret = write(fd, write_buffer, 10);
        if (ret < 0) {
            printf("Error write %s\n", filename);
            ret = FALSE;
            close(fd);
            return ret;
        }
    }
    else
    {
        printf("Wrong argument. It should be 1 or 2\n");
    }

    ret = close(fd);
    if (ret < 0) {          
        printf("Error close %s\n", filename);
        ret = FALSE;
        return ret;
    }

    return TRUE;
}
