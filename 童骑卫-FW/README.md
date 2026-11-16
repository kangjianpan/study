# 童骑卫 GuardKart —— 飞控骨架

STM32F103C8T6 + FreeRTOS，7 个任务的安全架构骨架。方案全文见
`../童骑卫-方案设计.md`。

## 1. 打开与编译

| 项 | 值 |
|---|---|
| 工程 | `MDK-ARM/GuardKart.uvprojx` |
| 器件 | STM32F103C8 |
| 库 | HAL + FreeRTOS V202212.01（heap_4） |
| 编译器 | ARMCC V5 |

命令行编译：

```bash
cd MDK-ARM
"D:/Keil5/UV4/UV4.exe" -b GuardKart.uvprojx -j0 -o build.log
```

当前结果（2026-10-17）：**0 error / 0 warning**，
`Code=24240  RO=484  RW=220  ZI=13404`（RAM 13.6 KB / 20 KB，其中 10 KB 是
FreeRTOS 堆）。

## 2. 上电预期输出

USART1（PA9/PA10）@115200 8N1：

```
============================================
 GuardKart 0.1.0
 RESET: power-on / reset pin
 INTERLOCK: bypassed (bring-up mode)
 limit: venue=12 parent=10 child=12 -> 10 km/h
============================================
sensor: imu=0 tof=0 nfc=0
 heap free after tasks: xxxx bytes
 starting scheduler
[200] STATE SELF_TEST -> STANDBY
[210] STATE STANDBY -> READY
[1000] st=READY v=0.00 lim=10 run=1 f=0x000 seat=0 belt=0 helm=0 heap=xxxx
```

- `RESET: watchdog - supervisor intervened` 出现在故障注入演示之后，是**预期**结果。
- `sensor: imu=0 tof=0 nfc=0` 表示三个驱动还是桩函数，属正常。
- 人机灯 PC13（板载 LED1）：允许行驶 1 Hz，被拦 5 Hz，正在减速 16 Hz。
- PB13 每 10 ms 翻转一次（50 Hz 方波），示波器可直接量 SafetyTask 的抖动。
- 状态机（方案 7.5）共六态：`SELF_TEST → STANDBY → READY → RUN`，
  以及旁路的 `DECEL`、`FAULT_LOCK`。只有 `READY/RUN/DECEL` 三态给电调使能。

## 3. 关键文件

| 文件 | 作用 |
|---|---|
| `Core/Inc/app_config.h` | **所有**阈值、周期、优先级、引脚参数 |
| `Core/Src/app_safety.c` | 故障分级 + 状态机 + 电调使能 + 喂狗，**唯一**能拉低 PB12 的地方 |
| `Core/Src/app_state.c` | 六个状态的定义、名字、使能映射 |
| `Core/Src/app_control.c` | 油门 → 软限速 → TTC → 斜率限制 → 电调 |
| `Core/Src/app_sensor.c` | 采样，每 10 ms 覆盖写一次 `g_q_sensor` |
| `Core/Src/app_comm.c` | USART1/USART3 双解析器，命令分发 |
| `Core/Src/app_telemetry.c` | 14 字节遥测 + 1 Hz 心跳（原 app_cloud.c） |
| `Core/Src/bsp.c` | GPIO / USART3 / ADC1 / IWDG（寄存器实现，本库没有 hal_iwdg.c） |
| `Core/Src/drv_*.c` | 执行器已实现（电调 PWM），三个传感器是桩 |

## 4. 串口帧格式

```
AA 55 | SEQ | CMD | LEN | PAYLOAD... | CRC8
```

CRC8 多项式 `0x07`，初值 `0x00`，**只覆盖 SEQ..PAYLOAD**，不含帧头。
建帧函数在 `protocol.c`，上位机可照抄。

### 4.1 现成测试帧（USART1 或 USART3 都能收）

| 用途 | CMD | 帧（十六进制） |
|---|---|---|
| 心跳 / 保持链路 | 0x30 | `AA 55 01 30 00 92` |
| 家长限速 8 km/h | 0x20 | `AA 55 01 20 01 08 78` |
| 场地限速 12 km/h | 0x23 | `AA 55 01 23 01 0C D9` |
| 孩子下调到 5 km/h | 0x24 | `AA 55 01 24 01 05 F0` |
| 远程停车 | 0x21 | `AA 55 01 21 00 D0` |
| 远程恢复 | 0x22 | `AA 55 01 22 00 EF` |
| 调试油门 500‰ | 0x25 | `AA 55 01 25 02 F4 01 73` |
| 调试车速 8.00 km/h | 0x26 | `AA 55 01 26 02 20 03 A9` |
| 故障注入（复位演示） | 0x40 | `AA 55 01 40 00 30` |
| 模拟穿戴失效（进 DECEL） | 0x27 | `AA 55 01 27 01 01 51` |
| 清除模拟失效 | 0x27 | `AA 55 01 27 01 00 56` |

