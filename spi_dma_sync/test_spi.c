/* 用户态测试程序：读 /dev/spi_sync0
 * 与 spi_dma_async/ 下的 test_spi.c 源码一致，仅设备节点名不同，
 * 方便两版分别运行后对比 time/丢帧率。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include "spi_dma_sync.h"

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "/dev/spi_sync0";
	int fd = open(path, O_RDWR);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	unsigned int speed = 5000000;
	ioctl(fd, SPI_DMA_IOC_SET_SPEED, &speed);

	printf("waiting data from %s ...\n", path);

	u8 rbuf[SPI_DMA_XFER_BYTES];
	int total = 0, lost = 0;
	u8 expect = 0;

	while (total < 10000) {
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);

		struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
		int ret = select(fd + 1, &rfds, NULL, NULL, &tv);
		if (ret < 0) { perror("select"); break; }
		if (ret == 0) {
			printf("select timeout\n");
			continue;
		}

		int n = read(fd, rbuf, sizeof(rbuf));
		if (n <= 0) continue;

		for (int i = 0; i < n; i++) {
			if (rbuf[i] != expect) {
				lost += (rbuf[i] > expect) ?
					(rbuf[i] - expect) : (256 + rbuf[i] - expect);
				expect = rbuf[i] + 1;
			} else {
				expect++;
			}
		}
		total += n;

		if (total % 2560 == 0)
			printf("recv %d bytes, lost %d\n", total, lost);
	}

	printf("done: total=%d lost=%d\n", total, lost);
	close(fd);
	return 0;
}