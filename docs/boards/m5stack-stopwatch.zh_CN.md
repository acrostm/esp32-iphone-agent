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
完整 App 工作流见上游 README；手机配对、视频吞吐和 Agent 完整任务仍待验收。

Build: PASS（ESP-IDF 5.5.3，Apple Silicon）

Host tests: PASS（命令解析、界面状态／计时、共享 C 测试、14 项产物／依赖测试）

Device tests: PASS（本 PR 固件）：写入哈希、USB ping／status，
用户确认屏幕、A／B 按键和运行时间正常。

Unverified: 蓝牙配对、AirPlay、HID 校准、电池读数、功耗、
休眠唤醒和持续 Agent 任务。
