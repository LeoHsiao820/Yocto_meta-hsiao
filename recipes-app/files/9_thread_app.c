#include <stdio.h>
#include <stdlib.h> 
#include <linux/string.h>
#include <linux/types.h>
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
    __u16 data_buffer[BUFFER_SIZE];
    __u16 count = 0;

    if (argc != 3) {
        printf("Error Usage\n");
        return FALSE;
    }

    filename = argv[1];
    
    fd = open(filename, O_RDWR);
    if (fd < 0) {
        printf("Error open %s\n", filename);
        return FALSE;
    }

    data_buffer[0] = atoi(argv[2]);

    ret = write(fd, data_buffer, sizeof(data_buffer));
    if (ret < 0) {          
        printf("Error write %s\n", filename);
        return FALSE;
    }

    // Simulate app running for 15 seconds
    while (1) 
    {
        sleep(3);
        count++;
        printf("Gpio_led app running... %d s\n", count * 3);
        if (count >= 5)
        {
            break;
        }
    }

    ret = close(fd);
    if (ret < 0) {          
        printf("Error close %s\n", filename);
        return FALSE;
    }

    return TRUE;
}
