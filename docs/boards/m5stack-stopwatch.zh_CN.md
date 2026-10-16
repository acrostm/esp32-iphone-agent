[English](m5stack-stopwatch.md) · **简体中文**

# M5Stack StopWatch C152

维护者：**jiachen**。板型 ID：`m5stack-stopwatch`。

## 支持范围

原生桥接使用 ESP-IDF 的 NimBLE、Wi-Fi、NVS、FreeRTOS，乐鑫 TinyUSB，
以及 M5Unified／M5GFX 官方屏幕与电源驱动。
原生编译不安装或链接 Arduino IDE／CLI／core、NimBLE-Arduino。
上游其他设备的 Arduino 实现保留在独立入口。

M5Unified 固定到支持原生 StopWatch 的提交
`e125e73f28b6b83e621e48df432af426bb2ff935`，M5GFX 固定为 0.2.33，
全部托管依赖和哈希见 `hardware/esp-idf/dependencies.lock`。
不将第三方驱动源码复制进仓库。复用上游纯 C AirPlay／bplist／GOP，
不编译其 Arduino 封装；保留 GPL 声明与许可证约束。

界面显示 USB、蓝牙、Wi-Fi、AirPlay、IP、电池和运行时间；A 键翻页、B 键调亮度，
详情页显示视频速率和内存。不显示凭据或手机镜像内容。
显示操作在界面任务执行，命令与视频使用工作任务。
视频分批处理、预留蓝牙控制缓冲；关闭断言也会执行必要初始化。

关闭外部 5 V 输出、扬声器、麦克风、IMU 和 RTC，不修改后部 UART／USB 切换。
此固件替换原厂秒表应用，不实现全部原厂功能。电池准确度、功耗行为仍需测量。

## Apple Silicon／Linux 编译

按[乐鑫官方指引](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/get-started/)
安装 **ESP-IDF 5.5.3** 和 ESP32-S3 工具，激活 `export.sh`。
脚本要求 Python 3.10 或以上，组件管理器下载锁定依赖，无需 Arduino 工具。
M5 官方驱动通过 ESP-IDF CMake 接口直接使用。

```bash
python3 hardware/tests/native/run.py
python3 hardware/build.py --board m5stack-stopwatch --build
python3 hardware/build.py --board m5stack-stopwatch --verify
```

也可在配置好的环境中双击 `Build-StopWatch.command`。
可选的 `.idf-local.json` 保存 `IDF_PATH`、`IDF_TOOLS_PATH`、
`IDF_PYTHON_ENV_PATH` 和 `extra_bin` 列表，不提交个人路径。
原生 CI 与 Arduino 编译任务分开，检查组件列表、编译宏及缓存路径，阻止引入 Arduino。

配置使用 16 MB 四线 Flash、8 MB 八线 PSRAM。
板型识别、PSRAM 或屏幕初始化失败时不启用桥接。
正常运行的 USB 名称为 **StopWatch iPhone Agent**，提供 CDC 诊断和鼠标／键盘／Home HID。
启动时不自动移动指针或打字；CDC `0 ping`、`1 status` 是只读诊断。
打字仅支持 ASCII，单次鼠标移动每轴限制 ±4096。

## 烧录和恢复

进入下载模式，确认当前串口后执行：

```bash
python3 hardware/build.py --board m5stack-stopwatch --flash --port /dev/cu.已确认的串口
```

或双击 `Flash-StopWatch.command`，脚本先编译，再要求输入串口。
检查产物哈希、芯片、Flash 容量、分区校验和布局后，仅写入启动程序 `0x0`、
分区表 `0x8000` 和应用 `0x10000`，不单独写 NVS／PHY，不整片擦除。
新分区布局仍可能与原厂设置不兼容。不要使用上游 8 MB Arduino 烧录脚本。

