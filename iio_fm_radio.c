/**
 * Copyright (C) 2012-2013 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/

#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include "iio_utils.h"

#define IIO_BLOCK_ALLOC_IOCTL   _IOWR('i', 0xa0, struct iio_buffer_block_alloc_req)
#define IIO_BLOCK_FREE_IOCTL    _IO('i', 0xa1)
#define IIO_BLOCK_QUERY_IOCTL   _IOWR('i', 0xa2, struct iio_buffer_block)
#define IIO_BLOCK_ENQUEUE_IOCTL _IOWR('i', 0xa3, struct iio_buffer_block)
#define IIO_BLOCK_DEQUEUE_IOCTL _IOWR('i', 0xa4, struct iio_buffer_block)

struct iio_buffer_block_alloc_req {
	__u32 type;
	__u32 size;
	__u32 count;
	__u32 id;
};

struct iio_buffer_block {
	__u32 id;
	__u32 size;
	__u32 bytes_used;
	__u32 type;
	__u32 flags;
	union {
		__u32 offset;
	} data;
	__u64 timestamp;
};

struct block {
	struct iio_buffer_block block;
	short *addr;
};

static struct block blocks[5];

/* Min and max are used for automatic gain control and DC offset control */
static int min = 0xfffffff;
static int max = -0xfffffff;

#define DECIMATION_FACTOR 48
#define AUDIO_SAMPLE_RATE 48000

static int demodulate(struct iio_buffer_block *block)
{
	int new_min, new_max;
	long i[3], q[3], di, dq;
	long long sample = 0;
	unsigned int j;
	unsigned int sub = 4;
	unsigned int x = 0;
	unsigned int n = 0;
	short *sample_buffer;
	size_t num_bytes, offset;
	int ret;

	new_min = 0xfffffff;
	new_max = -0xfffffff;

	sample_buffer = malloc(block->bytes_used / DECIMATION_FACTOR / 2);

	i[2] = blocks[block->id].addr[0];
	q[2] = blocks[block->id].addr[1];
	i[1] = blocks[block->id].addr[2];
	q[1] = blocks[block->id].addr[3];

	x = 0;
	for (j = 2; j < block->bytes_used / 2; j += 2 * sub) {

		/* FM demodulation implemented as described in
		 * http://www.embedded.com/design/embedded/4212086/DSP-Tricks--Frequency-demodulation-algorithms-
		 */
		i[0] = blocks[block->id].addr[j];
		q[0] = blocks[block->id].addr[j + 1];

		di = i[0] - i[2];
		dq = q[0] - q[2];

		sample += (i[1] * dq - q[1] * di);

		i[2] = i[1];
		q[2] = q[1];
		i[1] = i[0];
		q[1] = q[0];

		x += sub;
		if (x >= DECIMATION_FACTOR) {
			x = 0;
			sample /= (DECIMATION_FACTOR / sub);

			if (sample < new_min)
				new_min = sample;
			if (sample > new_max)
				new_max = sample;

			if (min >= max)
				continue;

			sample -= (max - min) / 2;
			sample = sample * 0x1fff / (max - min);
			if (sample > 0x1fff)
				sample = 0x1fff;
			else if(sample < -0x1fff)
				sample = -0x1fff;

			sample_buffer[n] = sample;	
			n++;
			sample = 0;
		}
	}

	min = new_min;
	max = new_max;

	if (n == 0)
		return 0;

	num_bytes = 2 * n;
	offset = 0;

	do {
		ret = write(STDOUT_FILENO, sample_buffer + offset, num_bytes);
		if (ret <= 0)
			break;
		num_bytes -= ret;
		offset += ret;
	} while (num_bytes);
		
	free(sample_buffer);

	if (ret == 0) {
		fprintf(stderr, "Failed to write samples to stdout: EOF\n");
		return -1;
	}

	if (ret == -1) {
		perror("Failed to write samples to stdout");
		return -1;
	}

	return 0;
}

static int app_running = 1;

static void terminate(int signal)
{
	app_running = 0;
}

static void setup_sigterm_handler(void)
{
	struct sigaction action = {
		.sa_handler = terminate,
	};

	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGHUP, &action, NULL);
	sigaction(SIGINT, &action, NULL);
	sigaction(SIGPIPE, &action, NULL);
}

