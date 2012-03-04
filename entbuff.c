#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>

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

int read_from_buffer()
{
	// If we don't have any bytes left to read, nullop.
	if(0 == buff_read_remaining)
		return 0;
	
	// Where in the buffer do we read from?
	// pos_write is the next byte which will be written.
	// So pos_write - 1 will be the last valid written byte.
	int buff_read_pos = (pos_write - 1) - buff_read_remaining;
	if(buff_read_pos < 0)
		// Buffer wrapped.
		buff_read_pos += buff_size;
	
	fwrite(entbuff + buff_read_pos, 1, 1, fdRandom);
	int brr_w = buff_read_remaining;
	--buff_read_remaining;
	++read_out_count;
	return 1;
}

int write_to_buffer()
{
	// If our buffer is full, nullop.
	if(buff_size == buff_read_remaining)
		return 0;

	// Buffer is wrapping.
	if(pos_write >= buff_size)
		pos_write -= buff_size;

	fread(entbuff + pos_write, 1, 1, fdRandom);
	int brr_w = buff_read_remaining;
	int pw_w = pos_write;
	++buff_read_remaining;
	++pos_write;
	++read_in_count;
	return 1;
}

int check_ent()
{
	int entcnt;
        int r;
	r = ioctl(fileno(fdRandom), RNDGETENTCNT, &entcnt);
	if(0 > r) {
		int e = errno;
                fprintf(stderr, "Error with ioctl call: %s\n", strerror(e));
		return -1;
        }
	return entcnt;
}

int main(int argc, char* argv[])
{
        int entcnt = 0;
	int entthresh_high = 4096 * 1 / 2;
	int entthresh_low = 4096 * 1 / 16;
        int waittime = 62500;
	int printperiod = 16;

	entbuff = mmap(NULL, buff_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if(MAP_FAILED == entbuff)
	{
		int e = errno;
                fprintf(stderr, "Error with mmap call: %s\n", strerror(e));
		return -1;
	}
	if(NULL == entbuff)
	{
		int e = errno;
		fprintf(stderr, "mmap returned NULL?!: %s\n", strerror(e));
		return -1;
	}

	int at_res = atexit(unmap_ent); // We mapped, so unmap when we're done.
	if(0 != at_res)
	{
		fprintf(stderr, "Warning: failed to register unmap_ent with atexit()");
	}

	fdRandom = fopen("/dev/random", "rw");
	if(0 == fdRandom)
	{
		int e = errno;
                fprintf(stderr, "Error with mmap call: %s\n", strerror(e));
		return -1;
	}

	at_res = atexit(close_fdRandom); // We opened the file, remember to close it.
	if(0 != at_res)
	{
		fprintf(stderr, "Warning: failed to register close_fdRandom with atexit()");
	}

        if(argc == 2) {
                waittime = atoi(argv[1]);
                if(waittime < 1) {
                        fprintf(stderr, "specified wait time cannot be less than 1\n");
                        return -1;
                }
        }
	fprintf(stderr, "wait time is %d, high threshold is %d, low threshold is %d\n", waittime, entthresh_high, entthresh_low);
        // loop here, calling ioctl(rfd, RNDGETENTCNT) and printing the result
	entcnt = check_ent();
	int printperiod_count = 0;
	while( -1 != entcnt)
	{
                int ures = usleep(waittime);
		if(printperiod_count++ >= printperiod)
		{
			fprintf(stderr, "entcnt(%d), buffer(%d) read(%llu) write(%llu)\n", entcnt, buff_read_remaining, (unsigned long long int) read_in_count, (unsigned long long int) read_out_count);
			printperiod_count = 0;
		}
		entcnt = check_ent();
		while	(
				(
					((entcnt >= entthresh_high) && (buff_read_remaining != buff_size))
					|| ((entcnt <= entthresh_low) && (buff_read_remaining != 0))
				)
			)
		{
			int res = 0;
			if(entcnt >= entthresh_high)
			{
				res = write_to_buffer();
			}
			else if(entcnt <= entthresh_low)
			{
				res = read_from_buffer();
			}
			else
			{
				fprintf(stderr, "BUG: entcnt(%d), etl(%d), eth(%d), brr(%d), bs(%d)\n", entcnt, entthresh_low, entthresh_high, buff_read_remaining, buff_size);
			}
			entcnt = check_ent();
			if(!res)
				// Ran out of space, don't bother calling read or write.
				break;
		}
	}

        return 0;
}

