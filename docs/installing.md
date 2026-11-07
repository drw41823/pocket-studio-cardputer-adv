# 安装 Pocket Studio v0.25.1

适用：M5Stack Cardputer ADV（ESP32-S3、8MB Flash）。不宣称兼容普通Cardputer或其他M5设备。

## Windows x64 离线安装包

1. 从 [GitHub Releases](https://github.com/drw41823/pocket-studio-cardputer-adv/releases/latest) 下载 Windows 安装包并完整解压。
2. 用能传数据的USB线连接设备，关闭占用串口的软件。
3. 双击 `PocketStudio-Setup-v0.25.1.exe`，选择设备串口。
4. 备份原固件和内部项目后勾选确认，点击“安装固件”。安装会重写固件及NVS内部存档，SD卡文件不会写入。
5. 等待烧录校验完成，设备自动重启。STUDIO基础音色可不插SD使用；采样和播放器需要SD。

不需要Python或联网。安装器未代码签名，请核对下载来源与SHA256。连接失败可按住G0并按复位进入下载模式，再刷新串口。烧录中保持连接。

## 手动烧录与M5Burner

Release中的独立镜像 `pocket-studio-v0.25.1-cardputer-adv-full.bin` 写入地址为 **0x0**，目标ESP32-S3、8MB Flash。它由bootloader、分区表、boot_app0和应用合并，不是地址0x10000的单独应用镜像。

商场上架资料包仅在作者本地保留，不在GitHub提供。上架时：登录M5Stack社区账号，`USER CUSTOM → Publish`，参考包内 `m5burner-publish-info.json` 填写字段，选择完整镜像与封面上传，然后设置公开状态或取得分享码。该JSON只是填写参考，不是M5Burner专用导入格式。如果设备列表没有ADV，请先确认匹配类别，勿宣称普通Cardputer已经兼容。

官方教程：https://docs.m5stack.com/zh_CN/uiflow/m5burner/publish

## 文件与验证

`SHA256SUMS.txt` 包含校验值。镜像来自干净构建，空白NVS为0xFF；不含设备转储、个人录音、存档、凭据或宣传配乐。代码和测试见本仓库。安装器源码及GPLv2许可证在installer目录，运行库许可附在安装包内。

已完成十套主机测试、固件编译、合并镜像分段/空白NVS验证及打包安装器离线自检。**本安装器尚未在目标设备上执行真实刷写**，当前设备固件没有改变。商场正式上架前应在测试设备完成安装、冷启动、键盘、SD与音频检查。
