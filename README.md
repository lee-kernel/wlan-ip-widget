# WLAN IP 悬浮窗

原生 Win32 小工具，显示当前已连接无线网卡的 IPv4 地址，每 3 秒刷新；断开显示“未连接”。窗口固定在屏幕右下角、任务栏上方，始终置顶，使用浅色半透明背景和黑色文字。双击复制 IP，右键退出。

## 下载

打开 [Actions](https://github.com/lee-kernel/wlan-ip-widget/actions/workflows/build.yml)，选择最新成功的运行，在 Artifacts 中下载 `WlanIp-win-x64`，解压并运行 `WlanIp.exe`。不需要安装 .NET、Python 或 Visual C++ 运行库。

此版本面向 Windows x64，使用 Windows 系统 API 读取无线网卡 IPv4。没有代码签名，Windows 首次运行可能提示来源未知。

## 构建

GitHub Actions 在 Windows Runner 上使用 MSVC 编译 `WlanIp.cpp`，并上传可执行文件。
