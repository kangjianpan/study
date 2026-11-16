# 童骑卫 Gateway —— ESP32-C3 蓝牙 + 云端网关

方案的 §3 架构图里，车与外界之间那一层。对应文档章节：§6.1（UART 帧）、
§6.2（遥测）、§6.3（BLE GATT）、§6.6（云端）。

> **状态：骨架，分两半看。**
>
> - `frame.cpp` / `telemetry.cpp` / `stm32_link.cpp`：**已在主机上编译并跑过回归测试**，
>   生成的帧与 STM32 README 里的现成测试帧逐字节一致（见文末第 11 节）。
> - BLE / MQTT / Arduino 主程序：**尚未编译**，本机没有 Arduino / PlatformIO 工具链。
>   第一次 `pio run` 大概率有几处 API 名称要按实际安装的库版本微调——见第 9 节。

---

## 1. 它做什么

```
STM32 ──UART1(AA55 帧)──► ESP32-C3 ──BLE Notify──► 小程序（10 Hz，能控制）
                                  └──MQTT──────► 阿里云 IoT（约 1 Hz，只读）
```

**一句话原则：云端只读，STM32 固件零改动。**

ESP32 收到云端的下行消息不会有任何反应——代码里根本没有订阅属性设置的 Topic。
这不是没写完，是故意不写（方案 §6.6.5）。

## 2. 编译

### PlatformIO（推荐）

```bash
cd 童骑卫-Gateway
pio run                  # 编译
pio run -t upload        # 烧录
pio device monitor       # 看日志，115200
```

依赖三个库，`platformio.ini` 里已经写好，首次 `pio run` 会自动下载：

| 库 | 版本 | 用途 |
|---|---|---|
| `h2zero/NimBLE-Arduino` | ^2.1 | BLE（比 Bluedroid 省 RAM） |
| `knolleary/PubSubClient` | ^2.8 | MQTT |
| `bblanchon/ArduinoJson` | ^7.0 | Alink JSON 拼装 |

### Arduino IDE

装好 `esp32` 板级包后，把 `src/*.cpp` 和 `include/*.h` 全部放进一个 sketch 文件夹
（IDE 不支持 include 目录），库用库管理器装上面三个。
**必须**在 Tools → USB CDC On Boot 选 **Enabled**，否则 UART0 会被调试口占掉。

## 3. 接线

| 信号 | ESP32-C3 | 对端 | 说明 |
|---|---|---|---|
| 车链路 TX | GPIO5 | STM32 **PB11**（USART3_RX） | 交叉接 |
| 车链路 RX | GPIO4 | STM32 **PB10**（USART3_TX） | 交叉接 |
| 4G TX | GPIO21 | 模组 RX（UART0） | 交叉接 |
| 4G RX | GPIO20 | 模组 TX（UART0） | 交叉接 |
| GND | GND | 两块板 + 模组 | **必须共地** |
| 供电 | 5V | 独立 DC-DC | 见方案 §4.1：模组峰值电流不能和主控共 LDO |

> ESP32-C3 只有 UART0 / UART1 两路控制器，接了车链路和 4G 之后就没有调试串口了。
> 调试日志走 USB Serial/JTAG（板子上那个 USB 口），不要接 UART0 的 TX/RX 去看日志。

## 4. 三个任务，优先级就是设计

| 任务 | 优先级 | 周期 | 职责 | 卡住了会怎样 |
|---|---|---|---|---|
| `stm32Task` | **10** | 1 ms 轮询 | 读 UART1、解析帧、**1 s 心跳**、发下行 | 车降速到 6 km/h |
| `bleTask` | 6 | 100 ms | 推 14 字节遥测给小程序 | 小程序看不到，车不受影响 |
| `netTask` | 2 | 50 ms | 云端上报、4G 重连 | 云上看不到，**车不受影响** |

**为什么要拆三个任务而不是一个大 loop**：`PubSubClient::publish()` 是阻塞的，
TLS 握手一次可能几百毫秒。如果读串口和上传在同一个循环里，云一卡，
STM32 的 1 s 心跳就断，3 s 后车自己降速——这正是方案 §3.2 那条红线要防的事。
拆成任务后，优先级 10 的 `stm32Task` 永远能抢占优先级 2 的 `netTask`。

