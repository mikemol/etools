#include "../config.h"
#define _POSIX_C_SOURCE 199309L

// This is the header file that lets us poll for the kernel's entropy level.
#include <linux/random.h>

#include <sys/ioctl.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <alloca.h>
#include <stdint.h>

int looping = 1;

int buff_write_pos = 0;
int buff_read_pos = 0;
int buff_size = 8388608;
/*@null@*/
char* entbuff = NULL;

/*@null@*/
FILE* fdRandom = NULL;

void free_entropy_buffer()
{
	// unmap. Should be called by atexit() if we successfully mapped.
	if(NULL != entbuff)
	{
		free(entbuff);
	}
	else
	{
		fprintf(stderr, "Logic error: free_entropy_buffer called on NULL entropy buffer.\n");
	}
}

void close_fdRandom()
{
	if(NULL != fdRandom)
	{
		int res = fclose(fdRandom);
		if(0 != res)
		{
			perror("Error closing random device");
			abort();
		}
	}
	else
	{
		fprintf(stderr, "Logic error: Random device fd null\n");
	}
}

int check_ent()
{
	if(NULL == fdRandom)
	{
		fprintf(stderr, "Logic error: Random device fd null\n");
		abort();
	}

	const int fNo = fileno(fdRandom);
	int entcnt;
        int r = ioctl(fNo, RNDGETENTCNT, &entcnt);
	if(0 > r) {
                fprintf(stderr, "Error with ioctl call: %s\n", strerror(errno));
		return -1;
        }

	return entcnt;
}

size_t get_read_remaining()
{
	int remaining = buff_write_pos - buff_read_pos;

	if(remaining >= 0)
		return (size_t) remaining;

	// Buffer is probably wrapping.
	remaining += buff_size;

	if(remaining >= 0)
		return (size_t) remaining;

	// Logic error! There's no valid case where this should happen.
	fprintf(stderr, "Internal error in buffer memory management!\n");
	abort();
	// Execution does not pass abort()
}

bool g_log_buffer_to_rand = false;
size_t buffer_to_rand_internal(const int to_transfer)
{
	if(buff_read_pos >= buff_size)
	{
		if(buff_read_pos == buff_size)
		{
			if(g_log_buffer_to_rand)
			{
				fprintf(stderr, "-W: buffer_to_rand_internal detected buffer wrap. to_transfer(%d)\n", to_transfer);
				fprintf(stderr, "-W:entcnt(%d), buffer(%zu)\n", check_ent(), get_read_remaining());
			}
			// Buffer wrapped.
			buff_read_pos = 0;
		}
		else
		{
			fprintf(stderr, "Logic error: read pos exceeded end of buffer!\n");
			abort();
		}
	}

	if((buff_read_pos + to_transfer) > buff_size)
	{
		fprintf(stderr, "Internal error: Would read past end of buffer!\n");
		abort();
	}

	// I am *not* happy with using raw types like this, but that's what
	// rngd is doing, as is random.h's rand_pool_info.
	struct {
		int entropy_count;
		int buf_size;
		unsigned char* buf;
	} entropy;

	entropy.entropy_count = to_transfer;
	entropy.buf_size = to_transfer;
	entropy.buf = alloca(to_transfer); // Allocate buffer space on the stack

	const int fileNo = fileno(fdRandom);

	if(-1 == fileNo)
	{
		perror("Unexpected failure while preparing to feed entropy to kernel\n");
	}

	const int ioRes = ioctl(fileNo, RNDADDENTROPY, &entropy);

	switch(ioRes)
	{
	case 0:
		// Good.
		break;
	case -EFAULT:
		fprintf(stderr, "EFAULT error while adding entropy to kernel.");
		abort();
		break;
	case -EPERM:
		fprintf(stderr, "EPERM error while adding entropy to kernel.");
		abort();
		break;
	case -EINVAL:
		fprintf(stderr, "EINVAL error while adding entropy to kernel.");
		abort();
		break;
	default:
		fprintf(stderr, "Unknown error while adding entropy to kernel.");
		abort();
		break;
	}
	
	size_t written = fwrite(entbuff + buff_read_pos, 1, to_transfer, fdRandom);

	if(0 == written)
	{
		if(ferror(fdRandom))
		{
			perror("Error writing to random device");
			abort();
		}
	}

	if(g_log_buffer_to_rand)
	{
		fprintf(stderr, "-W: Write. written(%zu) = buff_read_pos(%d), to_transfer(%d)\n", written, buff_read_pos, to_transfer);
	}

	buff_read_pos += written;
	if(g_log_buffer_to_rand)
	{
		fprintf(stderr,"-W: New buff_read_pos(%d)\n", buff_read_pos);
	}

	if(buff_read_pos > buff_size)
	{
		fprintf(stderr, "Internal error: READ past end of buffer!\n");
		abort();
	}
	return written;
}

