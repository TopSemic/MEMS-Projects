# ML307R(Cat.1, AT 固件) + STM32L431 + OneNET 新版物模型 接入调试指南

面向对象：第一次用这颗模组、或者第一次接 OneNET 新版物模型的嵌入式开发。
读完你应该能：独立把一条传感器数据发到 OneNET 并看懂平台回复，且知道每一步失败时该看哪一行日志。

工程位置：`MEMS/STM32L431-MQTT/Projects/STM32L431RCT6/Application_ADXL355_MQTT`


---

## 1. 整体结构

```
ADXL355 --SPI3--> STM32L431RCT6 --USART3(PC4=TX/PC5=RX,115200)--> ML307R --4G--> OneNET
                     |                                                     183.230.40.96:1883
                     +--USART2(PA2/PA3,115200)--> 调试串口 printf
```

- STM32 负责：读加速度、算 Roll/Pitch、判断变化是否 ≥5°、**自己拼 MQTT 报文**。
- ML307R 只负责：拨号上网 + 提供裸 TCP（`AT+MIP*` 指令集）+ 把 TCP 收到的字节以 AT 行的形式上交。
- **MQTT 协议栈不在模组里。** 模组的 `AT+MQTT*` 指令集在这块板子上无法用官方 token 建会话（详见 §4.3），所以我们只用它的 TCP 能力。

代码分工：

| 文件 | 职责 | 能否在 PC 上单测 |
|---|---|---|
| `Src/mqtt_payload.c` | 纯函数：拼 CONNECT/PUBLISH/SUBSCRIBE/PINGREQ、HEX 编解码、角度 JSON | 能（零 MCU 依赖） |
| `Src/angle_report.c` | 纯函数：阈值/去抖/限速判定 | 能 |
| `Src/mqtt_at.c` | AT 收发、异步 URC 等待、TCP 会话、MQTT 组包/拆包、保活重连 | 不能（依赖 LL 库/中断） |
| `Inc/mqtt_cfg.h` | 所有需要改的参数（产品ID/设备名/token/引脚/阈值） | — |

---

## 2. 上手要改的东西

### 2.1 Token 怎么生成（不用命令行）

1. 控制台 → 找到你的产品和设备 → 设备详情页复制**设备密钥**（一串 base64，可能带 `=`，**原样复制，不要做 URL 编码**）。注意用的是设备密钥，不是产品的 AccessKey，两者不同。**密钥只由你当场填进生成器，工程里不存放任何密钥文件**，生成器也不读写本地文件。
2. 双击 `STM32L431-MQTT/tools/生成Token工具.bat`（启动同目录 `Token生成器.pyw`，纯本机计算，密钥不外传）。
3. 填四项：**产品ID**、**设备名称**、**设备密钥**、**有效天数**（默认 3650；别用 30 天，到期设备就哑了）。下面还有"服务器/端口"两项，默认已填好 `183.230.40.96` / `1883`，只有换平台才需要动。
4. 点"生成 Token"，结果区会显示到期日、Token 本体，以及**四行可直接粘贴的 `#define`**（`ONEONET_CLIENT_ID` / `ONEONET_USERNAME` / `ONEONET_PASSWORD` / `MQTT_PUB_TOPIC`）。
5. 点"复制结果"，贴进 `Inc/mqtt_cfg.h` 覆盖同名宏，重新编译烧录。`MQTT_SUB_TOPIC` 生成器不输出，自己把发布 topic 末尾加上 `/reply` 即可（想收平台回复才需要）。
6. **烧板前想确认这份参数真的能用**：在同一个窗口点"测试连接"，日志区逐行变绿到最后"结论：可以用"就说明 token、topic、物模型格式平台全认（它会往平台写一条 Roll=12.345 的测试数据）。见 §9。

为什么要这么算：token 是拿**设备密钥**算出来的一串"临时密码"。设备连平台时报上产品ID、设备名和这串 token，平台用它自己存的那把同一密钥重算一遍，对得上才放行——所以真正的密钥从不发上网，token 里也推不出密钥。到期时间参与了计算，因此换了设备、改了设备名、重置过密钥、或者到期了，都得重新生成一份 token 再烧一次板；平时不用碰这个。