> **一个已知的、设计内的退化**：ESP32 自己重启时，`setup()` 里等 WiFi 最多 15 s，
> 这段时间没有心跳，STM32 会判链路超时、把限速压到 6 km/h。
> 这是安全侧的预期行为，**不是 bug**——网关挂了，车应该保守，而不是继续按原来的限速跑。
> 现场如果想避免，把 `CFG_WIFI_TIMEOUT_MS` 调小，或者让 `Cloud_Begin()` 在任务里跑。

## 5. 帧与命令

ESP32 → STM32 的方向用同一套 `AA55` 帧（`frame.cpp`，与 STM32 的 `protocol.c` 逐字节一致）。

**小程序写 FFF2 时不需要自己拼帧、也不需要算 CRC**，只写：

```
[CMD] [LEN] [PAYLOAD...]
```

ESP32 收到后补帧头、加自己的 SEQ、算 CRC8 再转发。这样小程序侧代码极简，
帧格式也只在一处维护。

**命令白名单**（`ble_link.cpp` 的 `cmd_allowed()`）：

| CMD | 允许 | 说明 |
|---|---|---|
| `0x20` 家长限速 | ✅ | |
| `0x21` 远程急停请求 | ✅ | |
| `0x22` 解除停车 | ✅ | |
| `0x23` 场地限速 | ✅ | |
| `0x24` 孩子下调 | ✅ | |
| `0x25` 强制油门 | ❌ | 只有台架能用 |
| `0x26` 强制车速 | ❌ | 同上 |
| `0x27` 模拟穿戴失效 | ❌ | 同上 |
| `0x40` 故障注入 | ❌ | 同上 |

后四个能伪造车辆自身状态，**任何拿着手机的儿童都不该能发**。ESP32 侧直接拒收，
STM32 侧还有一层（调试固件才响应），两层都留着。

## 6. 已实现 / 未实现

| 功能 | 状态 | 位置 |
|---|---|---|
| AA55 帧解析 + CRC8 + 4 帧队列 | ✅ | `stm32_link.cpp` / `frame.cpp` |
| 14 字节遥测解码 | ✅ | `telemetry.cpp` |
| 1 s 心跳下行（防 STM32 链路超时） | ✅ | `main.cpp` |
| BLE GATT（FFF0/FFF1/FFF2/FFF3）+ 命令白名单 | ✅ | `ble_link.cpp` |
| 10 Hz 遥测 Notify | ✅ | `ble_link.cpp` |
| 阿里云一机一密 + MQTT over TLS | ✅ | `uplink_wifi.cpp` |
| Alink JSON + 按 §6.6.3 降采样 | ✅ | `cloud.cpp` |
| WiFi 自动重连 + 指数退避 | ✅ | `uplink_wifi.cpp` |
| 4G 模组 AT 握手（AT/CPIN/CSQ） | 🟡 | `uplink_at4g.cpp`，**MQTT 部分没写** |
| 4G 走 MQTT 上报 | ⬜ | 需按买到的模组手册填 AT 序列 |
| TLS 证书校验 | 🟡 | 现在 `CFG_ALIYUN_INSECURE=1`，上线前必须关 |

> 4G 那部分**故意只留骨架**。Air780E / EC200S / EC800 的 MQTT AT 命令各不相同，
> 编一套看起来对、实际没跑过的序列，比留个明显的洞更糟。

## 7. 阿里云控制台要建什么

1. 创建产品，节点类型选**直连设备**，数据格式选 **Alink JSON**。
2. 在"功能定义"里按 §6.6.3 建 8 个属性，标识符必须一模一样：

| 标识符 | 数据类型 | 单位 | 读写 |
|---|---|---|---|
| `speed` | float | km/h | 只读 |
| `limit` | int32 | km/h | 只读 |
| `state` | int32 | — | 只读 |
| `faults` | int32 | — | 只读 |
| `battery` | float | V | 只读 |
| `wear` | int32 | — | 只读 |
| `trip_m` | int32 | m | 只读 |
| `rssi` | int32 | dBm | 只读 |

3. 添加设备，拿到三元组，填进 `include/config.h`：

```c
#define CFG_ALIYUN_PRODUCT_KEY  "..."
#define CFG_ALIYUN_DEVICE_NAME  "GuardKart-01"
#define CFG_ALIYUN_DEVICE_SEC   "..."
```