#define ALIGN(x, y) ((x) / (y)) * (y)

/**
 * Usage: `iio_fm_radio [frequency]`
 */
int main(int argc, char *argv[])
{
	struct iio_buffer_block_alloc_req req;
	struct iio_buffer_block block;
	unsigned int sample_rate;
	int fd, ret;
	int i;

	sample_rate = DECIMATION_FACTOR * AUDIO_SAMPLE_RATE;
	if (sample_rate <= 2083333) {
		fprintf(stderr, "Sample rate of %u is to low\n", sample_rate);
		return EXIT_FAILURE;
	}

	setup_sigterm_handler();

	req.type = 0x0;
	req.size = ALIGN(0x100000, sizeof(uint16_t) * 2 * DECIMATION_FACTOR);
	req.count = 4;

	ret = set_dev_paths("cf-ad9361-lpc");
	if (ret < 0) {
		perror("Failed to find 'cf-ad9361-lpc' device");
		exit(1);
	}

	fd = iio_buffer_open(true, O_RDWR);
	if (fd < 0) {
		perror("Failed to open the device buffer");
		exit(1);
	}

	/* Select I and Q data of the first channel */
	write_devattr_int("scan_elements/in_voltage0_en", 1);
	write_devattr_int("scan_elements/in_voltage1_en", 1);
	write_devattr_int("scan_elements/in_voltage2_en", 0);
	write_devattr_int("scan_elements/in_voltage3_en", 0);

	/* Setup the phy */
	set_dev_paths("ad9361-phy");
	write_devattr_int("in_voltage_sampling_frequency", sample_rate);
	/* Set bandwidth to 300 kHz */
	write_devattr_int("in_voltage_rf_bandwidth", 300000);

	if (argc > 1) {
		float freq;
		freq = atof(argv[1]);
		if (freq < 1000)
			freq *= 1000000;
		write_devattr_int("out_altvoltage0_RX_LO_frequency", freq);
	}

	/* Allocate and mmap buffer blocks */
	ret = ioctl(fd, IIO_BLOCK_ALLOC_IOCTL, &req);
	if (ret < 0) {
		perror("Failed to allocate memory blocks");
		exit(1);
	}
	for (i = 0; i < req.count; i++) {
		blocks[i].block.id = i;
		ret = ioctl(fd, IIO_BLOCK_QUERY_IOCTL, &blocks[i].block);
		if (ret) {
			perror("Failed to query block");
			exit(1);
		}

		blocks[i].addr = mmap(0, blocks[i].block.size, PROT_READ,
			MAP_SHARED, fd, blocks[i].block.data.offset);
		if (blocks[i].addr == MAP_FAILED) {
			perror("Failed to mmap block");
			exit(1);
		}

		ret = ioctl(fd, IIO_BLOCK_ENQUEUE_IOCTL, &blocks[i].block);
		if (ret) {
			perror("Failed to enqueue block");
			exit(1);
		}

		fprintf(stderr, "Sucessfully mapped block %d (offset %x, size %d) at %p\n",
			i, blocks[i].block.data.offset, blocks[i].block.size,
			blocks[i].addr);
	}

	fprintf(stderr, "Starting FM modulation\n");

	set_dev_paths("cf-ad9361-lpc");
	write_devattr_int("buffer/enable", 1);

	while (app_running) {
		ret = ioctl(fd, IIO_BLOCK_DEQUEUE_IOCTL, &block);
		if (ret) {
			perror("Failed to dequeue block");
			break;
		}
		ret = demodulate(&block);
		if (ret)
			break;
		ret = ioctl(fd, IIO_BLOCK_ENQUEUE_IOCTL, &block);
		if (ret) {
			perror("Failed to enqueue block");
			break;
		}
	}

	write_devattr_int("buffer/enable", 0);

	fprintf(stderr, "Stopping FM modulation\n");

	for (i = 0; i < req.count; i++)
		munmap(blocks[i].addr, blocks[i].block.size);	

	ioctl(fd, IIO_BLOCK_FREE_IOCTL, 0);
	close(fd);

	return 0;
}