size_t buffer_to_rand(const size_t to_transfer)
{
	// If we don't have any bytes left to read, nullop.
	if(buff_write_pos == buff_read_pos)
	{
		return 0;
	}

	if(g_log_buffer_to_rand)
	{
		fprintf(stderr, "-W: %zu bytes buffer -> random device\n", to_transfer);
	}

	// First, let's cap our read to however many bytes we have in the buffer.
	const size_t first_read_remaining = get_read_remaining();
	const size_t capped_read = to_transfer > first_read_remaining ? first_read_remaining : to_transfer;
	const size_t distance_to_end = buff_size - buff_read_pos;

	// First read: From here up to the edge of our buffer, or until we've
	// satisfied our read quantity, whichever comes first.
	
	const size_t first_read_qty = capped_read > distance_to_end ? distance_to_end : capped_read;

	if(g_log_buffer_to_rand)
	{
		fprintf(stderr, "-W: frr(%zu), cr(%zu), dte(%zu), frq(%zu)\n", first_read_remaining, capped_read, distance_to_end, first_read_qty);
	}
	const size_t first_bytes_read = buffer_to_rand_internal(first_read_qty);

	// Now, we may need to do one or two operations, depending on if our
	// read range goes over the buffer wrap.
	// All done here?
	if(first_read_qty == capped_read)
		return first_bytes_read;

	// Not quite; our buffer just wrapped, so we get to go again.
	const size_t second_read_qty = capped_read - first_read_qty;

	if(g_log_buffer_to_rand)
	{
		fprintf(stderr, "-W: srq(%zu)\n", second_read_qty);
	}
	const size_t second_bytes_read = buffer_to_rand_internal(second_read_qty);
	
	return first_bytes_read + second_bytes_read;
}

bool g_log_rand_to_buffer = false;
size_t rand_to_buffer_internal(size_t to_transfer)
{
	if(buff_write_pos >= buff_size)
	{
		if(buff_write_pos == buff_size)
		{
			// Buffer wrapped.
			if(g_log_rand_to_buffer)
			{
				fprintf(stderr, "-R: rand_to_buffer_internal detected buffer wrap. to_transfer(%zu)\n", to_transfer);
			}
			buff_write_pos = 0;
		}
		else
		{
			fprintf(stderr, "Logic error: write pos exceeded end of buffer!\n");
			abort();
		}
	}
	
	if((buff_write_pos + to_transfer) > buff_size)
	{
		fprintf(stderr, "Logic error: Would write past end of buffer!\n");
		abort();
	}

	size_t bytes_written = fread(entbuff + buff_write_pos, 1, to_transfer, fdRandom);
	if(g_log_rand_to_buffer)
	{
		fprintf(stderr, "-R: Write. rtb. written(%zu) = buff_write_pos(%d), to_transfer(%zu)\n", bytes_written, buff_write_pos, to_transfer);
	}

	buff_write_pos += bytes_written;
	if(buff_write_pos > buff_size)
	{
		fprintf(stderr, "Logic error: WROTE past end of buffer!\n");
		abort();
	}

	return bytes_written;
}