### 2.2 只改 `Inc/mqtt_cfg.h`

```c
#define ONEONET_SERVER    "183.230.40.96"   /* MQTT(3.x) 集群 */
#define ONEONET_PORT      "1883"
#define ONEONET_CLIENT_ID "Vibration_001"   /* = 设备名 */
#define ONEONET_USERNAME  "F0wguvAinT"      /* = 产品ID  */
#define ONEONET_PASSWORD  "version=2018-10-31&res=products%2F...&sign=...%3D%3D"  /* 长期 token */

#define MQTT_PUB_TOPIC  "$sys/<产品ID>/<设备名>/thing/property/post"
#define MQTT_SUB_TOPIC  "$sys/<产品ID>/<设备名>/thing/property/post/reply"  /* 可选，"" = 不订阅 */
#define MQTT_KEEPALIVE_S 120               /* 数值，PINGREQ 周期 = 一半 */

#define ANGLE_TRIGGER_DELTA_MDEG 5000      /* 5° */
#define ANGLE_TRIGGER_DEBOUNCE_CNT 3
#define ANGLE_REPORT_MIN_INTERVAL_MS 10000U
```

换一套产品/设备时，下面这张表逐项说明该填什么：

| 宏 | 值从哪来 | 换设备是否必改 |
|---|---|---|
| `ONEONET_SERVER` | MQTT(3.x) 集群 IP `183.230.40.96`（或 `mqtts.heclouds.com` 解析结果） | 一般不改 |
| `ONEONET_PORT` | `1883`（非加密） | 不改 |
| `ONEONET_CLIENT_ID` | **设备名称**（token 生成器第 2 项填的就是它） | 必改 |
| `ONEONET_USERNAME` | **产品ID**（生成器第 1 项） | 必改 |
| `ONEONET_PASSWORD` | 生成器输出的 Token 整行 | 必改 |
| `MQTT_PUB_TOPIC` | `$sys/<产品ID>/<设备名>/thing/property/post`（生成器已拼好） | 必改 |
| `MQTT_SUB_TOPIC` | 上面 + `/reply`，用来收平台回复；不需要就写 `""` | 必改（或留空） |
| `MQTT_KEEPALIVE_S` | 与 CONNECT 里的 keepalive 一致，PINGREQ 周期取一半 | 不改 |
| `ANGLE_TRIGGER_DELTA_MDEG` | 触发阈值，单位 **毫度**（5000 = 5°） | 按产品需求 |
| `ANGLE_TRIGGER_DEBOUNCE_CNT` | 连续几次超阈值才算真动（抗振） | 按产品需求 |
| `ANGLE_REPORT_MIN_INTERVAL_MS` | 两次上报最小间隔（防刷流量） | 按产品需求 |

`MQTT_PUB_TOPIC` / `MQTT_SUB_TOPIC` 里的产品ID、设备名必须和上面三个宏完全一致，写错了平台按未知 topic 丢包，模组侧看不出异常。

三条铁律：

1. **token 原样照抄，一个 `%` 都不要动。** 官方生成格式里 `res` 用 `%2F`、签名 base64 的 `==` 写成 `%3D%3D`。把它"还原"成 `/` 或去掉补位，模组/平台会出各种玄学失败。
2. 产品/设备的**通信协议必须选 MQTT(3.x)**。建成 MQTT 5.0 的设备只存在于 v5 集群（183.230.102.120），v3.1.1 客户端永远连不上，症状是 CONNACK rc=4 或直接沉默。协议是在**创建产品**时定的，设备页看不出来。
3. **设备密钥和 token 等同密码。** 不要把密钥写进工程里任何文件，也不要提交、贴到群里/网上——注意本工程的调试串口会把 `AT+MIPSEND=0,171,"10A8..."` 整条 HEX 打出来，**那串 HEX 解开就是 token**，公开日志=泄密。

---

## 3. 第一次接线/上电

