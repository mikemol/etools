#include "../config.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <getopt.h>
#include <stdbool.h>

#include <linux/random.h>

int looping = 1;

int pos_write = 0;
int pos_read = 0;
int buff_size = 8388608;
char* entbuff = 0;

void unmap_ent()
{
	// unmap. Should be called by atexit() if we successfully mapped.
	munmap(entbuff, buff_size);
}

FILE* fdRandom = NULL;

void close_fdRandom()
{
	fclose(fdRandom);
}

size_t get_read_remaining()
{
	int remaining = pos_write - pos_read;

	if(remaining >= 0)
		return remaining;

	// Buffer is probably wrapping.
	remaining += buff_size;

	if(remaining >= 0)
		return remaining;

	// Logic error! There's no valid case where this should happen.
	fprintf(stderr, "Internal error in buffer memory management!\n");
	abort();
}

bool g_log_reads = false;
size_t read_from_buffer_internal(const size_t toRead)
{
	if(pos_read >= buff_size)
	{
		if(pos_read == buff_size)
		{
			// Buffer wrapped.
			pos_read = 0;
		}
		else
		{
			fprintf(stderr, "Logic error: read pos exceeded end of buffer!\n");
			abort();
		}
	}

	if((pos_read + toRead) > buff_size)
	{
		fprintf(stderr, "Internal error: Would read past end of buffer!\n");
		abort();
	}

	size_t written = fwrite(entbuff + pos_read, 1, toRead, fdRandom);
	if(g_log_reads)
	{
		fprintf(stderr, "Read. written(%zu) = pos_read(%d), toRead(%zu)\n", written, pos_read, toRead);
	}
	pos_read += written;
	if(pos_read > buff_size)
	{
		fprintf(stderr, "Internal error: READ past end of buffer!\n");
		abort();
	}
	return written;
}

size_t read_from_buffer(const size_t toRead)
{
	if(g_log_reads)
		fprintf(stderr, "%zu bytes buffer -> random device\n", toRead);

	// If we don't have any bytes left to read, nullop.
	if(pos_write == pos_read)
	{
		if(g_log_reads)
			fprintf(stderr, "Buffer empty.\n");
		return 0;
	}

	// First, let's cap our read to however many bytes we have in the buffer.
	const size_t first_read_remaining = get_read_remaining();
	const size_t capped_read = toRead > first_read_remaining  ? first_read_remaining : toRead;
	const size_t distance_to_end = buff_size - pos_read;

	// First read: From here up to the edge of our buffer, or until we've
	// satisfied our read quantity, whichever comes first.
	
	const size_t first_read_qty = capped_read > distance_to_end ? distance_to_end : capped_read;

	if(g_log_reads)
	{
		fprintf(stderr, "First read: frr(%zu), cr(%zu), dte(%zu), frq(%zu)\n", first_read_remaining, capped_read, distance_to_end, first_read_qty);
	}
	const size_t first_bytes_read = read_from_buffer_internal(first_read_qty);

	// Now, we may need to do one or two operations, depending on if our
	// read range goes over the buffer wrap.
	// All done here?
	if(first_read_qty == capped_read)
		return first_bytes_read;

	// Not quite; our buffer just wrapped, so we get to go again.
	const size_t second_read_qty = capped_read - first_read_qty;

	if(g_log_reads)
	{
		fprintf(stderr, "Second read: srq(%zu)\n", second_read_qty);
	}
	const size_t second_bytes_read = read_from_buffer_internal(second_read_qty);
	
	return second_bytes_read;
}

bool g_log_writes = false;
size_t write_to_buffer_internal(size_t toWrite)
{
	if(pos_write >= buff_size)
	{
		if(pos_write == buff_size)
		{
			// Buffer wrapped.
			pos_write = 0;
		}
		else
		{
			fprintf(stderr, "Logic error: write pos exceeded end of buffer!\n");
			abort();
		}
	}
	
	if((pos_write + toWrite) > buff_size)
	{
		fprintf(stderr, "Logic error: Would write past end of buffer!\n");
		abort();
	}
	size_t bytes_written = fread(entbuff + pos_write, 1, toWrite, fdRandom);
	if(g_log_reads)
	{
		fprintf(stderr, "Write. written(%zu) = pos_write(%d), toWrite(%zu)\n", bytes_written, pos_write, toWrite);
	}
	pos_write += bytes_written;
	if(pos_write > buff_size)
	{
		fprintf(stderr, "Logic error: WROTE past end of buffer!\n");
	}

	return bytes_written;
}

