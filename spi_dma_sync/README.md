# SPI + DMA 对比基准组（伪异步 / 等价 spi_sync）

## 这是"对照组"，不是"真异步"

spi_async 提交后立即 `wait_for_completion` 死等硬件，CPU 在 DMA 搬运
期间什么也没做。语义上和 `spi_sync` 完全等价（spi_sync 内部实现
就是 `spi_async + wait_for_completion`）。本版用来和 `spi_dma_async/`
里的 ping-pong 双缓冲真异步做 AB 对比，量化"真异步"相对"伪异步"
的吞吐增益。

## 设备树片段

```dts
&spi4 {
    status = "okay";
    pinctrl-0 = <&spi4m0_cs0 &spi4m0_pins>;
    pinctrl-names = "default";

    spisync: spi-sync@0 {
        compatible = "spi,sync-demo";
        reg = <0>;
        spi-max-frequency = <50000000>;
        status = "okay";
    };
};
```

## 编译/加载

```bash
make KSRC=/path/to/linux-6.1 ARCH=arm64 CROSS_COMPILE=aarch64-linux-
scp spi_dma_sync.ko test_spi root@board:/root/
# 板上（MOSI 接 MISO）：
insmod spi_dma_sync.ko
time ./test_spi /dev/spi_sync0
rmmod spi_dma_sync
```

## 用 ftrace 看每次 spi_async 的间隔

```bash
echo 1 > /sys/kernel/debug/tracing/events/spi/spi_async/enable
./test_spi /dev/spi_sync0
cat /sys/kernel/debug/tracing/trace
```

伪异步版每次 submit 后立即 wait_for_completion，所以两次
spi_async 的时间差 = DMA 飞行时间 + msleep(10ms)。
真异步版两次 submit 的时间差 = CPU 构造下一帧 TX 的时间
（通常 < DMA 飞行时间，所以被 wait 隐藏）。

## 对比另一组

`../spi_dma_async/` 下的真异步流水线版作为对照组。
两版的设备树 compatible 不同（"spi,sync-demo" vs
"spi,async-demo"），可以在同一 SPI 总线上挂两个节点同时
加载两个驱动分别测，但实测更推荐先后单独跑、对比 time 输出。