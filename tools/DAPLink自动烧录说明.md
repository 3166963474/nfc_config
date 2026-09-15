# DAPLink 自动烧录工具

该工具检测 DAPLink/CMSIS-DAP 探针及其SWD目标板，通过 pyOCD 对 STM32F103CB 执行擦除、写入、逐字节回读校验和复位。DAPLink可以始终插在电脑USB上：烧完一块板，拔下SWD接头并插到下一块板后，会自动再次烧录。

烧录成功时发出两声由低到高的短提示音；烧录或校验失败时发出三声低音。可用 `--no-sound` 关闭声音。

## 安装

在 `tools` 目录已有的虚拟环境中执行：

```powershell
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
```

检查 DAPLink：

```powershell
python -m pyocd list
```

## 使用

单次模式：

```powershell
python daplink_auto_flash.py once "..\MDK-ARM\BUS_SEAT_V0_0_3\BUS_SEAT_V0_0_3.hex"
```

连续生产模式（DAPLink不用拔USB，只更换目标板）：

```powershell
python daplink_auto_flash.py continuous "..\MDK-ARM\BUS_SEAT_V0_0_3\BUS_SEAT_V0_0_3.hex"
```

连续模式默认立即烧录启动时已经连接的第一块目标板。单次模式为防止误烧，默认忽略已经连接的目标板；单次模式需要立即烧录当前板时增加：

```powershell
--include-existing
```

生产环境有多个探针时，可锁定一个 UID：

```powershell
python daplink_auto_flash.py continuous firmware.hex --uid "探针UID"
```

默认目标名为 `stm32f103rc`，SWD 时钟为 1 MHz，擦除策略为 `auto`。pyOCD 的内置目标没有单独列出 STM32F103CB；RC 与 CB 使用相同的 STM32F1 Flash 算法和 2KB 页结构，本工程固件位于 CB 的 128KB Flash 范围内，因此使用内置 RC 目标。问题板可降低SWD频率并整片擦除：

```powershell
python daplink_auto_flash.py once firmware.hex --frequency 100000 --erase chip --include-existing
```

`.bin` 默认写入 `0x08000000`，可用 `--address` 修改。`--dry-run` 只显示命令；完整选项运行：

```powershell
python daplink_auto_flash.py --help
```

## 接线

```text
DAPLink SWDIO -> STM32 PA13
DAPLink SWCLK -> STM32 PA14
DAPLink GND   -> 板卡 GND
DAPLink VTref -> 板卡 3.3V
DAPLink NRST  -> STM32 NRST（推荐）
```

DAPLink 的 VTref 通常是目标电压检测输入，不能默认当作板卡电源使用。目标板应单独可靠供电。

连续模式在每次烧录后等待目标板连续三次无法通过SWD访问，才确认该板已经移除并重新布防。这可以避免瞬时SWD错误导致同一块板重复烧录。