size_t write_to_buffer(size_t toWrite)
{
	if(g_log_writes)
		fprintf(stderr, "%zu bytes random device -> buffer\n", toWrite);

	// If our buffer is full, nullop.
	if(get_read_remaining() == buff_size)
	{
		if(g_log_writes)
			fprintf(stderr, "Buffer full.\n");
		return 0;
	}

	// First, let's cap our write to however many bytes we can still fit in the buffer.
	const size_t first_write_remaining = buff_size - get_read_remaining();
	const size_t capped_write = toWrite > first_write_remaining ? first_write_remaining : toWrite;
	const size_t distance_to_end = buff_size - pos_write;

	// First write: From here up to the edge of our buffer, or until we've
	// satisfied our write quantity, whichever comes first.

	const size_t first_write_qty = capped_write > distance_to_end ? distance_to_end : capped_write;

	if(g_log_writes)
	{
		fprintf(stderr, "bs(%d), grr(%zu), fwr(%zu), cw(%zu), dte(%zu), fwq(%zu), pw(%d)\n", buff_size, get_read_remaining(), first_write_remaining, capped_write, distance_to_end, first_write_qty, pos_write);
	}

	const size_t first_bytes_written = write_to_buffer_internal(first_write_qty);

	// All done here?
	if(first_write_qty == capped_write)
	{
		return first_bytes_written;
	}

	// Not quite; our buffer just wrapped, so we get to go again.
	const size_t second_write_qty = capped_write - first_write_qty;
	const size_t second_bytes_written = write_to_buffer_internal(second_write_qty);

	return second_bytes_written;
}

int check_ent()
{
	int entcnt;
        int r;
	r = ioctl(fileno(fdRandom), RNDGETENTCNT, &entcnt);
	if(0 > r) {
                fprintf(stderr, "Error with ioctl call: %s\n", strerror(errno));
		return -1;
        }
	return entcnt;
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
		"\t-R, --log-reads\t\tPrint diagnostic information when entropy is read by us from the random device.\n"
		"\t-W, --log-writes\t\tPrint diagnostic information when entropy is written to the random device.\n"
		"\t-b, --buffer-size=BYTES\t\tSet the size of our internal entropy buffer.\n"
		"\t-h, --help\t\tThis help message\n";

	fprintf(stderr, usage, argv[0]);
}

int main(int argc, char* argv[])
{
	// Knobs, tunables and argument processing.
	int entthresh_high = 4096 * 1 / 2; // -i --high-thresh
	int entthresh_low = 4096 * 1 / 16; // -l --low-thresh
        int waittime = 2500; // microseconds // -w --wait
	int printperiod = 1000; // milliseconds, roughly // -p --print-period
	char* rand_path = "/dev/random"; // -r --rand-path

	{
		// Use temporaries for argument input.
		int e_h = entthresh_high;
		int e_l = entthresh_low;
		int wt = waittime;
		int pp = printperiod;
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
				// yes, I realize the internal names are confusing.
				g_log_writes = true;
				break;
			case 'W':
				// yes, I realize the internal names are confusing.
				g_log_reads = true;
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
		if(e_l >= e_h)
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

		// Finally, assign our temporaries back.
		entthresh_high = e_h;
		entthresh_low = e_l;
		waittime = wt;
		printperiod = pp;
		rand_path = rp;
		buff_size = bs;
	}

	// Now start setting up our operations pieces.
	
	entbuff = mmap(NULL, buff_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if(MAP_FAILED == entbuff)
	{
                fprintf(stderr, "Error with mmap call: %s\n", strerror(errno));
		return 1;
	}
	if(NULL == entbuff)
	{
		fprintf(stderr, "mmap returned NULL?!: %s\n", strerror(errno));
		return 1;
	}

	int at_res = atexit(unmap_ent); // We mapped, so unmap when we're done.
	if(0 != at_res)
	{
		fprintf(stderr, "Warning: failed to register unmap_ent with atexit()");
	}

	fdRandom = fopen(rand_path, "rw");
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


        // loop here, calling ioctl(rfd, RNDGETENTCNT) and printing the result
        int entcnt = check_ent();
	size_t slept = 0;
	while( -1 != entcnt)
	{
                int ures = usleep(waittime);
		slept += waittime;
		if(slept >= (1000 * printperiod))
		{
			fprintf(stderr, "entcnt(%d), buffer(%zu)\n", entcnt, get_read_remaining());
			slept = 0;
		}
		entcnt = check_ent();
		if ( (entcnt > entthresh_high) || (entcnt < entthresh_low) )
		{
			// entcnt and entthresh_* are in bits, but we can only work in multiples
			// of 8 bits.
			if(((entcnt - entthresh_high) / 8) > 0)
			{
				// Let's pull some bytes in for later.
				write_to_buffer((entcnt - entthresh_high) / 8);
			}
			else if(((entthresh_low - entcnt) / 8) > 0)
			{
				// Let's get that back up to the threshold.
				read_from_buffer((entthresh_low - entcnt) / 8);
			}
		}
	}

        return 0;
}

