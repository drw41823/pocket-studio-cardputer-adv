# Pocket Studio for Cardputer ADV

面向 M5Stack Cardputer ADV 的口袋音乐工作站。当前音乐固件版本：v0.25。

## 功能

- **STUDIO**：三条旋律轨（每轨四复音）与独立鼓组，实时录音、STEP步进输入、1/2/4小节循环。
- 调内三和弦、琶音器、项目调性、全局BPM、音量及轨道静音。
- **SOUND LAB**：麦克风录音、WAV导入、波形裁剪、持续循环、SD音色槽。
- 基础波形双振荡器、音高/滤波包络、LFO、失真和位深处理。
- **PLAYER**：SD目录浏览及WAV/MP3流式播放。

操作详见 [完整使用手册](docs/manual-v0.25.md)。SOUND LAB、SMP音色与播放器需要SD卡。音色槽及WAV素材不随LOCAL编曲项目打包。

## 构建

需要Python、PlatformIO及可用的C++工具链。Windows PowerShell示例：

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
python -m platformio run -e cardputer_adv
```

可通过环境变量 `PLATFORMIO_CORE_DIR`、`PLATFORMIO_CACHE_DIR`、`PLATFORMIO_BUILD_CACHE_DIR` 和 `PIP_CACHE_DIR` 自定义工具与缓存位置。

固定依赖：Espressif32 6.7.0、Arduino ESP32 2.0.16、M5Unified 0.2.25、M5GFX 0.2.32；M5Cardputer固定提交见platformio.ini。目标ESP32-S3、8MB Flash、无PSRAM。

刷机前请自行识别设备端口并备份已有固件与存档，勿默认擦除Flash。通过PlatformIO上传：

```powershell
python -m platformio run -e cardputer_adv -t upload --upload-port YOUR_PORT
```

应用二进制输出在 `.pio/build/cardputer_adv/firmware.bin`。它是应用分区镜像，不应直接当作地址0的整片Flash镜像；M5Burner发布应另行准备合适的镜像和烧录配置。

## 主机测试

安装Zig或支持C++17的编译器后运行：

```powershell
python scripts/run-tests.py
# 或显式选择Zig
python scripts/run-tests.py --zig /path/to/zig
```

九套测试包含音频DSP、编曲、存储、采样、OSC/LFO、采样音轨、SD事务、MP3及ADV键盘。`tests/fixtures` 仅包含本项目生成的正弦测试音，出处说明见该目录。

## 文件与隐私

这个源码发布副本仅包含音乐固件源码、必要依赖代码、测试和使用说明。未包含设备备份、NVS/Flash转储、个人录音、个人项目、SD副本、调试日志、聊天归档、本机开发路径或宣传视频配乐。固件会在SD卡上创建自己的素材和音色文件。

## 第三方代码

MP3解码使用 [minimp3](https://github.com/lieff/minimp3)，其CC0许可随 `include/third_party/minimp3/LICENSE` 保留。M5Stack与其他库通过PlatformIO下载，各自许可归原作者。此仓库尚未为项目自有代码指定开源许可证。

## M5Burner

[固件商场发布准备](docs/m5burner-publishing.md)：账号流程、资料清单、镜像格式及隐私注意事项。当前没有向商场发布固件。
