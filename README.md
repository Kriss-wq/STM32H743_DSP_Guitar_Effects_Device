# STM32H743 DSP 吉他效果器

基于 STM32H743 的实时吉他效果器。外置 ADC 经 I2S 采入，在 MCU 上按 8 槽效果链处理，再经 I2S 送到 DAC。屏幕用 LVGL 选效果、调参数。

采样率 **48 kHz**，I2S 数据格式 **24 bit 右对齐**，效果内部是单声道 `float`（约 `[-1, 1]`）。

## 硬件

| 部分 | 用法 |
| --- | --- |
| MCU | STM32H743，硬浮点 `fpv5-d16` |
| ADC | I2S3 Master RX，DMA |
| DAC | I2S2 Master TX，DMA |
| 屏 | LTDC，800×480，RGB565，显存在 SDRAM `0xC0000000` |
| 刷屏 | DMA2D 把 LVGL 绘制缓冲拷到显存 |
| 触摸 | GT911，软件 I2C |
| RTOS | FreeRTOS |

I2S2 与 I2S3 目前都是主机，时钟不共用，长时间运行可能有相位漂移。

## 音频路径

```
I2S3 RX DMA
    → DatacollecTask（高优先级）
    → 24bit 符号扩展，归一化到 float
    → effect_process()
    → 限幅到 ±1，写回 24bit
    → I2S2 TX DMA
```

`Audio_Buffer_Size` 为 64 个立体声 `int32`。半缓冲是 16 个单声道帧，约 0.33 ms。RX 完成中断里先失效 D-Cache，再通知音频任务；写出 TX 半区后做 D-Cache Clean。

左右声道目前都写成同一路单声道结果。

## 任务

| 任务 | 优先级 | 做什么 |
| --- | --- | --- |
| `DatacollecTask` | High | 等 I2S RX 通知，跑效果链，填 TX |
| `DisplayTask` | Low | GT911 扫描、`lv_task_handler()`、每秒刷新 CPU 占用 |

点屏会触发大面积刷屏（DMA2D + LTDC 读 SDRAM）和软件 I2C，和 I2S DMA 抢总线。半缓冲很短时，听感上就是卡顿。

## 效果链

链长 8。每个槽是一份固定的 `effect_t`。没 `Init`、`enable == 0` 或 `Process == NULL` 的槽会跳过。各级之间用两块 scratch 乒乓，不原地覆盖输入。

在 `Effect/Src/effect.c` 的 `effect_init()` 里登记可选效果：

| 序号 | 名字 | 作用 |
| --- | --- | --- |
| 0 | Test | 软削波示例（Gain / Tone / Level） |
| 1 | Vintage30 | Celestion V30 风格 FIR 箱体 IR |
| 2 | TS808 | Tube Screamer 削波 + Tone（见下方许可） |
| 3 | NAMTest | 嵌入的 `.namb` Neural Amp Modeler 模型 |
| 4 | Delay | 采样环形延时，最长约 500 ms |
| 5 | Volume | 音量槽。`Process` 目前是空的，选中后不改信号 |

界面操作：

- 短按槽位：进入效果列表，选中后拷贝到该槽并 `Init`，默认开启
- 长按槽位：三个旋钮调 `param[0..2]`，旋钮变化会调 `Setup`
- 参数页开关：翻转该槽的 `enable`

## 加一个效果器

1. 在 `Effect/Inc`、`Effect/Src` 里实现 `Init` / `Setup` / `Process`，并提供 `Get_Xxx_t()`。
2. 在 `effect_init()` 里 `All_Effect[n] = Get_Xxx_t();`。
3. `name` 不超过 15 个字符，三个参数名各不超过 15 个字符。`param` 是 `0..100` 的 `uint8_t`，不要直接拿它当线性增益（`50` 就是放大 50 倍，后面硬限幅会改变音色）。

`Process` 的 `size` 是本块帧数，当前为 16，上限 `EFFECT_MAX_BLOCK`（64）。

## 目录

```
Core/           CubeMX：时钟、I2S、LTDC、DMA、FreeRTOS 入口
Effect/         效果链和各个效果器
lvgl_main/      SquareLine 生成的 UI（Screen1 槽位 / Screen2 列表 / Screen3 参数）
lvgl/           LVGL 8.3
BSP/            GT911 触摸
Drivers/        HAL、CMSIS-DSP
```

## 编译

CMake 工程，工具链按 STM32Cube 的 `cmake/stm32cubemx`。C11，效果库里的 NAM 路径使用 C++17，并打开硬件浮点和 `-Ofast`。CMSIS-DSP 静态库链的是 `libarm_cortexM7lfdp_math.a`。

## 已知问题

- I2S2 / I2S3 双主机，没有共用位时钟。
- `I2S_RX_State` 只有一个标志。半完成和全完成通知挤在一起时，后写的会覆盖前一次，可能丢一块。
- 点屏、拖旋钮、换页时，刷屏和软件 I2C 可能让音频欠载。
- Volume 的 `Process` 还没写增益。
- 早期 USB Audio 从机通路还在工程里，当前播放不走那条路。

## 第三方与许可

- **TS808** 移植自 [TS-808-Ultra](https://github.com/JamesStubbsEng/TS-808-Ultra)，**GPL-3.0**。整机若分发二进制，需要按 GPL-3.0 提供对应源码，或换成别的过载实现。
- Vintage30 使用 Celestion V30 相关 IR 数据，分发前要自行确认采样/IR 的版权。
- HAL、CMSIS、FreeRTOS、LVGL、USB Device Library 各自遵循仓库里对应的许可文件。
