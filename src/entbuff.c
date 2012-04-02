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

#include <linux/random.h>

int looping = 1;

int pos_write = 0;
int buff_read_remaining = 0; // This many bytes left in the buffer
int buff_size = 8388608;
size_t read_in_count = 0;
size_t read_out_count = 0;
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

size_t read_from_buffer_internal(size_t toRead, size_t readPos)
{
	// We let the caller handle concerns about how many bytes we can read,
	// and where we can read from.	

	return fwrite(entbuff + readPos, toRead, 1, fdRandom);
}

size_t read_from_buffer(size_t toRead)
{
	// If we don't have any bytes left to read, nullop.
	if(0 == buff_read_remaining)
		return 0;
	
	// Where in the buffer do we read from?
	// pos_write is the next byte which will be written.
	// So pos_write - 1 will be the last valid written byte.
	size_t buff_read_pos = (pos_write - 1) - buff_read_remaining;

	// Did our starting position wrap?
	if(buff_read_pos < 0)
		// Buffer wrapped.
		buff_read_pos += buff_size;

	// Now, we may need to do one or two operations, depending on if our
	// read range goes over the buffer wrap.
	size_t first_read = (toRead < (buff_size - buff_read_pos)) ? toRead : (buff_size - buff_read_pos);
	size_t bytes_read = read_from_buffer_internal(first_read, buff_read_pos);
	toRead -= bytes_read;
	buff_read_remaining -= bytes_read;
	read_out_count += bytes_read;

	if(( toRead > 0) && (buff_read_remaining > 0))
	{
		size_t bytes_read_second = read_from_buffer_internal(toRead, buff_read_pos);
		buff_read_remaining -= bytes_read_second;
		read_out_count += bytes_read_second;
		bytes_read += bytes_read_second;
	}
	
	return bytes_read;
}

size_t write_to_buffer_internal(size_t toWrite, size_t writePos)
{
	// Caller can worry about buffer bounds
	return fread(entbuff + writePos, toWrite, 1, fdRandom);
}

size_t write_to_buffer(size_t toWrite)
{
	// If our buffer is full, nullop.
	if(buff_size == buff_read_remaining)
		return 0;

	// If we don't have enough space in the buffer to add as many bytes as
	// we're asked, reduce how many bytes we'll add.
	toWrite = toWrite < (buff_size - buff_read_remaining) ? toWrite : (buff_size - buff_read_remaining);
	

	// If we don't have any bytes left to work with, drop out.
	if(toWrite == 0)
		return 0;

	// Buffer is wrapping.
	if(pos_write >= buff_size)
		pos_write -= buff_size;

	// Now we need to do one or two operations, depending on if our
	// write range goes over the buffer wrap
	size_t first_write = (toWrite <= (buff_size - pos_write)) ? toWrite : (buff_size - pos_write);
	size_t bytes_written = write_to_buffer_internal(toWrite, pos_write);
	toWrite -= bytes_written;
	buff_read_remaining += bytes_written;
	pos_write += bytes_written;
	read_in_count += bytes_written;

	if((toWrite > 0 && buff_size) > (buff_read_remaining))
	{
		size_t bytes_written_second = write_to_buffer_internal(toWrite, pos_write);
		buff_read_remaining += bytes_written_second;
		pos_write += bytes_written_second;
		read_in_count += bytes_written_second;
		bytes_written += bytes_written_second;
	}

	return bytes_written;
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

		const char shortopts[] = "i:l:w:p:r:h";
		static const struct option longopts[] = {
			{ "high-thresh", required_argument, NULL, 'i' },
			{ "low-thresh", required_argument, NULL, 'l' },
			{ "wait", required_argument, NULL, 'w'},
			{ "print-period", required_argument, NULL, 'p'},
			{ "rand-path", required_argument, NULL, 'r'},
			{ "help", no_argument, NULL, 'h'},
			{ NULL, NULL, NULL, NULL }
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

		// Finally, assign our temporaries back.
		entthresh_high = e_h;
		entthresh_low = e_l;
		waittime = wt;
		printperiod = pp;
		rand_path = rp;
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
			fprintf(stderr, "entcnt(%d), buffer(%d) read(%llu) write(%llu)\n", entcnt, buff_read_remaining, (unsigned long long int) read_in_count, (unsigned long long int) read_out_count);
			slept = 0;
		}
		entcnt = check_ent();
		if ( (entcnt > entthresh_high) || (entcnt < entthresh_low) )
		{
			if(entcnt > entthresh_high)
			{
				// Let's pull some bytes in for later.
				write_to_buffer((entcnt - entthresh_high) / 8);
			}
			else if(entcnt < entthresh_low)
			{
				// Let's get that back up to the threshold.
				read_from_buffer((entthresh_low - entcnt) / 8);
			}
		}
	}

        return 0;
}