- 模组供电用 **5V/≥2A** 独立源，别只靠开发板 3.3V LDO。Cat.1 发射瞬间电流可达 2A 级，压跌会导致模组复位或 STM32 掉电重启——现象是日志开头莫名出现两次 `Hello STM32L431...`。
- `PC4 = MCU TX → 模块 RXD`，`PC5 = MCU RX ← 模块 TXD`，共地。波特率 115200，无流控。
- 先用 USB-TTL 直接连模块主 UART 手发 `AT` 确认能回 `OK`，再接 STM32。
- 如果 STM32 发 `AT` 完全无回复，且以前跑过 MQTT/透传相关代码：模组的 AT 口可能**卡死**，唯一恢复手段是**模组彻底断电 ≥10 秒**（只复位 MCU 没用）。

---

## 4. 必须理解的四个概念

### 4.1 AT 是"两段式"：`OK` ≠ 事情做成

`AT+MIPOPEN` / `AT+MIPCLOSE` 这类命令的响应是：

```
AT+MIPOPEN=0,"TCP","183.230.40.96",1883
OK                      ← 仅表示"命令受理"
+MIPOPEN: 0,0           ← 真正的结果（异步 URC），0=连接成功，非 0=失败
```

`AT+MIPCLOSE` 同理，结果行是 `+MIPCLOSE: <id>[,<ret>]`，而且**可能晚到好几条命令之后**。

踩过的坑（第一次上板的真实失败）：代码看到 `OK` 就当连接成功，立刻 `AT+MIPSEND` → `+CME ERROR: 553`（TCP/IP 未连接）。而且上一条 `MIPCLOSE` 的 URC 在 MIPOPEN 之后才冒出来，说明关闭动作还在进行。

正确姿势：
1. 关连接后要**等它的 `+MIPCLOSE` URC**（若关命令直接回 `+CME ERROR: 551`，表示本来就没连接，不会再有 URC，别死等）。
2. 开连接后要**等 `+MIPOPEN: 0,<result>` 并检查 result==0**，才能发数据。

### 4.2 收发数据用 HEX，别用 ASCII

JSON 里有双引号，AT 命令的参数也用双引号定界，两者会打架。解决：

```
AT+MIPCFG="encoding",0,1,1     ← send_format=1(HEX), recv_format=1(HEX)
```

**这条必须写在 `AT+MIPOPEN` 之前**（`ackmode`、`rcvbuf` 同样是"建连接前配置"，连上之后禁止改）。

发：`AT+MIPSEND=<id>,<字节数>,"<大写HEX>"`，HEX 串长度必须正好 = 2×字节数（模组只校验长度不校验内容，所以内容错了不会报错，只会发出去一坨垃圾）。行内直发单包上限约 1460 B；驱动里保守按 200 B/条分块，TCP 是字节流，MQTT 解析端会自己拼回去。

收：`+MIPURC: "rtcp",<id>,<len>,<HEX>`。**一个 MQTT 包可能被拆成两条 rtcp**，所以解码后要累积在缓冲区里按包长切分（`payload_mqtt_packet_len()`）。

### 4.3 `ackmode=1` 是取证用的

```
AT+MIPCFG="ackmode",0,1
```
之后每次发送都会上报 `+MIPURC: "ack",0,<服务器确认收到的字节数>`。

- `+MIPSEND: 0,171` 只代表"进了协议栈缓存"。
- `"ack",0,171` 才代表**对端 TCP 确认收到了 171 字节**。

有了它，"STM32→模组"、"模组→服务器"、"服务器有没有回"三段可以一眼分开，不用再猜。

### 4.4 为什么不用 `AT+MQTT*`

模组自带 MQTT 客户端（`AT+MQTTCONN/MQTTPUB/...`），但实测：**同一条官方 token 字节，PC 上发原始 CONNECT 得 CONNACK rc=0，模组 `AT+MQTTCONN` 永远停在 `+MQTTURC: "conn",0,1`**；去掉 `%3D` 补位、换成无 `%` 的等价 token 也一样。结论是毒在 AT 固件的 MQTTCONN 解释器/状态机，不是 token。所以本方案自己拼 MQTT 报文走裸 TCP——模组只当串口转 TCP 的桥。

---

## 5. 完整时序（照抄即可）