size_t rand_to_buffer(size_t to_transfer)
{
	// If our buffer is full, nullop.
	if(get_read_remaining() == buff_size)
	{
		return 0;
	}

	if(g_log_rand_to_buffer)
	{
		fprintf(stderr, "-R: %zu bytes random device -> buffer\n", to_transfer);
	}

	// First, let's cap our write to however many bytes we can still fit in the buffer.
	const size_t first_write_remaining = buff_size - get_read_remaining();
	const size_t capped_write = to_transfer > first_write_remaining ? first_write_remaining : to_transfer;
	const size_t distance_to_end = buff_size - buff_write_pos;

	// First write: From here up to the edge of our buffer, or until we've
	// satisfied our write quantity, whichever comes first.

	const size_t first_write_qty = capped_write > distance_to_end ? distance_to_end : capped_write;

	if(g_log_rand_to_buffer)
	{
		fprintf(stderr, "-R: bs(%d), grr(%zu), fwr(%zu), cw(%zu), dte(%zu), fwq(%zu), pw(%d)\n", buff_size, get_read_remaining(), first_write_remaining, capped_write, distance_to_end, first_write_qty, buff_write_pos);
	}

	const size_t first_bytes_written = rand_to_buffer_internal(first_write_qty);

	// All done here?
	if(first_write_qty == capped_write)
	{
		return first_bytes_written;
	}

	// Not quite; our buffer just wrapped, so we get to go again.
	const size_t second_write_qty = capped_write - first_write_qty;
	const size_t second_bytes_written = rand_to_buffer_internal(second_write_qty);

	return first_bytes_written + second_bytes_written;
}


void print_usage(int argc, char* argv[])
{
	const char usage[] =
		"Usage: %s [OPTION]\n"
		"Acts as an entropy reservoir tied into the Linux kernel's entropy pool.\n"
		"\n"
		"Mandatory arguments for long options are mandatory for short options, too.\n"
		"\t-i, --high-thresh=BITS\t\tNumber of bits of entropy the kernel pool must contain before the reservoir is fed.\n"
		"\t-l, --low-thresh=BITS\t\tMinimum allowed level of entropy in the kernel pool before bits from the reservoir are fed into it.\n"
		"\t-w, --wait=MICROSECONDS\t\tHow long to wait between polls of the kernel entropy level.\n"
		"\t-r, --rand-path=PATH\t\tPath to random device. (Typically /dev/random)\n"
		"\t-p, --print-period=MILLISECONDS\tHow often to print an update on operational information.\n"
		"\t-R, --log-reads\t\tPrint diagnostic information when we read from the random device.\n"
		"\t-W, --log-writes\t\tPrint diagnostic information when we write to the random device.\n"
		"\t-b, --buffer-size=BYTES\t\tSet the size of our internal entropy buffer.\n"
		"\t-h, --help\t\tThis help message\n";

	fprintf(stderr, usage, argv[0]);
}

double timespec_to_double(const struct timespec* t)
{
	if(NULL == t)
	{
		fprintf(stderr, "Logic error: timespec pointer NULL.\n");
		abort();
	}
	// double is good for integers up to about 2^56. At nsec precision,
	// that gives us about two years. So we'll live with it. Convert
	// timespecs to doubles, compare those.
	return (double) t->tv_sec * 1000000000.0 + (double) t->tv_nsec;
}

bool timespec_lt(const struct timespec* l, const struct timespec* r)
{
	return timespec_to_double(l) < timespec_to_double(r);
}

bool timespec_lte(const struct timespec* l, const struct timespec* r)
{
	return timespec_to_double(l) <= timespec_to_double(r);
}

