# 童骑卫固件 v0.3.0-hw-v2

STM32F103C8 + FreeRTOS，适配 v2 底板。默认 `APP_DRIVE_COMMISSIONED=0`，继电器使能保持关闭。**驱动代码已实现不等于实物已验证；未购买、未标定的器件不会被假数据替代。**

已接入：ICM-42688-P SPI、双 VL53L1X 的 XSHUT 改址与异步测距、MFRC522 的 ISO14443-A 完整 4/7/10 字节 UID、防冲突 BCC / SAK CRC 检查和白名单、PB4/PB5 双路轮速中断、PA15 有源蜂鸣器。

PC13 只作 ARM 锁存器回读，不能再当板载 LED 输出。`CORE_3V3` 控制 Q4/Q5 自动切断传感器电源，不需要 GPIO。ARM 回读不代表继电器主触点已闭合。

## 编译

打开 `MDK-ARM/GuardKart.uvprojx`，使用 Keil ARMCC 5。命令行示例：

```powershell
Start-Process 'D:/Keil5/UV4/UV4.exe' -ArgumentList '-b','GuardKart.uvprojx','-j0','-o','build_v3.log' -WorkingDirectory './MDK-ARM' -WindowStyle Hidden -Wait
```

2026-11-16 本机编译：0 错误、0 警告；Code=32440、RO=700、RW=272、ZI=13576 字节。约 33.1 KB Flash、13.5 KB 静态 RAM，目标为 64 KB Flash / 20 KB RAM。任务堆栈余量还须实物运行测量。

新增 HAL SPI/I2C 源文件与原有库版本一致（ST HAL v1.1.9），已加入 Keil 工程；VL53L1X 配置来源与许可见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 主机回归

安装 Python 3 和 GCC 后，从项目根目录运行：

```powershell
python 童骑卫-FW/tests/run_tests.py
node 童骑卫-App/tools/test_node.js
```

前者编译真实驱动、ARM 决策和完整 SafetyTask，以模拟总线和任务时钟测试初始化、单位换算、错误 ID、读取失败、数据过期、完整 UID、防冲突校验、授权、轮速抖动、反馈丢失、使能顺序和配置门禁。模拟值仅在 `tests` 中存在，正式固件不会启用它们。

测试不能代替示波器、中断时序、光学测距、继电器和电调实测。BLE、MQTT 及 ESP32 完整固件不属于本次已完成的板级验证。

## 上电与提示

串口 USART1 PA9/PA10，115200 8N1，输出传感器初始化结果、首次读到的完整 UID、状态与故障字。默认配置下出现 `FAULT_LOCK` 和 `FAULT_COMMISSION` 是预期行为；轮径未填写也会报告 `FAULT_SENSOR`。

仅在联调验收完成、配置开放后：

1. 检查通过且踏板松开，进入 READY，MOTOR_EN 拉高，**短鸣一声**提示可以按 ARM。
2. 按 ARM 后 **短鸣两声**，保持 1500 μs 中立，等待 `ESC_NEUTRAL_DWELL_MS`。
3. 等待结束后必须检测到踏板松开，**长鸣一声**，下一次踩踏才可进入 RUN。
4. DECEL 周期短鸣；FAULT_LOCK 每 5 秒三短鸣。蜂鸣提示不是安全执行回路，HMI 卡住不能阻止硬件急停。

当前 3000 ms 中立等待仅为待验证起点。电调购买后必须实测“供电稳定后需要的中立时间”，修改参数并签署台架记录。按 ARM 太早需要松开再按；故障解除不会自动产生新的 ARM 按钮沿。

详细采购、配置和必须测量的限制见 [联调与验收.md](联调与验收.md)。