```
AT                                   → OK                    同步 & 确认 AT 口活着
ATE0                                 → OK                    关回显，省一半解析工作
AT+MIPCLOSE=0                        → OK / +CME 551         清掉上次残留
(+MIPCLOSE: 0                        ← 异步，等到它或 551 就不等)
AT+MIPCFG="encoding",0,1,1           → OK                    收发都用 HEX（必须在 OPEN 前）
AT+MIPCFG="ackmode",0,1              → OK                    要 ACK 取证（必须在 OPEN 前）
AT+MIPOPEN=0,"TCP","183.230.40.96",1883
                                     → OK
                                     → +MIPOPEN: 0,0        ← 必须等到这行且 result=0
AT+MIPSEND=0,171,"10A8..."           → +MIPSEND: 0,171 / OK  CONNECT 包
+MIPURC: "ack",0,171                                       服务器收到了
+MIPURC: "rtcp",0,4,20020000                                 CONNACK，HEX 是 20 02 00 RC
   → 20020000 解出 rc=0 = 登录成功
AT+MIPSEND=0,62,"823C..."            → SUBSCRIBE（订阅 post/reply，可选）
+MIPURC: "rtcp",0,...,90...                                  SUBACK
（触发上报）
AT+MIPSEND=0,139,"30..."             → PUBLISH(QoS0)
+MIPURC: "ack",0,139
+MIPURC: "rtcp",0,...,30...                                  平台回复，内含 code=200 success
（每 60 s）AT+MIPSEND=0,2,"C000"     → PINGREQ；回 PINGRESP(2字节 D000)
AT+MIPCLOSE=0                        → 收尾
```

`+MIPURC: "disconn",0,<st>` 表示链路被切（1=服务器关，2=异常，3=PDP 去激活）；收到就把"已连接"标志清零，等下一次触发时重连即可。`"drop"` 表示接收缓存溢出，说明你处理 rtcp 太慢。

---

## 6. MQTT 报文要点（`mqtt_payload.c` 已实现，这里只讲为什么）

固定头 = 1 字节(类型|标志) + **剩余长度 varint**（1~4 字节，每字节低 7 位有效、最高位 1 表示还有下一字节）。

| 包 | 首字节 | 备注 |
|---|---|---|
| CONNECT | `0x10` | `00 04 "MQTT" 04 C2 <keepalive:2>` + clientid/username/password（各带 2 字节长度） |
| CONNACK | `0x20` | **HEX 形态是 `200200RC`**，不是 `200002`（这里写反过一次，导致"服务器没回"的误判） |
| PUBLISH QoS0 | `0x30` | topic 长度 2B + topic + payload，**没有 packet id** |
| SUBSCRIBE | `0x82` | 标志位固定 `0x02`；packet id + topic filter + 请求 QoS(1B) |
| PINGREQ | `0xC0 0x00` | 2 字节 |
| DISCONNECT | `0xE0 0x00` | 2 字节 |

flags `0xC2` = user name + password + clean session；keepalive 120 s，**PING 周期取一半（60 s）**。

本产品的实际包长：CONNECT 171 B（token 129 B）、SUBSCRIBE 62 B、PUBLISH 139~140 B。剩余长度超过 127 时会占 2 字节 varint（例：168→`A8 01`、137→`89 01`），写死 1 字节长度的实现会在包一大之后错位。

---

## 7. OneNET 侧（平台配置，代码里看不出来的部分）

上报格式（新版物模型，属性标识符 **区分大小写**）：

```json
{"id":"1","version":"1.0","params":{"Roll":{"value":6.474},"Pitch":{"value":15.872}}}
```

| 规则 | 错的后果（都是实测） |
|---|---|
| 标识符大小写要和物模型完全一致 | 发 `roll` → `{"code":2306,"msg":"identifier not exist:identifier:roll"}` |
| 属性**数据类型**要和值匹配 | 定义成整型(int32) 发 `12.345` → `{"code":2211,"msg":"int32 type error:identifier:Roll"}`；改成浮点型后同一报文 → `{"code":200,"msg":"success"}` |
| `version` 填 `"1.0"` | 填 `2018-10-31` → `{"code":2407,"msg":"invalid version"}` |
| `id` 每次请求唯一（递增即可） | 只影响你把自己的回复和请求对上 |
| 回复 topic 是 `/property/post/reply` | 不是 `post_reply`；订阅 `/property/post/reply` 才能拿到 `code:200/2306/...` |
| 功能定义改完要**发布** | 未发布同样报 2306 |

