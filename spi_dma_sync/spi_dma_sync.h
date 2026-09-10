#ifndef _SPI_DMA_SYNC_H
#define _SPI_DMA_SYNC_H

#include <linux/types.h>

#define SPI_DMA_NAME		"spi_dma_sync"
#define SPI_DMA_DEV		"spi_sync0"

#define SPI_DMA_FIFO_SIZE	4096
#define SPI_DMA_XFER_BYTES	256
#define SPI_DMA_INTERVAL_MS	10

#define SPI_DMA_IOC_MAGIC	'S'
#define SPI_DMA_IOC_SET_SPEED	_IOW(SPI_DMA_IOC_MAGIC, 1, unsigned int)
#define SPI_DMA_IOC_SET_BITS	_IOW(SPI_DMA_IOC_MAGIC, 2, unsigned int)
#define SPI_DMA_IOC_SET_MODE	_IOW(SPI_DMA_IOC_MAGIC, 3, unsigned int)
#define SPI_DMA_IOC_GET_SPEED	_IOR(SPI_DMA_IOC_MAGIC, 4, unsigned int)

#endif /* _SPI_DMA_SYNC_H */