bool timespec_gt(const struct timespec* l, const struct timespec* r)
{
	return timespec_to_double(l) > timespec_to_double(r);
}

bool timespec_gte(const struct timespec* l, const struct timespec* r)
{
	return timespec_to_double(l) >= timespec_to_double(r);
}

int floor_by_8(const int in)
{
	// Fractional of 8.
	const int remainder = in % 8;

	return in - remainder;
}

int ceil_by_8(const int in)
{
	// If we're already a multiple of 8, adding 7 will take us just shy of
	// the next multiple of 8. If we're not a multiple of 8, adding 7 will
	// result us in either being a multiple of 8, or being between the
	// next two multiples of eight. Flooring will take us to the nearer of
	// the two.
	return floor_by_8(in + 7);
}

void entropy_watch_loop(const struct timespec* waittime, const struct timespec* printperiod, const int entthresh_high, const int entthresh_low)
{
        // loop here, calling ioctl(rfd, RNDGETENTCNT) and printing the result
        int entcnt = check_ent();
	struct timespec wait_remainder = {0,0};
	struct timespec slept = {0,0};
	while( -1 != entcnt)
	{
                int sres = nanosleep(waittime, &wait_remainder);
		if(0 != sres)
		{
			perror("Sleep interrupted");
			abort();
		}
		
		slept.tv_sec += (waittime->tv_sec - wait_remainder.tv_sec);
		slept.tv_nsec += (waittime->tv_nsec - wait_remainder.tv_nsec);

		if(timespec_gte(&slept, printperiod))
		{
			fprintf(stderr, "entcnt(%d), buffer(%zu)\n", entcnt, get_read_remaining());
			slept.tv_sec = 0;
			slept.tv_nsec = 0;
		}
		
		entcnt = check_ent();
		if ( entcnt >= entthresh_high )
		{
			int e8f = floor_by_8(entcnt);
			int extra_bytes = (e8f - entthresh_high) / 8;

			while(extra_bytes > 0)
			{
				const int transferred = rand_to_buffer(extra_bytes);
				if(extra_bytes != transferred)
				{
					// Not all transferred. Try again later.
					break;
				}

				entcnt = check_ent();
				e8f = floor_by_8(entcnt);
				extra_bytes = (e8f - entthresh_high) / 8;
			}
		}
		else if ( entcnt <= entthresh_low )
		{
			int e8c = ceil_by_8(entcnt);
			int deficient_bytes = (entthresh_low - e8c) / 8;

			buffer_to_rand(deficient_bytes);
		}
		// Remaining case means we're within the noop zone.

		entcnt = check_ent();
	}
}

