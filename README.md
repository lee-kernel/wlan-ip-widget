# WLAN IP 悬浮窗

原生 Win32 小工具，显示当前已连接无线网卡的 IPv4 地址。每 3 秒刷新；断开显示“未连接”。文字固定在桌面右下角、任务栏上方，不覆盖其他应用。窗口宽度按内容自适应，文字为黑色、背景完全透明。双击 IP 复制地址；右键可切换同一行的“严禁处理涉密信息”（设置会保存），也可退出。

## 下载

打开 [Actions](https://github.com/lee-kernel/wlan-ip-widget/actions/workflows/build.yml)，选择最新成功的运行，在 Artifacts 中下载 `WlanIp-win-x64`，解压并运行 `WlanIp.exe`。不需要安装 .NET、Python 或 Visual C++ 运行库。

此版本面向 Windows x64。使用 Windows 系统 API 读取网络适配器，编译为原生程序；没有代码签名，Windows 首次运行可能提示来源未知。

## 构建

GitHub Actions 在 Windows Runner 上使用 MSVC 编译 `WlanIp.cpp`，并上传可执行文件。源码不依赖第三方库。
