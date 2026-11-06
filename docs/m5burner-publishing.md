# M5Burner发布准备

核对日期：2026-11-06。官方教程：[固件发布](https://docs.m5stack.com/zh_CN/uiflow/m5burner/publish)、[固件导出](https://docs.m5stack.com/zh_CN/uiflow/m5burner/export)。

## 官方桌面流程

1. 启动M5Burner，用M5Stack社区账号登录。
2. 左下角USER CUSTOM → Publish。
3. 填写Name、Version、Description、Device Type、Github，选择Firmware二进制与Cover封面，Upload上传。
4. 上传后可用Detail编辑；Publish切换公开状态；Share取得分享码，其他人可通过Share Burn烧录。

官方也有网页版入口 https://burner.m5stack.com/ 。本文核实的是官方桌面教程；网页版的具体审核和分类流程尚未登录确认。

## Pocket Studio建议资料

- Name：Pocket Studio for Cardputer ADV
- Version：0.25
- Device Type：选择与Cardputer ADV匹配的类别。暂未验证普通Cardputer，不应宣称兼容。
- Github：https://github.com/drw41823/pocket-studio-cardputer-adv
- 简介：面向Cardputer ADV的口袋音乐工作站，包含三条四复音旋律轨、独立鼓组、实时与STEP编曲、调内和弦与琶音器、录音与WAV采样、双振荡器和LFO，以及WAV/MP3播放器。采样器、音色素材与播放器需要SD卡。
- Cover：使用不包含个人录音或存档信息的项目封面。
- 操作说明：链接本仓库docs/manual-v0.25.md。

## 二进制与隐私

PlatformIO的firmware.bin是写入应用地址0x10000的镜像。商场发布前必须确认平台所需的烧录地址/镜像格式；如需地址0的合并镜像，应由干净构建生成bootloader、分区表及应用后合并，不能直接将应用镜像冒充整片镜像。

官方提供Firmware Exporter，但从使用过的设备导出Flash可能携带NVS中的项目、设置或凭据；不要直接上传个人设备备份。本项目优先从干净源码构建发行镜像，不带个人SD素材，不把宣传视频中的歌曲片段当作默认示例。

GitHub仓库当前为私有时，其他用户无法阅读其内容；如要向公众提供源码和手册，需要另行决定仓库可见性。项目自有代码的发布许可证也需由作者决定，不能把第三方许可证直接套到全项目。

## 当前状态

尚未向M5Burner账号提交或公开任何固件。正式提交前应准备干净发行镜像，并用不带个人存档的测试环境验证冷启动、SD缺失提示、键盘、音频与保存读取。
