#include "../config.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <getopt.h>

#include <linux/random.h>

int looping = 1;

void print_usage(int argc, char* argv[])
{
    const char usage[] =
        "Usage: %s [OPTION]\n"
        "Polls the kernel entropy pool and prints the results.\n"
        "\n"
        "Mandatory arguments for long options are mandatory for short "
            "options, too.\n"
        "\t-w, --wait=SECONDS\t\tHow long to wait between polls of the "
            "kernel entropy level.\n"
        "\t-r, --rand-path=PATH\t\tPath to random device. (Typically "
            "/dev/random)\n"
        "\t-h, --help\t\tThis help message\n";

    fprintf(stderr, usage, argv[0]);
}

int main(int argc, char* argv[])
{
    int waittime = 1; // seconds // -w --wait
    char* rand_path = "/dev/random"; // -r --rand-path

    {
        // Use temporaries for argument input.
        int wt = waittime;
        char* rp = rand_path;

        const char shortopts[] = "w:r:h";
        static const struct option longopts[] = {
            { "wait", required_argument, NULL, 'w'},
            { "rand-path", required_argument, NULL, 'r'},
            { "help", no_argument, NULL, 'h'},
            { NULL, 0, NULL, 0 }
        };

        int indexptr = 0;
        int val = getopt_long( argc, argv, shortopts, longopts, &indexptr );
        while( -1 != val)
        {
            switch(val)
            {
            case 'w':
                wt = atoi(optarg);
                break;
            case 'r':
                rp = optarg;
                break;
            case 'h':
                print_usage(argc, argv);
                return 0;
            default:
                print_usage(argc, argv);
                return 1;
            }
            val = getopt_long( argc, argv, shortopts, longopts, &indexptr);
        }

        // Validate input.
        // All values must be greater than 0.
        if(wt <= 0)
        {
            fprintf(stderr, "Error wait time must be greater than 0.\n");
        }

        waittime = wt;
        rand_path = rp;
    }

    int rfd = open(rand_path, O_RDONLY);

    if(-1 == rfd) {
        fprintf(stderr, "error opening %s: %s\n", rand_path, strerror(errno));
        return 1;
    }

    // loop here, calling ioctl(rfd, RNDGETENTCNT) and printing the result
    do {
        int entcnt = 0;
        char timebuf[512];
        time_t now;
        int r;

        now = time(NULL);
        r = ioctl(rfd, RNDGETENTCNT, &entcnt);
        if(0 > r) {
            fprintf(stderr, "Error with ioctl call on %s: %s\n", rand_path, strerror(errno));
            break;
        }
        strftime(timebuf, sizeof(timebuf), "%F %T", localtime(&now));
        printf("[%s] %d\n", timebuf, entcnt);
        sleep(waittime);
    } while(looping);

    close(rfd);
    return 0;
}