CONNACK rc 含义：`1`=协议版本不被接受、`2`=标识符非法、`3`=服务不可用、`4`=**用户名/密码错误（token 或设备协议版本问题）**、`5`=未授权。

---

## 8. 排错决策树

先看日志里最后成功到哪一步：

| 现象 | 结论 | 处理 |
|---|---|---|
| `AT` 都无回复，`rx=0` | AT 口卡死 或 接线/供电 | 模组断电 ≥10 s；量 PC4/PC5 波形 |
| `AT` 通，`MIPOPEN` 无 URC 或失败 | 网络侧 | 看驱动自动打的 `AT+CGATT?`(应为1)、`AT+CEREG?`、`AT+CGPADDR`(要有 IP)、`AT+CESQ`(信号) |
| `CGATT:0` / 拿不到 IP | 卡：欠费/未实名/未激活/无覆盖 | 换卡或充值，与代码无关 |
| `CGATT:1` 有 IP 但 MIPOPEN 失败 | 定向卡只放白名单地址 / 服务器不可达 | 找卡商开通 `183.230.40.96:1883` |
| MIPOPEN 成功，MIPSEND 报 `+CME 553` | 把 OK 当成功、连接其实没建立 | 等 `+MIPOPEN: 0,0` 再发（§4.1） |
| `+MIPSEND: 0,N` 但一直没有 `"ack",0,N` | 字节没出模组 | 检查 MIPCFG 时序；看是否透传模式没退出 |
| 有 `"ack",0,N` 但没有 rtcp CONNACK | 服务器没回 | 抓 IP/端口、token 长度、协议版本；确认设备是 3.x |
| CONNACK rc=4 | token 错 / 设备不是 MQTT3.x | 重新生成 token；确认产品在 3.x 集群 |
| 平台回 2306 / 2211 / 2407 | 物模型问题 | 见 §7 表格，**都是平台侧改，不用改代码** |
| 一切通了但偶尔掉 | `"disconn",0,<st>` | 正常，重连逻辑接管；频繁则查供电/PDP |

模组 TCP/IP 错误码（手册附录）：`551` 未使用、`552` 已被使用、`553` 未连接、`554` socket 创建失败、`607` MQTT 客户端未释放（AT+MQTT 路径）。

---

## 9. 不烧板就能做的验证

**双击 `tools/生成Token工具.bat`**（打开 `tools/Token生成器.pyw`），把产品ID / 设备名称 / 设备密钥填进去，点 **"测试连接"**，下面日志区会一行行变绿：TCP 连上 → CONNACK rc=0 → 订阅成功 → 发布测试数据 → 平台回 `code=200 success` → PINGRESP 正常。最后一行给结论。全程不显示 token，也不往硬盘上写任何东西。

注意它会真的往平台写一条 `Roll=12.345 / Pitch=-5.432` 的测试数据（平台上会多出这条记录），这正是"固件的报文格式平台收不收"的答案。哪一步红了就照着那一步的红字查（§8 的决策树和错误码表）。

开发者侧还有一条不做界面也能跑的检查——嵌入式编译校验（只查语法/警告，不产生可烧录包）：

```sh
arm-none-eabi-gcc -c -mcpu=cortex-m4 -mthumb -DSTM32L431xx -DUSE_FULL_LL_DRIVER -Wall -Wextra \
  -I Inc -I ../../../Drivers/CMSIS/Device/ST/STM32L4xx/Include -I ../../../Drivers/CMSIS/Include \
  -I ../../../Drivers/STM32L4xx_HAL_Driver/Inc -o /tmp/x.o Src/mqtt_at.c
```
**`-DSTM32L431xx -DUSE_FULL_LL_DRIVER` 两个宏缺一不可**，少了会报一堆 `unknown type LL_GPIO_InitTypeDef`。

---

## 10. 单片机侧的代码坑（都在这块板子上真实踩过）

