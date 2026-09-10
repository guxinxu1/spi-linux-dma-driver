ifneq ($(KERNELRELEASE),)

obj-m += spi_dma_sync.o

else

KSRC ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KSRC) M=$(PWD) modules
	$(CROSS_COMPILE)gcc test_spi.c -o test_spi

clean:
	$(MAKE) -C $(KSRC) M=$(PWD) clean
	rm -f test_spi

endif