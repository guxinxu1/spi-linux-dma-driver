#ifndef _SPI_DMA_ASYNC_H
#define _SPI_DMA_ASYNC_H

#include <linux/types.h>

#define SPI_DMA_NAME		"spi_dma_async"
#define SPI_DMA_DEV		"spi_async0"

#define SPI_DMA_FIFO_SIZE	4096
#define SPI_DMA_XFER_BYTES	256
#define SPI_DMA_INTERVAL_MS	10
#define SPI_DMA_NBUFS		2		/* ping-pong 双缓冲数量 */

#define SPI_DMA_IOC_MAGIC	'A'
#define SPI_DMA_IOC_SET_SPEED	_IOW(SPI_DMA_IOC_MAGIC, 1, unsigned int)
#define SPI_DMA_IOC_SET_BITS	_IOW(SPI_DMA_IOC_MAGIC, 2, unsigned int)
#define SPI_DMA_IOC_SET_MODE	_IOW(SPI_DMA_IOC_MAGIC, 3, unsigned int)
#define SPI_DMA_IOC_GET_SPEED	_IOR(SPI_DMA_IOC_MAGIC, 4, unsigned int)

#endif /* _SPI_DMA_ASYNC_H */