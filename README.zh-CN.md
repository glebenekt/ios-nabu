# ios-nabu

**Apple iPadOS 16.6.1 运行在 Xiaomi Pad 5（`nabu`）上。**

[English](README.md)

这个项目把 Apple 开源的 XNU 内核（xnu-8796，iOS 16 世代）移植到 Xiaomi Pad 5
（`nabu`，Qualcomm SM8150-AC / Snapdragon 860）。上面运行的是 **未修改的
iPadOS 16.6.1（20G81）iPad7,3 用户态**：launchd、系统 daemon、backboardd 和
SpringBoard。

**当前状态（2026-10-21）：** XNU 已经在真机上启动，UFS root 可读写，
launchd / SpringBoard / backboardd 都能起来，显示服务已经注册。但 iPad UI
还没有真正显示：backboardd 只提交过一次 `SwapEnd`，随后把显示关掉；GPU
renderer 还没有。触摸（Novatek NT36523）和 USB gadget 仍在 bring-up。

> **实验性 bring-up，不是可用的平板系统。** 很多功能缺失或只是 stub，
> Apple 的安全模型基本关闭。系统装在 `userdata` 分区，会清掉 Android 用户数据。

## 目前已经好的

| 部分 | 状态 |
| --- | --- |
| 启动 | 解锁的 stock ABL → `fastboot boot` 我们的 shim → XNU；不刷 boot 分区 |
| CPU | 1 核稳定；8 核能到 userspace，但偶发 loopattach 竞争/复位，默认 `cpus=1` |
| 存储 | 自己的 UFS host driver；iPadOS root 是 `userdata` 里的 HFS+，可读写 |
| iPadOS 用户态 | 真实 iPad7,3 16.6.1（20G81）root 和 shared cache；launchd 和系统 daemon 能跑 |
| SpringBoard | SpringBoard、backboardd、AKS 调用都能起来 |
| 显示服务 | `AppleCLCD`、`IOMobileFramebuffer`、`IOSurface` 已注册；1600×2560 面板已亮 |
| 按键 | PMIC PON / GPIO 读取正常，能产生按键事件 |
| AppleKeyStore | selector 正常，keybag 已补齐，AKS 有返回 |
| 日志 | `userdata` 末尾有 4 MiB 持久 ring，可从 TWRP 读取 |

## 还没好的 / 当前卡点

| 部分 | 状态 |
| --- | --- |
| iPad UI | backboardd 调用过一次 `SwapBegin`/`SwapEnd`，但紧接着 `RequestPowerChange(0)` 关显示，之后没有持续 swap |
| GPU | 没有 accelerator；目前只有 CPU framebuffer/合成思路 |
| 触摸 | Novatek NT36523 SPI；协议解析和主机测试已完成，内核 probe 还没接 |
| USB | USB0 GDSC/clock/DWC3/femto 可读；`nabuusb=1` 稳定，`nabuusb=2` 约 60–70 秒复位；主机还没看到 `1209:0001` |
| 8 核 | 能启动到 userspace，但偶发 loopattach 竞争/复位，尚未长稳 |
| 其他 | Wi-Fi、蓝牙、音频、相机、传感器、GPS、SEP/Face ID 都未实现 |

## 当前 bring-up 重点

**显示。** 下一步最便宜的是 `nabufb_force_on=1`：bring-up 阶段忽略
`RequestPowerChange(0)`，并打印前几次 `SwapEnd` 的 mask/layer/surface/rect。
如果出现 `NABU FB: swap`，说明 renderer 在工作，剩下的只是电源/格式/缩放。

**USB。** 还差 PHY。`nabuusb=1` 能保持控制器可读；`nabuusb=2` 会执行 femto
init + gadget 并复位。下一步是分步限制 femto 写，每一步后同步 flush 日志。

**触摸。** 正确协议是 NT36523 的 **65 字节 point-data**，不是 NT36532E 的
THP 矩阵。解析器和主机测试在 `tools/touch-nt36523/`；GENI SPI、时钟、GPIO、
SMMU/DMA 和 HID 映射还需要硬件验证。

## 启动流程

```text
ABL（解锁的 stock Xiaomi bootloader）
  -> fastboot boot shim-boot-nabu.img          （临时启动，不刷 boot）
     -> shim：自己的 MMU、读取 live FDT、从 ramdisk 加载 payload
        -> XNU Mach-O + 生成的 Apple Device Tree (ADT) + boot-args
           -> XNU -> UFS driver -> userdata 里的 HFS+ root
              -> dyld + shared cache -> launchd -> daemons
                 -> backboardd + SpringBoard
```

这台机器没有可用串口。内核 console 画在 ABL 留下的 framebuffer
（`0x9c000000`），同时写进 `userdata` 末尾的持久 log ring。

## 构建

XNU 的 patch 系列在外部 `darwin-xnu-build` 树里，用 macOS GitHub runner 构建：