4. 用控制台的"**MQTT 连接参数生成**"工具核对一次 clientId / username / password。
   `uplink_wifi.cpp` 顶部的注释里写了签名算法，两边对上再查代码。

Topic 不用手动创建，物模型上报的 Topic 是平台固定的：
`/sys/{productKey}/{deviceName}/thing/event/property/post`

## 8. 上线前 checklist

- [ ] `CFG_ALIYUN_INSECURE` 改成 **0**，并在 `uplink_wifi.cpp` 里装根证书
- [ ] 加 NTP 校时，替换 `CFG_BUILD_EPOCH`
- [ ] 按模组手册补完 `uplink_at4g.cpp` 的 AT 序列，并实测断网重连
- [ ] 复核命令白名单，确认 `0x25/0x26/0x27/0x40` 仍然拒收
- [ ] 小程序正式版：域名白名单 + ICP 备案（演示用体验版可跳过）
- [ ] 跑一遍方案 §13.1 的四条"4G 隔离性验证"，特别是"云端卡顿不阻塞串口"

## 9. 踩过的坑

1. **C3 只有 UART0 / UART1**。两路都被占，调试只能走 USB Serial/JTAG，
   并且必须开 `ARDUINO_USB_CDC_ON_BOOT=1`（`config.h` 里有 `#error` 挡着，忘开会直接编译失败并提示原因）。
2. **NimBLE-Arduino 1.x 与 2.x 的回调签名不同**。本工程按 2.x 写
   （`onConnect(NimBLEServer*)`、`onWrite(NimBLECharacteristic*)`）。
   如果你的库管理器装成了 1.x，`ble_link.cpp` 会报重写签名不匹配——升到 2.x，别改代码。
3. **`PubSubClient` 默认缓冲区只有 256 B**，装不下带中文属性的 JSON。
   `Cloud_Begin()` 里已经 `setBufferSize(CFG_CLOUD_PAYLOAD_MAX + 64)`，改大 JSON 时记得同步改。
4. **心跳 1 s 不能省**。ESP32 只要 3 s 不发帧，STM32 就判链路超时、把限速压到 6 km/h。
   这个心跳和 BLE 连没连上无关，永远在发。
5. **`millis()` 从 0 开始**。阿里云签名里的 timestamp 用"编译时刻 + 运行时间"糊过去了，
   能让握手成功，但不是长期方案，§8 里列了要换成 NTP。
6. **微信开发者工具模拟器不支持蓝牙**，BLE 必须真机预览。

## 10. 下一步

| 谁 | 做什么 |
|---|---|
| B | 补 `uplink_at4g.cpp` 的 AT 序列（拿模组手册对照） |
| C | 小程序四页面 + FFF2 写入格式对接；远程只读页 |
| C | 用控制台生成器核对我这里的签名实现 |
| A | 4G 模组独立供电那一路 DC-DC |

---

## 11. 主机侧回归测试（不需要开发板）

`frame.cpp` / `telemetry.cpp` / `stm32_link.cpp` 可以在电脑上直接编译运行，
因为它们只依赖 UART 和 `millis()`。`tools/host_test/Arduino.h` 是个极简替身。

```bash
g++ -std=gnu++11 -DARDUINO_USB_CDC_ON_BOOT=1 \
    -Itools/host_test -Iinclude \
    src/frame.cpp src/telemetry.cpp src/stm32_link.cpp \
    tools/host_test/test_link.cpp -o /tmp/gk_test && /tmp/gk_test
```

当前结果：

```
parse one heartbeat                        ok
two frames in one poll, in order           ok
bad CRC dropped                            ok
resync after garbage                       ok
LEN>32 resyncs                             ok
14-byte telemetry payload intact           ok
send builds AA55 + crc                     ok

rx=6 crc=1
all good
```

**这些用例的价值在于跨端对齐**：`test_link.cpp` 里期望的哈希字节
`AA 55 01 30 00 92` 是抄 STM32 README 4.1 节的现成测试帧。
两边帧格式一旦被谁改了，这个测试立刻红，不用等上车才发现。

**改了 `protocol.c` 或 `frame.cpp` 之后必须重跑。** 建议写进每周联调检查单。
