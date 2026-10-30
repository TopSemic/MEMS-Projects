# STM32L431 + ADXL355 + ML307R —— 4G 倾角监测（OneNET 新版物模型）

ADXL355 测横滚/俯仰角，任一方向相对"上次上报时的基准"变化超过 **5°** 时，由 ML307R Cat.1 走 4G 用 MQTT 3.1.1 上报到中国移动 OneNET 新版物模型。整条链路已真机跑通：`CONNACK rc=0` → `SUBACK` → 平台回 `{"id":"1","code":200,"msg":"success"}`。

```
ADXL355 --SPI3--> STM32L431RCT6 --USART3(PC4=TX/PC5=RX,115200)--> ML307R --4G--> OneNET
                     |                                                        183.230.40.96:1883
                     +--USART2(PA2/PA3,115200)--> 调试串口 printf
```

## 为什么是"自己拼 MQTT 报文"

ML307R 用的是 **AT 固件**，它自带的 `AT+MQTT*` 指令集在发送含 `%XX` 转义的 OneNET 长期 token 时状态机会永久停在"正在重连"，而同一个字节流在 PC 上直连是 `rc=0`；OneNET 的 token 又必须**原样保留** `%2F`、`%3D%3D`（去掉就拒）。所以本项目放弃 `AT+MQTT*`，改为在 `AT+MIP*` 提供的**裸 TCP** 上实现一个最小 MQTT 3.1.1 客户端：

- 只用到实际需要的四类报文：`CONNECT` / `SUBSCRIBE` / `PUBLISH`(QoS0) / `PINGREQ`，以及对应的 `CONNACK` / `SUBACK` / `PUBLISH` 回复 / `PINGRESP`；
- `AT+MIPCFG="encoding",0,1,1` 让收发都走 HEX 串，避开 JSON 里的双引号与 AT 引号冲突；
- `"ackmode",0,1` 打开 `+MIPURC: "ack",<id>,<字节数>`，可以确认字节真的发出去了；
- `+MIPURC: "rtcp"` 的分片按剩余长度重新拼包；掉线（`"disconn"`）由主循环重新拨号连接。

## 目录结构

```
STM32L431-MQTT/
├─ Projects/STM32L431RCT6/Application_ADXL355_MQTT/
│   ├─ Inc/mqtt_cfg.h        ← 唯一需要改的配置文件
│   ├─ Inc/ Src/             应用代码（main / angle_report / mqtt_at / mqtt_payload / adxl355）
│   └─ MDK-ARM/Project.uvprojx    Keil 工程，target 名 STM32L431RCT6
├─ Drivers/                  ST CMSIS + STM32L4 LL 库（随工程自带，保持其原始许可）
├─ docs/ML307R_OneNET_接入调试指南.md   深度调试文档：AT 时序、MQTT 字节表、错误码、排障决策树
└─ tools/                    Token生成器.pyw（含"测试连接"）+ 生成Token工具.bat
```

模块划分：

| 文件 | 职责 |
|------|------|
| `Src/main.c` | 初始化、主循环：采数 → 滤波 → 判触发 → 交给上报层 |
| `Src/adxl355/` | ADXL355 SPI 驱动（ADI no-OS 风格） |
| `Src/angle_report.c` | 角度基准与触发判定（阈值/去抖/最小间隔）、上报 JSON |
| `Src/mqtt_payload.c` | MQTT 报文构造（纯函数，不含 IO） |
| `Src/mqtt_at.c` | AT 串口状态机 + DIY MQTT 客户端（连接、收发、保活、重连） |

## 硬件与接线

| 用途 | MCU 侧 | 说明 |
|------|--------|------|
| ADXL355 | SPI3：SCK=PC10、MISO=PC11、MOSI=PC12、CS=PA15 | 3.3 V，±2 g |
| ML307R 主 UART | USART3：PC4=TX → 模块 RXD，PC5=RX ← 模块 TXD | 115200 8N1 |
| 调试 printf | USART2：PA2=TX、PA3=RX | 115200，排障全靠它 |

供电注意：Cat.1 模组发射瞬间电流很大，电源要能稳定给出 **≥2 A**（按你所用模组板的电压档，一般 3.7～4.2 V 或 5 V）。上电不足的表现是"能注网但一发消息就复位/无输出"，很容易被误判成代码问题。

## 快速上手

1. **建平台产品**。OneNET 新建**新版物模型**产品，接入协议必须选 **MQTT(3.x)**——这个选项在**创建产品**时定，设备页看不出来；建成 MQTT 5.0 的设备只存在于 v5 集群，本工程这套 v3.1.1 客户端永远连不上。物模型里加两个属性：标识符 **`Roll`**、**`Pitch`**（**区分大小写**），数据类型选 **浮点型**，然后发布 TSL。
2. **拿设备密钥**。设备详情页复制 **设备密钥**（不是产品页的 AccessKey，两者不同）。
3. **生成 token 并验证**。双击 `tools/生成Token工具.bat`，填 产品ID / 设备名称 / 设备密钥 / 有效天数（建议 3650），点 **"生成 Token"**，再点 **"测试连接"**：日志区会逐行变绿，最后给出"能连上、能上报"的结论。这一步不用烧板，也不用命令行，密钥只填在输入框里，程序不读写任何文件。
4. **改配置**。点"复制结果"，把四行 `#define` 覆盖到 `Inc/mqtt_cfg.h` 的同名宏；`MQTT_SUB_TOPIC` 生成器不输出，自己在发布 topic 末尾加 `/reply`（不想收平台回复可以置 `""`）。
5. **编译烧录**。Keil MDK（装好 STM32L4 pack）打开 `Projects/STM32L431RCT6/Application_ADXL355_MQTT/MDK-ARM/Project.uvprojx`，选 target `STM32L431RCT6`。
6. **看日志**。上电后调试串口应依次出现 `+MIPOPEN: 0,0` → `CONNACK rc=0` → `SUBACK code=0`；转一下板子，超过 5° 就出现 `PUBLISH ... "ack",0,<len>`，随后 `{"id":"1","code":200,"msg":"success"}`。

