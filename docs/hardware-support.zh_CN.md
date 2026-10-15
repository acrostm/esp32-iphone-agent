[English](hardware-support.md) · **简体中文**

# 硬件支持管理

| 板型 ID | 硬件 | 固件入口 | 框架 | 验证状态 |
| --- | --- | --- | --- | --- |
| 上游通用 S3 | 原有 8 MB Flash／8 MB OPI PSRAM 配置 | `hardware/hid_bridge`、`hardware/flash.sh` | Arduino ESP32 | 原有上游 CI |
| `m5stack-stopwatch` | M5Stack StopWatch C152，S3、16 MB Flash、8 MB OPI PSRAM、466×466 CO5300 圆屏 | `hardware/esp-idf` | ESP-IDF 5.5.3，不依赖 Arduino | 编译、本地测试及基础实机回归通过；手机验收待完成 |

参见 [StopWatch 安装和验收](boards/m5stack-stopwatch.zh_CN.md)。
仅有屏幕和 ESP32-S3 芯片不代表兼容；其他设备需独立确认屏幕、电源、引脚并实机验收。
不支持的板型 ID 会在配置时拒绝。新增型号须保留原有设备支持。

## 新增型号流程

1. 增加 `hardware/esp-idf/boards/<厂商-型号>/board.json`、`sdkconfig.defaults`
   和 `partitions.csv`，记录芯片、实际 Flash／PSRAM、屏幕及维护者。
   不得从其他 S3 设备推测容量或显示引脚。
2. 实现对应初始化与界面，在 CMake 中明确选择。目前会检查 M5Unified 是否识别为
   StopWatch，再启用桥接；仅新增配置文件不能启用未支持硬件。
3. 扩展 `hardware/build.py` 的支持型号、预期分区和输出路径，每板型使用独立产物目录，
   不削弱现有型号的烧录保护。
4. 锁定依赖，增加本地测试和 CI 矩阵项，维护双语文档。生成配置、依赖缓存、
   个人路径、凭据与设备日志不进入 Git。
5. 分开报告编译、本地测试和设备测试。硬件验收包含屏幕／按键、USB 诊断、
   蓝牙配对、AirPlay、HID 校准、持续运行、功耗与休眠唤醒。
   不应将编译成功等同于完整手机控制成功。

BIN、ELF、MAP 与 SHA-256 清单生成于 `hardware/build/<板型ID>/`。
源码和依赖锁文件进入 Git；固件二进制放在 CI 产物或 Release，不提交进源码历史。
烧录须明确输入串口，不自动选择第一个 USB 设备。