```sh
gh workflow run xnu.yml -R YOU/ios-nabu
gh run download RUN_ID -R YOU/ios-nabu -n xnu-kernel -D xnu/out
```

在 Linux 上用 `apfs-fuse` 和 `hfsprogs` 构建 iPadOS root：

```sh
tools/mkiosroot.sh OS.dmg SystemOS.dmg AppOS.dmg ipados16-root.img 16G
sudo fsck.hfsplus -fy ipados16-root.img
sudo mount -t hfsplus -o loop,force ipados16-root.img /mnt/ipadroot
sudo python3 tools/nabu-root-tweaks.py /mnt/ipadroot
sudo umount /mnt/ipadroot
img2simg ipados16-root.img ipados16-root.simg
```

`nabu-root-tweaks.py` 会跳过 Setup、写入 activation 状态、移除 baseband
daemon、关闭会杀慢进程的 watchdog，并按 root 里实际的 `SystemVersion.plist`
生成 DataMigration/activation 数据。

打包并启动：

```sh
python3 tools/mkadt.py shim/out/nabu.adt --highmem
python3 tools/mkpayload.py xnu/out/kernel.development.nabu shim/out/nabu.adt shim/out/payload.bin \
    --cmdline 'debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam -noprogress rd=ufsroot0 ufsrw=1 rootrw=1 launchd_unsecure_cache=1 nabuusb=1 ufslogsecs=1 nabufb_force_on=1 nabufb_noconsole=1 wdt=-1 wdt_test=no_user_checkin ignition_level=1'
shim/build.sh ../shim/out/payload.bin
fastboot -S 512M flash userdata ipados16-root.simg   # 会清空数据
fastboot boot shim/out/shim-boot.img
```

读内核日志：

```sh
fastboot boot recovery/recovery.img        # TWRP，临时
tools/nabu-log-pull.sh logs/boot.txt
tools/nabu-log.py logs/boot.txt | less
```

## 启动参数

| Boot-arg | 作用 |
| --- | --- |
| `rd=ufsroot0 ufsrw=1 rootrw=1` | Root 在 `userdata`，可读写 |
| `cpus=1` | 安全默认值；8 核仍在验证 |
| `nabuusb=1` | 只读 USB 状态，不启动 femto/gadget |
| `nabuusb=2` | 执行 femto init + gadget（目前会复位，仅 bring-up） |
| `nabufb_force_on=1` | 忽略 `RequestPowerChange(0)` |
| `nabufb_noconsole=1` | 不把 kernel console 画到 GUI 上 |
| `ufslogsecs=1` | 每秒刷一次持久 ring |
| `wdt=-1 wdt_test=no_user_checkin` | 关闭 Apple watchdog |

## 硬件

- SoC：SM8150-AC / Snapdragon 860，6 GB RAM，UFS 3.1。
- 面板：1600×2560；ABL framebuffer 在 `0x9c000000`。
- 触摸：Novatek NT36523，SPI；IRQ GPIO39，reset GPIO12。
- USB：DWC3 `0x0a600000`，QSCRATCH `0x0a6f8800`，femto PHY `0x088e2000`。
- PMIC：PM8150 / PM8150B / PM8150L。
- userdata：`/dev/block/sda31`，first LBA `0x288040`，last LBA `0x769e7fa`，
  4 KiB block；ring 从 `0x769e3fa` 开始。

## 仓库结构

| Path | 内容 |
| --- | --- |
| `shim/` | Bare-metal boot shim（C + asm），打包成 Android boot image |
| `tools/mkadt.py` | 生成 Apple Device Tree |
| `tools/mkpayload.py` | 打包 kernel + ADT + boot-args |
| `tools/mkiosroot.sh` | 从 IPSW 构建 HFS+ iPadOS root |
| `tools/nabu-root-tweaks.py` | Root bring-up 修改 |
| `tools/nabu-log-pull.sh`, `nabu-log.py` | 从 recovery 读/解析持久 log |
| `tools/nabu-crash.py` | Crash/panic 符号化 |
| `tools/touch-nt36523/` | NT36523 协议解析 + 主机测试 |
| `tools/qemu-nabu.py` | QEMU 启动测试 |
| `NOTES.md` | 详细工程记录：每次 boot、crash、决策和硬件事实 |

不在 git 里：分区备份、live device tree、recovery、Apple 固件和 root 镜像、
下载/生成的 build tree。它们包含设备序列号/校准数据，或 Apple 版权文件。

## Credits

Built on Apple's open-source releases (xnu, IOHIDFamily, libpthread, hfs),
[BearSSL](https://bearssl.org),
[blacktop/darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build),
[ipsw](https://github.com/blacktop/ipsw), Qualcomm/Xiaomi 公开的 Linux kernel
源码、TWRP，以及 ios-sweet / UniXNU / ios-nabu 的 bring-up 工作。
iOS/iPadOS 本身属于 Apple，不在此分发。
