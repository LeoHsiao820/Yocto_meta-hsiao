#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <stdint.h>
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include <sys/ioctl.h>
#include "fcntl.h"


#define BUFFER_SIZE     (256)
#define CLOSE_CMD			(_IO(0xEF, 0x1)) // Close timer
#define OPEN_CMD			(_IO(0xEF, 0x2)) // Open timer
#define SETPERIOD_CMD		(_IOW(0xEF,0x3,unsigned int)) // Set timer period


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
    int ret = 0;
    int cmd;
    uint32_t period;
    uint16_t data_buffer[BUFFER_SIZE];

    if (argc != 2) {
        printf("Error Usage\n");
        return 1;
    }

    filename = argv[1];
    
    fd = open(filename, O_RDWR);
    if (fd < 0) {
        printf("Error open %s\n", filename);
        return 1;
    }

    while (1) 
    {
        printf("Input CMD. 1: close timer, 2: open timer, 3: period timer\n");
        ret = scanf("%d", &cmd);
        if (ret != 1) {
            printf("Invalid input. Please enter a number.\n");
            // Clear the input buffer
            while (getchar() != '\n');
            continue;
        }

        switch(cmd) 
        {
        case 1:
            if (ioctl(fd, CLOSE_CMD, NULL) < 0) 
            {
                printf("ioctl CLOSE_CMD");
            } 
            else 
            {
                printf("Timer stopped\n");
            }
            break;

        case 2:
            if (ioctl(fd, OPEN_CMD, NULL) < 0) 
            {
                printf("ioctl OPEN_CMD");
            } 
            else 
            {
                printf("Timer started\n");
            }
            break;

        case 3:
            printf("Input period (ms): ");
            fflush(stdout);
            if (scanf("%u", &period) != 1) 
            {
                printf("Invalid period!\n");
                while (getchar() != '\n');
                continue;
            }
            if (ioctl(fd, SETPERIOD_CMD, period) < 0) 
            {
                printf("ioctl SETPERIOD_CMD");
            } 
            else 
            {
                printf("Period set to %u ms\n", period);
            }
            break;

        default:
            printf("Invalid CMD. Please enter 1, 2, or 3.\n");
            continue;
        }
    }

    close(fd);

    return 0;
}