1. **栈太小。** Keil 模板 `startup_stm32l431xx.s` 里 `Stack_Size EQU 0x400`（1 KB），而 AT 驱动一行命令缓冲是 1024 B——放成局部数组必压穿，且 MSP 与 USART3 中断共用，症状是"模组收到了正确长度却行为诡异/输出乱掉"，极难定位。已把 `Stack_Size` 提到 `0x800`，并把所有 >256 B 的缓冲改成 `static`。**规则：这个工程里任何大数组都别放栈上。**
2. **SysTick 不跑。** ST 例程的 `LL_InitTick` 只开计数器不开 TICKINT/NVIC（原例程只用轮询式 `LL_mDelay` 所以没暴露）。所有依赖 `g_uwTickMs` 的超时都会永远不到期。驱动启动时会打印 `tick ok: 4 -> 1204` 自检，看到 `tick frozen` 就是这里。
3. **模板芯片与实际芯片不一致。** 本工程模板来自 ST 的 NUCLEO-L432KC 例程，实际芯片是 L431RCT6：L432 的向量表里 USART3 中断位置是空的，一收到模块回复就取 0 号向量跑飞且**无任何输出**。换芯片必须同时换 startup 文件和宏（`Drivers/CMSIS` 里仍带着一堆 l432 的厂商文件，别误用）。
4. **ATE0 关掉回显**，否则每条命令都会把命令本身再打回来，解析要额外处理。
5. **接收环形缓冲要够大 + 及时消费。** 这里 1024 B；主循环每圈都调 `mqtt_at_poll()`。满了模组会打 `"drop"`。
6. **串口发送要有超时逃生**（`uart_tx_byte` 里有 `s_tx_stuck` 计数），否则 USART 一旦异常整个主循环死在等 TXE。

---

## 11. 成功日志长这样（节选，HEX 已省略）

```
tick ok: 4 -> 1204
AT try #1 done: r=0 rx=6 ovr=0 tx_stuck=0
ML307R ready.
MQTT DIY connect to 183.230.40.96:1883...
AT< +MIPOPEN: 0,0
TCP connected: +MIPOPEN: 0,0
CONNECT packet 171 bytes
AT< +MIPSEND: 0,171
AT< +MIPURC: "ack",0,171
OneNET ACKed our bytes: +MIPURC: "ack",0,171
AT< +MIPURC: "rtcp",0,4,20020000
MQTT CONNACK session_present=0 rc=0
MQTT connected.
Subscribed to $sys/F0wguvAinT/Vibration_001/thing/property/post/reply
MQTT SUBACK code=0
PUBLISH 139 bytes -> $sys/F0wguvAinT/Vibration_001/thing/property/post
Angle reported: {"id":"1","version":"1.0","params":{"Roll":{"value":6.474},"Pitch":{"value":15.872}}}
OneNET ACKed our bytes: +MIPURC: "ack",0,139
MQTT PUBLISH <$sys/F0wguvAinT/Vibration_001/thing/property/post/reply>: {"id":"1","code":200,"msg":"success"}
```

最后那行是终局证明：平台把回复**发回设备**、模组以 HEX URC 上交、驱动跨 URC 拼包并解出 topic+payload。看到它就说明整条链路（含计费、鉴权、物模型）全通。

---

## 12. 已知限制 / 待办

- 上报只在倾角变化 ≥5°（3 次采样去抖、最小间隔 10 s）时发生；没有定时心跳上报，平台侧"最后上报时间"可能很久不更新——需要的话在 `angle_report.c` 里加定时条件即可。
- 平台主动下发（属性设置 `/thing/property/set`）目前没订阅；要用的话把 `MQTT_SUB_TOPIC` 换成 set topic，下行消息会走 `mqtt_at_on_message()` 回调。
- `AT+MIPSEND` 发送时会阻塞等回执（最长 10 s），期间传感器采样暂停；对实时性有要求要考虑状态机化。
- 换 OpenCPU 架构（应用直接跑在模组里、STM32 退场）的备用路线已经打通，但**烧 OpenCPU 包会替换掉 AT 固件**，属架构切换，务必先确认再动手。