恢复原装应用请按
[M5Stack 官方指南](https://docs.m5stack.com/en/guide/stopwatch/restore_factory)
选择准确的 StopWatch 固件。M5Burner 可用于恢复原厂，不是原生编译链的依赖。

## iPhone 与验收

使用已有 `ios/IPhoneAgent.xcodeproj`，为 IPhoneAgent 和 AgentActivity 配置自己的
Bundle ID 和签名 Team。不提交 `ios/Config/Local.xcconfig`、签名材料或模型密钥。
免费 Personal Team 可将本地 `IA_ENTITLEMENTS`、`IA_WIDGET_ENTITLEMENTS` 设为空，
手动加入热点。开启开发者模式，在解锁手机上运行 App。

将设备连接手机数据接口，在 App 中通过蓝牙选择 `ESP32 Agent XXXX`，
用 App 经加密蓝牙读取的凭据加入 `ESP32Agent-XXXX` 热点，
开启屏幕镜像到 **ESP32 Agent**，开启辅助触控并校准。
连接设备热点时保持蜂窝网络可用，以访问模型 API。
完整 App 工作流见上游 README；下方验收仅覆盖当前 StopWatch 型号，
其他带屏 ESP32-S3 需要独立硬件配置与实测。

### 蓝牙已配对，但无法加入 Wi-Fi

蓝牙配对与加入 Wi-Fi 是两个步骤。蓝牙就绪后 App 自动通过加密特征读取热点信息，
并发请求共用一次读取；30 秒后仍无结果会提示超时，不再无限等待。
原有 5 秒无通知检测不会打断这次配对读取。
进入 **设置 → Board（开发板）→ Wi-Fi**，可查看热点名、密码、复制按钮及错误信息。
读取成功便显示手动入网信息，不必等自动连接报错；取消系统入网提示也能看到密码。

未启用 Hotspot Configuration 权限的构建无法自动入网。
将复制的密码粘贴到 iPhone“设置 → Wi-Fi”中对应的 `ESP32Agent-XXXX` 热点，
加入后返回 App，再开启屏幕镜像。
如果加密读取失败，应排查蓝牙配对，而不是直接判断 Wi-Fi 密码错误；
重试后应显示读取错误或超时。不要把热点密码发到公开日志中。

Build: PASS（ESP-IDF 5.5.3，Apple Silicon）

Host tests: PASS（命令解析、任务状态／计时和心跳失联、共享 C 协议、
一万帧缓存回收、14 项产物／依赖测试）

Device tests: PASS（2026-10-16 的当前 StopWatch）：写入哈希、USB ping／status，
用户确认屏幕、A／B 按键和运行时间正常；已验证加密 BLE 凭据读取、热点连接、
AirPlay 接收和 App 解码（实际观察为 718 × 1560）。用户确认“回到主屏幕后结束”
任务执行成功，板子显示 `done`。这属于基础任务回归，不代表长期或任意任务认证。

Unverified: 付费签名下的自动加入热点、其他屏幕型号、电池读数、功耗、
休眠唤醒及长期／复杂 Agent 任务。免费 Personal Team 当前签名配置仍不支持
自动入网；手动加入是临时回退，不代表自动联网功能已经完成。


## 任务反馈与视频就绪检查

A 键在连接状态、任务进度和诊断三个页面间切换，B 键调节亮度。任务页显示阶段、
步骤、操作、耗时和最近四条操作记录。新任务或终态会自动切换到任务页；完成结果
持续保留。超过 15 秒未收到任务心跳时显示 `STATUS LOST`，避免误认为任务仍正常执行。

执行手机任务时，开发板 USB 必须连接 iPhone；仅接 Mac 无法通过 HID 操作手机。
串口诊断时再接回 Mac。任务结束后将解锁的手机接回 Mac，即可读取 App 保存的日志，
不必为了读取日志中断正在运行的任务。

启动任务会先在前台等待实际解码出视频帧，再返回手机主屏幕。屏幕镜像中的勾选不代表
视频已就绪。原生 `status` 增加 `ui:1`（任务显示协议）、`vcfg`（视频配置字节数）、
`vaus`（已缓存访问单元数）、`vwait`（等待关键帧）和 `vdrop`（GOP 溢出次数）。
`diag` 返回有限的协议事件，不包含图像或凭据。`snap` 区分缺少配置、关键帧或视频帧。
开发板重启后需要重新选择屏幕镜像。

仅原生固件支持的 `task <json>` 接收有界的 `phase`、`action`、`step`、`ms`、
`elapsed`、`code` 和 `reset` 字段。App 发送规范化操作名和两秒一次的心跳，
不发送提示词或输入文本。未声明 `ui:1` 的旧开发板不会收到任务显示命令。
手机同时保留 Live Activity 反馈，实测范围见上方验收记录。


持续推流会在固定 PSRAM 缓存被历史数据占满前回收已完整发送的访问单元，保留未发送
帧的依赖。缓存中的 IDR 已回收后，独立 `snap` 无法重放完整 GOP，会明确报错，
不会发送不可解码的 P 帧。此时新建视频流需要新的关键帧／镜像会话。
这保护的是不中断的视频流，不能在丢包后凭空生成关键帧。
任务状态采用独立请求路径，不再被长截图阻塞于 HID 串行队列后。
启动阶段容许短暂的 BLE 重连，推流模式等待返回主屏幕后实际捕获的新帧，避免强制重传
完整 GOP；启动失败原因会直接显示在 App 首页。