int main(int argc, char* argv[])
{
	// Knobs, tunables and argument processing.
	int entthresh_high = 4096 * 1 / 2; // -i --high-thresh
	int entthresh_low = 4096 * 1 / 16; // -l --low-thresh
	struct timespec waittime = { 0, 2500000 }; // seconds,nanoseconds // -w --wait
	struct timespec printperiod	= { 1, 0 }; // seconds,nanoseconds // -p --print-period
	
	char* rand_path = "/dev/random"; // -r --rand-path

	{
		// Use temporaries for argument input.
		int e_h = entthresh_high;
		int e_l = entthresh_low;
		int wt = waittime.tv_sec * 1000 + waittime.tv_nsec / 1000000; // milliseconds
		int pp = printperiod.tv_sec * 1000 + waittime.tv_nsec / 1000000; // milliseconds
		char* rp = rand_path;
		int bs = (int) buff_size;

		const char shortopts[] = "i:l:w:p:r:hRWb:";
		static const struct option longopts[] = {
			{ "high-thresh", required_argument, NULL, 'i' },
			{ "low-thresh", required_argument, NULL, 'l' },
			{ "wait", required_argument, NULL, 'w'},
			{ "print-period", required_argument, NULL, 'p'},
			{ "rand-path", required_argument, NULL, 'r'},
			{ "log-reads", no_argument, NULL, 'R'},
			{ "log-writes", no_argument, NULL, 'W'},
			{ "buff-size", required_argument, NULL, 'b'},
			{ "help", no_argument, NULL, 'h'},
			{ NULL, 0, NULL, 0 }
		};

		int indexptr = 0;
		int val = getopt_long( argc, argv, shortopts, longopts, &indexptr);
		while( -1 != val)
		{
			switch(val)
			{
			case 'i':
				e_h = atoi(optarg);
				break;
			case 'l':
				e_l = atoi(optarg);
				break;
			case 'w':
				wt = atoi(optarg);
				break;
			case 'p':
				pp = atoi(optarg);
				break;
			case 'r':
				rp = optarg;
				break;
			case 'R':
				g_log_rand_to_buffer = true;
				break;
			case 'W':
				g_log_buffer_to_rand = true;
				break;
			case 'b':
				bs = atoi(optarg);
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
		// First, high-thresh must be greater than low-thresh.
		if(e_l > e_h)
		{
                	fprintf(stderr, "Error: high threshold(%i) must be greater than low threshold(%i).\n", e_h, e_l);
			return 1;
		}

		// Second, all values must be greater than 0.
		if(e_h <= 0)
		{
                	fprintf(stderr, "Error: high threshold must be greater than 0.\n");
			return 1;
		}

		if(e_l <= 0)
		{
                	fprintf(stderr, "Error: low threshold must be greater than 0.\n");
			return 1;
		}

		if(wt <= 0)
		{
                	fprintf(stderr, "Error: wait time must be greater than 0.\n");
			return 1;
		}

		if(pp <= 0)
		{
                	fprintf(stderr, "Error: print period must be greater than 0.\n");
			return 1;
		}

		if(bs <= 0)
		{
			fprintf(stderr, "Error: buffer size must be greater than 0.\n");
			return 1;
		}

		// Next, check that low and high threshholds are multiples of
		// eight. If they're not, we get headaches from unintuitive
		// behavior resulting from bits/bytes conversions.
		if(e_h % 8 != 0)
		{
			fprintf(stderr, "Error: high threshold must be a multiple of eight.\n");
			return 1;
		}

		if(e_l % 8 != 0)
		{
			fprintf(stderr, "Error: low threshold must be a multiple of eight.\n");
			return 1;
		}

		// Finally, assign our temporaries back.
		entthresh_high = e_h;
		entthresh_low = e_l;

		waittime.tv_sec = wt / 1000; // ms to seconds
		waittime.tv_nsec = 1000000 * (wt % 1000); // remainder ms to ns

		printperiod.tv_sec = pp / 1000; // ms to seconds
		printperiod.tv_nsec = pp / 1000000 * (pp % 1000); // remainder ms to ns
		rand_path = rp;
		buff_size = bs;
	}

	// Now start setting up our operations pieces.
	
	entbuff = malloc(buff_size);
	if(NULL == entbuff)
	{
		perror("Failed to allocate memory for entropy buffer\n");
		return 1;
	}

	int at_res = atexit(free_entropy_buffer); // We mapped, so unmap when we're done.
	if(0 != at_res)
	{
		fprintf(stderr, "Warning: failed to register free_entropy_buffer with atexit()");
	}

	fdRandom = fopen(rand_path, "a+");
	if(0 == fdRandom)
	{
                fprintf(stderr, "Unable to open %s for r/w: %s\n", rand_path, strerror(errno));
		return 1;
	}

	// Turn off buffering for writes to fdRandom.
	setbuf(fdRandom, NULL);

	at_res = atexit(close_fdRandom); // We opened the file, remember to close it.
	if(0 != at_res)
	{
		fprintf(stderr, "Warning: failed to register close_fdRandom with atexit()");
	}

	entropy_watch_loop(&waittime, &printperiod, entthresh_high, entthresh_low);

	return 0;
}

