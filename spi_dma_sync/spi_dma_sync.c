#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/kfifo.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/mutex.h>

#include "spi_dma_sync.h"


struct spi_dma_data {
	struct spi_device	*spi;
	struct spi_message	msg;
	struct spi_transfer	xfer;
	struct completion	done;
	bool			xfer_busy;

	u8			*tx_buf;
	u8			*rx_buf;
	DECLARE_KFIFO(rx_fifo, u8, SPI_DMA_FIFO_SIZE);

	wait_queue_head_t	rx_wait;

	struct task_struct	*kthread;
	bool			running;

	struct cdev		cdev;
	dev_t			devno;
	struct class		*cls;
	struct device		*dev;

	struct mutex		config_lock;
	unsigned int		speed_hz;
	unsigned int		bits_per_word;
	unsigned int		mode;
};

static void spi_dma_complete(void *arg)
{
	struct spi_dma_data *data = arg;

	/* DMA 完成中断上下文，禁止睡眠，仅唤醒等待者 */
	data->xfer_busy = false;
	complete(&data->done);
}

static int spi_dma_kthread_fn(void *arg)
{
	struct spi_dma_data *data = arg;
	unsigned int len;
	int ret;

	while (!kthread_should_stop()) {
		static u8 counter;
		int i;

		for (i = 0; i < SPI_DMA_XFER_BYTES; i++)
			data->tx_buf[i] = counter++;

		spi_message_init(&data->msg);
		memset(&data->xfer, 0, sizeof(data->xfer));
		data->xfer.tx_buf	= data->tx_buf;
		data->xfer.rx_buf	= data->rx_buf;
		data->xfer.len		= SPI_DMA_XFER_BYTES;

		mutex_lock(&data->config_lock);
		data->xfer.speed_hz		= data->speed_hz;
		data->xfer.bits_per_word	= data->bits_per_word;
		data->xfer.cs_change		= (data->mode & SPI_CS_HIGH) ? 0 : 1;
		mutex_unlock(&data->config_lock);

		data->msg.complete	= spi_dma_complete;
		data->msg.context	= data;
		data->xfer_busy		= true;
		reinit_completion(&data->done);

		/* 提交后立即返回 */
		ret = spi_async(data->spi, &data->msg);
		if (ret) {
			dev_err(&data->spi->dev, "spi_async err %d\n", ret);
			msleep(SPI_DMA_INTERVAL_MS);
			continue;
		}

		/* 关键：submit 后立即死等，CPU 空等 DMA 完成 */
		wait_for_completion(&data->done);

		len = kfifo_in(&data->rx_fifo, data->rx_buf,
			       SPI_DMA_XFER_BYTES);
		if (len < SPI_DMA_XFER_BYTES)
			dev_dbg(&data->spi->dev, "rx fifo overflow\n");
		wake_up_interruptible(&data->rx_wait);

		msleep(SPI_DMA_INTERVAL_MS);
	}

	return 0;
}

static int spi_dma_open(struct inode *inode, struct file *filp)
{
	struct spi_dma_data *data = container_of(inode->i_cdev,
						 struct spi_dma_data, cdev);
	filp->private_data = data;

	if (!data->running) {
		data->running = true;
		data->kthread = kthread_run(spi_dma_kthread_fn, data,
					    "spi_dma_sync");
		if (IS_ERR(data->kthread)) {
			data->running = false;
			return PTR_ERR(data->kthread);
		}
	}
	return 0;
}

static int spi_dma_release(struct inode *inode, struct file *filp)
{
	struct spi_dma_data *data = filp->private_data;

	if (data->running) {
		kthread_stop(data->kthread);
		data->running = false;
	}
	return 0;
}

static ssize_t spi_dma_read(struct file *filp, char __user *buf,
			    size_t count, loff_t *ppos)
{
	struct spi_dma_data *data = filp->private_data;
	unsigned int copied;
	int ret;

	if (kfifo_is_empty(&data->rx_fifo)) {
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		ret = wait_event_interruptible(data->rx_wait,
				!kfifo_is_empty(&data->rx_fifo));
		if (ret)
			return ret;
	}

	ret = kfifo_to_user(&data->rx_fifo, buf, count, &copied);
	if (ret)
		return ret;

	return copied;
}