**SEQ 规则**（`app_comm.c` 的 `seq_accept()`，别踩坑）：

- 同一条链路上，**1 s 内 SEQ 相同的第二条帧会被当作重传丢掉**。所以演示时
  每帧间隔 ≥ 1 s，或者手动把 SEQ 递增。
- 链路静默满 3 s 后再来的帧**无条件接受**，SEQ 从几开始都行——这是给手机 /
  ESP32 重启准备的会话重置。
- 不做"SEQ 必须更大"的判断（8 位会回绕），所以上电后第一帧用 `01` 没问题。

## 5. 四个可现场演示的效果

**① 远程停车**
发 `CMD_STOP` → `g_drive_allowed` 变 0、PB12 拉低、PA8 回到 1500 µs 中位，
日志出现 `FAULT ON 0x100`。发 `CMD_RESUME` 恢复。

**② 链路超时降级**
发任意帧后停止发送，3 s 后日志出现 `FAULT ON 0x010`，限速自动压到 6 km/h，
但**不切断动力**——孩子不会被扔在场地中间。

**③ 看门狗复位（核心）**
发 `CMD_FAULT_INJECT` → SafetyTask 进入死循环（中断仍开着，其它任务全部饿死）
→ 200 ms 后 IWDG 复位 → 重新上电打印
`RESET: watchdog - supervisor intervened`。
这条证明的是：**主控任何一种卡死，都会退回到"电机断电"**。

**④ 减速停机 DECEL（软故障的核心）**

按顺序发（每帧间隔 1 s 以上，避开 `DUP_WINDOW_MS`）：

| 步 | 帧 | 预期 |
|---|---|---|
| 1 | `AA 55 01 25 02 F4 01 73` | `STATE READY -> RUN`，run=1 |
| 2 | `AA 55 01 27 01 01 51` | `FAULT ON 0x008`、`STATE RUN -> DECEL`，油门 500‰→0 按 60‰/5 ms 斜坡收回，约 85 ms |
| 3 | `AA 55 01 27 01 00 56` | 若在 3 s 内发：`STATE DECEL -> READY`，条件解除；若故意拖过 3 s：`STATE DECEL -> FAULT_LOCK` 并**锁存**，之后 `0x27 00` 也解不开，只能复位 |

关键点在第 2 步：`run` 在整个减速过程中**保持 1**，PB12 不拉低、PA8 是缓慢回中位而不是
直接跳 1500 µs —— 高速时直接断动力等于把孩子甩出去，所以先收油门再判故障。
第 3 步拖过 3 s 会锁死，是因为"说了失效却一直不恢复"本身就该当硬故障处理。

> 演示 ② 和 ③ 之前，先把 `APP_BRINGUP_MODE` 保持 1，否则没接传感器时
> 穿戴互锁会一直把车速锁死。

## 6. 待办（按成员分工）

| 谁 | 任务 | 落点 |
|---|---|---|
| B | ICM-42688 / SPI1，WHO_AM_I 应为 0x47 | `drv_imu.c` |
| B | 2× VL53L1X / I2C1，XSHUT 分时改址 0x29→0x2A | `drv_tof.c` |
| B | MFRC522 / SPI1 片选 PA2，头盔 UID 白名单 | `drv_nfc.c` |
| B | 轮速霍尔 TIM3_CH1 捕获 + 周期估计，替换 `g_dbg_speed_x100` | `app_sensor.c` |
| C | ESP32 侧 BLE GATT + 小程序 | 见方案 6.3 / 6.4 |
| C | 指令去重窗口 1 s（`DUP_WINDOW_MS`）联调：BLE 重传与手发同帧都要测 | `app_comm.c` |
| C | 遥测里的圈速与里程（现在还是 0） | `app_telemetry.c` |
| A | 急停硬件回路、PB12 外部下拉、接触器 | 原理图 |

## 7. 踩过的坑（别重复踩）

1. **USART2 在这块板子上会满屏 0x00**，ESP32 链路必须走 USART3。
2. `Core/Src/usart.c` 里的 `HAL_UART_MspInit` 只处理 USART1，
   所以 USART3 的引脚复用写在 `bsp.c`，不要指望 CubeMX 的回调。
3. 这套 HAL 副本**没有 `stm32f1xx_hal_iwdg.c`**，
   `HAL_IWDG_Init()` 会链接失败，故 `bsp.c` 直接写寄存器。
4. `configCHECK_FOR_STACK_OVERFLOW=2` 已打开，钩子在 `app_main.c` 末尾，
   改栈大小之后务必再跑一次。
5. 新加的 `.c` 必须同时在 `GuardKart.uvprojx` 里登记，Keil 不会自动扫描目录。
