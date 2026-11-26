# TellyTile 方寸电视 · SenseCAP Indicator 开源桌面直播屏

[English](README.md) · [CodeCraft Hub](https://codecraft.seeed.cc/hub/application) · [CodeCraft AI Hardware](https://github.com/Seeed-Solution/codecraft-ai-hardware)

TellyTile 把 Seeed Studio SenseCAP Indicator 变成一台 4 英寸 Wi-Fi 桌面小电视。把它放在键盘旁，就能看直播、留意工作台摄像头，或循环播放产品演示。

![白色 SenseCAP Indicator 的方屏显示霓虹城市夜景，展示 TellyTile 桌面电视](assets/tellytile-cover-cyberpunk-city-en.png)

## 能做什么

- **全屏看直播。** GX 版本默认铺满方屏，轻点才出现频道、播放控件和状态。再点一下画面即可收起。
- **滑动切台。** 在直播、摄像头和演示视频之间切换，手不用离开桌面。
- **自己编频道。** 电脑上的视频服务通过 Wi-Fi 把画面送到 Indicator。想听声音，可以搭配 reSpeaker Lite。 [声音配件设置](audio/README.md)

## 怎么开始

准备一台 **SenseCAP Indicator**、USB 电源、供设备连接的 **2.4 GHz Wi-Fi**，以及同一局域网内用来运行视频服务的电脑。

1. **选对屏幕版本并烧录。** GX 版提供这里展示的全屏体验，DX 版沿用原来的布局。板型不确定时，先查屏幕版本。 [GX 固件](release-fullscreen-gx/firmware.bin) · [DX 固件](release-receive-deadline-dx/firmware.bin) · [板型与设置说明](DETAILS.md)
2. **在电脑上启动视频服务**，添加你想看的视频源，记下服务的局域网地址和配对码。 [视频服务启动说明](https://github.com/guolifu/ai-passport-tv/blob/30ba309d5fe77e86154de3ca1b81d457b65bc36f/README.zh_CN.md#3-一键启动-tv-服务)
3. **连接屏幕上显示的 `TV-SETUP` 热点**，打开[设备配网页](http://192.168.4.1)，填入 Wi-Fi、视频服务地址和配对码，保存重启。

GX 实机已验证全屏直播和轻点呼出控件。封面为效果图。 [GX 实测记录](evidence/indicator-gx-fullscreen.md)

## 探索 CodeCraft

想看屏幕、触摸是怎么驱动的，或想把 TellyTile 移植到别的硬件？从这个项目使用的板级源码和示例开始。 [CodeCraft AI Hardware 开源仓库](https://github.com/Seeed-Solution/codecraft-ai-hardware)

想在手头的设备上试更多项目？可以按硬件型号筛选。 [CodeCraft Hub 应用广场](https://codecraft.seeed.cc/hub/application)

[设置与开发说明](DETAILS.md) · [MIT 许可证](LICENSE) · [软件来源与第三方许可](UPSTREAM.md)

**标签**

`esp32` · `esp32-s3` · `sensecap-indicator` · `seeedstudio` · `desktop-tv` · `live-streaming` · `touchscreen` · `open-source-hardware` · `codecraft`