## 关键配置（全部在 `Inc/mqtt_cfg.h`）

| 宏 | 含义 |
|----|------|
| `ONEONET_SERVER` / `ONEONET_PORT` | MQTT(3.x) 集群地址与端口 |
| `ONEONET_CLIENT_ID` | = 设备名称 |
| `ONEONET_USERNAME` | = 产品ID |
| `ONEONET_PASSWORD` | 长期 token，**原样照抄，一个 `%` 都不要动** |
| `MQTT_PUB_TOPIC` | `$sys/<产品ID>/<设备名>/thing/property/post` |
| `MQTT_SUB_TOPIC` | 上面加 `/reply`，`""` 表示不订阅 |
| `MQTT_KEEPALIVE_S` | 秒，`PINGREQ` 周期取它的一半 |
| `ANGLE_TRIGGER_DELTA_MDEG` | 触发阈值，单位**毫度**（`5000` = 5°） |
| `ANGLE_TRIGGER_DEBOUNCE_CNT` | 连续几帧超阈值才算数（抗抖） |
| `ANGLE_REPORT_MIN_INTERVAL_MS` | 两次上报的最小间隔 |

上报 JSON（新版物模型要求带 `version`，否则回 2407）：

```json
{"id":"1","version":"1.0","params":{"Roll":{"value":6.474},"Pitch":{"value":15.872}}}
```

## 排障速查

| 现象 | 判断 |
|------|------|
| `AT+MIPSEND` 报 `+CME ERROR: 553` | TCP 还没连上。`MIPOPEN` 回 `OK` 只是"命令受理"，真结果是异步的 `+MIPOPEN: 0,<result>`，必须等它并确认 `result==0` |
| `551` 无连接可关 / `552` id 已被用 / `554` socket 创建失败 | 见模组 TCP/IP 手册错误码表 |
| `CONNACK rc=4` 或直接沉默 | token 不对，或设备是 MQTT 5.0（不在 3.x 集群） |
| 平台回 `2306` | 标识符不存在：查大小写、查物模型是否已发布 |
| `2211` | 值类型不符：属性类型要选浮点型 |
| `2407` | `version` 字段不对 |
| 模组整个 AT 口静默 | MQTT 子系统卡死，只有模组**彻底断电 ≥10 s** 才能恢复，MCU 复位无效 |

更长的排障决策树、整段 AT 时序注释、MQTT 字节表都在 `docs/ML307R_OneNET_接入调试指南.md`。

## 改代码前必读的坑（都在这块板子上真实踩过）

- **栈**。`startup_stm32l431xx.s` 里 `Stack_Size` 已从模板的 `0x400` 提到 `0x800`；AT 驱动一行命令缓冲是 1 KB，**任何大数组一律 `static`，别放栈上**，否则症状是"模组收到了正确长度却行为诡异"。
- **SysTick**。ST 例程的 `LL_InitTick` 只开计数器不开 `TICKINT`/NVIC，所有基于节拍的超时会永远不到期（驱动启动时会打印 `tick ok: 4 -> 1204` 自检）。
- **芯片与 startup 必须配套**。工程模板来自 ST 的 L432 例程，实际芯片是 **STM32L431RCT6**：L432 向量表里 USART3 中断位置是空的，一收到模组回复就取 0 号向量跑飞且毫无输出。`Drivers/CMSIS` 里仍带着 l432 的厂商文件，别误用。
- **ATE0** 关回显，否则每条命令会被打回来两次。
- **文件编码**。带中文的 `.bat` 必须存成 **GBK**（cmd 按 OEM 936 读 bat），源码与文档统一 UTF-8；混用会把中文注释变成乱码。

## 安全说明

- 本仓库里 `ONEONET_PASSWORD` 是**占位文字，不含真实 token**。token 等同设备密码，请不要提交到公开仓库，也不要贴进群里/网上。
- 调试串口会把 `AT+MIPSEND=0,171,"10A8..."` 整段 HEX 打出来，**那段 HEX 解开就是 token**。公开日志、发帖、录视频之前，把这类行删掉。
- `tools/Token生成器.pyw` 不读写任何文件：密钥只由使用者当场输入，生成的 token 也不会落盘。

## 许可

本仓库暂未添加 LICENSE。`Drivers/`（ST CMSIS 与 STM32L4 LL/HAL 头文件）、`Src/adxl355/`（ADI no-OS 驱动）保留其各自的原始许可，自研部分（`Src`、`Inc`、`tools`、`docs`）的许可方式待定。
