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

int write_to_buffer(int toWrite)
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
		int e = errno;
                fprintf(stderr, "Error with ioctl call: %s\n", strerror(e));
		return -1;
        }
	return entcnt;
}

int main(int argc, char* argv[])
{
        int entcnt = 0;
	int entthresh_high = 4096 * 1 / 8;
	int entthresh_low = 4096 * 1 / 16;
        int waittime = 12500;
	int printperiod = 1000000 / waittime;

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
		if ( (entcnt > entthresh_high) || (entcnt < entthresh_low) )
		{
			if(entcnt > entthresh_high)
			{
				// Let's pull some bytes in for later.
				write_to_buffer(entcnt - entthresh_high);
			}
			else if(entcnt < entthresh_low)
			{
				// Let's get that back up to the threshold.
				read_from_buffer(entthresh_low - entcnt);
			}
		}
	}

        return 0;
}

