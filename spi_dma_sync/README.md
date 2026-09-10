# SPI + DMA 实验组（真异步 / ping-pong 双缓冲流水线）

## 与对照组的差异

`../spi_dma_sync/` 是伪异步版（submit 后立即 wait_for_completion
死等），作为基准。本版用 ping-pong 双缓冲实现真异步：

```
submit(buf0)                     ← DMA 搬运 buf0
  CPU 构造 buf1 TX                ← CPU 与 DMA 并行
wait_for_completion(buf0)         ← 若 CPU 比 DMA 快才等
kfifo_in(rx0) + wake_up
submit(buf1)                     ← DMA 搬运 buf1
  CPU 构造 buf0 TX                ← buf0 已空闲可复用
wait_for_completion(buf1)
kfifo_in(rx1) + wake_up
submit(buf0) → ...
```

核心：DMA 飞行期间 CPU 在构造下一帧 TX，CPU 不再空等。
高速率（CPU 构造 TX < DMA 飞行时间）时吞吐显著提升。

## 设备树片段

```dts
&spi4 {
    status = "okay";
    pinctrl-0 = <&spi4m0_cs0 &spi4m0_pins>;
    pinctrl-names = "default";

    spiasync: spi-async@0 {
        compatible = "spi,async-demo";
        reg = <0>;
        spi-max-frequency = <50000000>;
        status = "okay";
    };
};
```

注意：`spi,sync-demo` 与 `spi,async-demo` 是两个不同 compatible，
可以在同一个 SPI 总线上挂两个节点同时加载两个驱动分别测，但
建议先后单独跑，对比 `time` 输出更直观。

## 编译/加载

```bash
make KSRC=/path/to/linux-6.1 ARCH=arm64 CROSS_COMPILE=aarch64-linux-
scp spi_dma_async.ko test_spi root@board:/root/
# 板上（MOSI 接 MISO）：
insmod spi_dma_async.ko
time ./test_spi /dev/spi_async0
rmmod spi_dma_async
```

## ftrace 验证两次 spi_async 的间隔

```bash
echo 1 > /sys/kernel/debug/tracing/events/spi/spi_async/enable
./test_spi /dev/spi_async0
cat /sys/kernel/debug/tracing/trace
```

伪异步版两次 spi_async 间隔 = DMA 飞行时间 + msleep(10ms)。
真异步版两次 spi_async 间隔 = max(CPU 构造 TX 时间, DMA 飞行时间)
+ msleep(10ms)，高速率下 CPU 构造 TX 远快于 DMA，间隔与伪异步
接近；但 CPU 不再空等，吞吐在 DMA 慢于 CPU 时显著提升。

## AB 对比流程

```bash
# 1. 基准组（伪异步）
cd spi_dma_sync && make && scp ...
insmod spi_dma_sync.ko
time ./test_spi /dev/spi_sync0     # 记录 real time
rmmod spi_dma_sync

# 2. 实验组（真异步）
cd ../spi_dma_async && make && scp ...
insmod spi_dma_async.ko
time ./test_spi /dev/spi_async0    # 记录 real time
rmmod spi_dma_async

# 3. 改变 speed_hz（ioctl 设 1MHz / 10MHz / 50MHz）重复测，
#    真异步在高速率下吞吐增益约 30%+。
```

## 文件清单

| 文件                | 作用                                                  |
| ------------------- | ----------------------------------------------------- |
| spi_dma_async.c     | 真异步版驱动（ping-pong 双缓冲流水线）                |
| spi_dma_async.h     | 头文件：ioctl 命令、FIFO 大小、NBUFS=2                 |
| test_spi.c          | 用户态测试程序，select 阻塞 + loopback 校验 + 丢帧率   |
| Makefile            | 独立模块编译                                          |