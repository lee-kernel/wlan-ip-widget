# WLAN IP 悬浮窗

Windows 上显示当前已连接无线网卡的 IPv4 地址。每 3 秒刷新；断开显示“未连接”。窗口始终置顶，可拖动；双击复制 IP，右键退出。

## 下载

打开仓库的 [Actions](https://github.com/lee-kernel/wlan-ip-widget/actions/workflows/build.yml)，选择最新成功的运行，在 Artifacts 中下载 `WlanIp-win-x64`，解压并运行 `WlanIp.exe`。无需在本机安装 .NET。

此版本面向 Windows x64。发布为自包含单文件，未启用裁剪，以保证 WinForms 兼容性。没有代码签名，Windows 首次运行可能提示来源未知。

## 本地构建（可选）

安装 .NET 8 SDK 后执行：

```powershell
dotnet publish WlanIp.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -o publish
```
