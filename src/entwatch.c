#include "../config.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include <linux/random.h>

int looping = 1;

int main(int argc, char* argv[])
{
        int rfd = 0;
        int entcnt = 0;
        int r;
        int waittime = 30;
        char timebuf[512];
        time_t now;
        struct tm *tmp;

        if(argc == 2) {
                waittime = atoi(argv[1]);
                if(waittime < 1) {
                        fprintf(stderr, "specified wait time cannot be less than 1\n");
                        return -1;
                }
        }

        rfd = open("/dev/random", O_RDONLY);

        if(-1 == rfd) {
                fprintf(stderr, "error opening /dev/random: %s\n", strerror(errno));
                return -1;
        }

        // loop here, calling ioctl(rfd, RNDGETENTCNT) and printing the result
        do {
                now = time(NULL);
                r = ioctl(rfd, RNDGETENTCNT, &entcnt);
                if(0 > r) {
                        int e = errno;
                        fprintf(stderr, "Error with ioctl call: %s\n", strerror(e));
                        break;
                }
                tmp = localtime(&now);
                strftime(timebuf, 512, "%F %T", tmp);
                printf("[%s] %d\n", timebuf, entcnt);
                sleep(waittime);
        } while(looping);

        close(rfd);
        return 0;
}