static __poll_t spi_dma_poll(struct file *filp, poll_table *wait)
{
	struct spi_dma_data *data = filp->private_data;
	__poll_t mask = 0;

	poll_wait(filp, &data->rx_wait, wait);

	if (!kfifo_is_empty(&data->rx_fifo))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

static long spi_dma_ioctl(struct file *filp, unsigned int cmd,
			  unsigned long arg)
{
	struct spi_dma_data *data = filp->private_data;
	unsigned int val;
	int ret = 0;

	if (_IOC_SIZE(cmd) > sizeof(val))
		return -EINVAL;
	if (_IOC_DIR(cmd) & _IOC_READ)
		ret = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
	if (!ret && (_IOC_DIR(cmd) & _IOC_WRITE)) {
		ret = copy_from_user(&val, (void __user *)arg,
				     sizeof(val)) ? -EFAULT : 0;
	}
	if (ret)
		return ret;

	mutex_lock(&data->config_lock);
	switch (cmd) {
	case SPI_DMA_IOC_SET_SPEED:
		data->speed_hz = val;
		break;
	case SPI_DMA_IOC_SET_BITS:
		data->bits_per_word = val;
		break;
	case SPI_DMA_IOC_SET_MODE:
		data->mode = val;
		break;
	case SPI_DMA_IOC_GET_SPEED:
		val = data->speed_hz;
		mutex_unlock(&data->config_lock);
		return put_user(val, (unsigned int __user *)arg);
	default:
		ret = -ENOTTY;
	}
	mutex_unlock(&data->config_lock);
	return ret;
}

static const struct file_operations spi_dma_fops = {
	.owner		= THIS_MODULE,
	.open		= spi_dma_open,
	.release	= spi_dma_release,
	.read		= spi_dma_read,
	.poll		= spi_dma_poll,
	.unlocked_ioctl	= spi_dma_ioctl,
};

static int spi_dma_probe(struct spi_device *spi)
{
	struct spi_dma_data *data;
	int ret;

	data = devm_kzalloc(&spi->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->spi = spi;
	spi_set_drvdata(spi, data);

	data->speed_hz		= 1000000;
	data->bits_per_word	= 8;
	data->mode		= SPI_MODE_0;

	mutex_init(&data->config_lock);
	init_completion(&data->done);
	init_waitqueue_head(&data->rx_wait);
	INIT_KFIFO(data->rx_fifo);

	data->tx_buf = devm_kmalloc(&spi->dev, SPI_DMA_XFER_BYTES,
				    GFP_KERNEL | GFP_DMA);
	data->rx_buf = devm_kmalloc(&spi->dev, SPI_DMA_XFER_BYTES,
				    GFP_KERNEL | GFP_DMA);
	if (!data->tx_buf || !data->rx_buf)
		return -ENOMEM;

	ret = alloc_chrdev_region(&data->devno, 0, 1, SPI_DMA_NAME);
	if (ret)
		return ret;

	cdev_init(&data->cdev, &spi_dma_fops);
	data->cdev.owner = THIS_MODULE;
	ret = cdev_add(&data->cdev, data->devno, 1);
	if (ret)
		goto err_unreg_chr;

	data->cls = class_create(THIS_MODULE, SPI_DMA_NAME);
	if (IS_ERR(data->cls)) {
		ret = PTR_ERR(data->cls);
		goto err_cdev_del;
	}

	data->dev = device_create(data->cls, &spi->dev, data->devno,
				  NULL, SPI_DMA_DEV);
	if (IS_ERR(data->dev)) {
		ret = PTR_ERR(data->dev);
		goto err_class_destroy;
	}

	dev_info(&spi->dev, "spi_dma_sync probe ok, /dev/%s\n", SPI_DMA_DEV);
	return 0;

err_class_destroy:
	class_destroy(data->cls);
err_cdev_del:
	cdev_del(&data->cdev);
err_unreg_chr:
	unregister_chrdev_region(data->devno, 1);
	return ret;
}

static void spi_dma_remove(struct spi_device *spi)
{
	struct spi_dma_data *data = spi_get_drvdata(spi);

	if (data->running)
		kthread_stop(data->kthread);

	device_destroy(data->cls, data->devno);
	class_destroy(data->cls);
	cdev_del(&data->cdev);
	unregister_chrdev_region(data->devno, 1);
}

static const struct of_device_id spi_dma_of_match[] = {
	{ .compatible = "spi,sync-demo" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, spi_dma_of_match);

static struct spi_driver spi_dma_driver = {
	.driver = {
		.name		= SPI_DMA_NAME,
		.of_match_table = spi_dma_of_match,
	},
	.probe		= spi_dma_probe,
	.remove		= spi_dma_remove,
};

module_spi_driver(spi_dma_driver);

MODULE_AUTHOR("Guxinxu");
MODULE_DESCRIPTION("SPI + DMA sync (fake async) demo - baseline");
MODULE_LICENSE("GPL");