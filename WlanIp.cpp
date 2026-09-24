#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

namespace {
constexpr int kHeight = 30;
constexpr int kPadding = 8;
constexpr UINT_PTR kTimer = 1;
constexpr UINT_PTR kVisibilityTimer = 2;
constexpr UINT kRefreshMs = 3000;
constexpr UINT kToggleWarning = 100;
constexpr UINT kExit = 101;
constexpr wchar_t kSettings[] = L"Software\\WlanIpWidget";
std::wstring currentIp;
bool showWarning = false;
int width = 130;
HFONT font = nullptr;
HWND desktopView = nullptr;

BOOL CALLBACK FindDesktopView(HWND top, LPARAM data) {
    HWND view = FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr);
    if (view) {
        *reinterpret_cast<HWND*>(data) = view;
        return FALSE;
    }
    return TRUE;
}

std::wstring FindWirelessIpv4() {
    ULONG size = 0;
    constexpr ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                            GAA_FLAG_SKIP_DNS_SERVER;
    if (GetAdaptersAddresses(AF_INET, flags, nullptr, nullptr, &size) != ERROR_BUFFER_OVERFLOW)
        return L"";
    for (int attempt = 0; attempt < 3; ++attempt) {
        std::vector<BYTE> buffer(size);
        auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        ULONG result = GetAdaptersAddresses(AF_INET, flags, nullptr, adapters, &size);
        if (result == ERROR_BUFFER_OVERFLOW) continue;
        if (result != NO_ERROR) return L"";
        for (auto* adapter = adapters; adapter; adapter = adapter->Next) {
            if (adapter->IfType != IF_TYPE_IEEE80211 || adapter->OperStatus != IfOperStatusUp)
                continue;
            for (auto* address = adapter->FirstUnicastAddress; address; address = address->Next) {
                if (!address->Address.lpSockaddr ||
                    address->Address.lpSockaddr->sa_family != AF_INET) continue;
                auto* ipv4 = reinterpret_cast<sockaddr_in*>(address->Address.lpSockaddr);
                const BYTE* bytes = reinterpret_cast<const BYTE*>(&ipv4->sin_addr);
                if ((bytes[0] == 169 && bytes[1] == 254) || bytes[0] == 127 || bytes[0] == 0)
                    continue;
                wchar_t ip[INET_ADDRSTRLEN] = {};
                if (InetNtopW(AF_INET, &ipv4->sin_addr, ip, INET_ADDRSTRLEN))
                    return ip;
            }
        }
        return L"";
    }
    return L"";
}

std::wstring Label() {
    std::wstring label = currentIp.empty() ? L"未连接" : currentIp;
    if (showWarning) label += L"  严禁处理涉密信息";
    return label;
}

bool DesktopIsForeground() {
    HWND foreground = GetForegroundWindow();
    if (!foreground) return false;
    HWND top = GetAncestor(foreground, GA_ROOT);
    HWND shell = GetShellWindow();
    if (top == shell) return true;
    return desktopView && top == GetAncestor(desktopView, GA_ROOT);
}

void UpdateVisibility(HWND window) {
    bool visible = DesktopIsForeground();
    if (visible != (IsWindowVisible(window) != FALSE))
        ShowWindow(window, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
}

void Position(HWND window) {
    MONITORINFO monitor = { sizeof(monitor) };
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &monitor))
        return;
    POINT location = { monitor.rcWork.right - width - 12, monitor.rcWork.bottom - kHeight - 10 };
    SetWindowPos(window, nullptr, location.x, location.y, width, kHeight,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void Draw(HWND window) {
    if (!font) return;
    std::wstring label = Label();
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HGDIOBJ previousFont = SelectObject(dc, font);
    SIZE measured = {};
    GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &measured);
    width = measured.cx + 2 * kPadding;
    SelectObject(dc, previousFont);
    DeleteDC(dc);

    Position(window);
    RECT bounds;
    GetWindowRect(window, &bounds);
    POINT destination = { bounds.left, bounds.top };
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO bitmap = {};
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = width;
    bitmap.bmiHeader.biHeight = -kHeight;
    bitmap.bmiHeader.biPlanes = 1;
    bitmap.bmiHeader.biBitCount = 32;
    bitmap.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP image = CreateDIBSection(screen, &bitmap, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (image && pixels) {
        HGDIOBJ oldImage = SelectObject(memory, image);
        RECT area = { 0, 0, width, kHeight };
        FillRect(memory, &area, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        HGDIOBJ oldFont = SelectObject(memory, font);
        SetBkMode(memory, TRANSPARENT);
        SetTextColor(memory, RGB(0, 0, 0));
        area.left = kPadding;
        DrawTextW(memory, label.c_str(), -1, &area, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
        GdiFlush();
        auto* bytes = static_cast<BYTE*>(pixels);
        for (int i = 0; i < width * kHeight; ++i) {
            BYTE opacity = static_cast<BYTE>(255 - std::min({ bytes[i * 4], bytes[i * 4 + 1], bytes[i * 4 + 2] }));
            bytes[i * 4] = bytes[i * 4 + 1] = bytes[i * 4 + 2] = 0;
            bytes[i * 4 + 3] = opacity;
        }
        POINT origin = { 0, 0 };
        SIZE size = { width, kHeight };
        BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        UpdateLayeredWindow(window, screen, &destination, &size, memory, &origin,
                            0, &blend, ULW_ALPHA);
        SelectObject(memory, oldFont);
        SelectObject(memory, oldImage);
        DeleteObject(image);
    }
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
}

void Refresh(HWND window) {
    auto next = FindWirelessIpv4();
    if (next != currentIp) {
        currentIp = std::move(next);
        Draw(window);
    }
}

void CopyIp(HWND window) {
    if (currentIp.empty() || !OpenClipboard(window)) return;
    EmptyClipboard();
    const SIZE_T bytes = (currentIp.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        void* destination = GlobalLock(memory);
        if (destination) {
            memcpy(destination, currentIp.c_str(), bytes);
            GlobalUnlock(memory);
            if (SetClipboardData(CF_UNICODETEXT, memory))
                memory = nullptr;
        }
        if (memory) GlobalFree(memory);
    }
    CloseClipboard();
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        SetTimer(window, kTimer, kRefreshMs, nullptr);
        SetTimer(window, kVisibilityTimer, 200, nullptr);
        return 0;
    case WM_TIMER:
        if (wParam == kTimer) {
            Refresh(window);
            Position(window);
        } else if (wParam == kVisibilityTimer) {
            UpdateVisibility(window);
        }
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        Position(window);
        return 0;
    case WM_LBUTTONDBLCLK:
        CopyIp(window);
        return 0;
    case WM_CONTEXTMENU: {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING | (showWarning ? MF_CHECKED : MF_UNCHECKED),
                    kToggleWarning, L"显示严禁处理涉密信息");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kExit, L"退出");
        POINT point;
        GetCursorPos(&point);
        UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                      point.x, point.y, 0, window, nullptr);
        DestroyMenu(menu);
        if (command == kToggleWarning) {
            showWarning = !showWarning;
            DWORD value = showWarning ? 1 : 0;
            RegSetKeyValueW(HKEY_CURRENT_USER, kSettings, L"ShowWarning",
                            REG_DWORD, &value, sizeof(value));
            Draw(window);
        } else if (command == kExit) {
            DestroyWindow(window);
        }
        return 0;
    }
    case WM_DESTROY:
        KillTimer(window, kTimer);
        KillTimer(window, kVisibilityTimer);
        if (font) DeleteObject(font);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    EnumWindows(FindDesktopView, reinterpret_cast<LPARAM>(&desktopView));
    DWORD saved = 0;
    DWORD bytes = sizeof(saved);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettings, L"ShowWarning", RRF_RT_REG_DWORD,
                     nullptr, &saved, &bytes) == ERROR_SUCCESS)
        showWarning = saved != 0;

    const wchar_t className[] = L"WlanIpWidgetWindow";
    WNDCLASSW cls = {};
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = instance;
    cls.lpszClassName = className;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.style = CS_DBLCLKS;
    if (!RegisterClassW(&cls)) {
        MessageBoxW(nullptr, L"注册窗口失败。", L"WLAN IP", MB_OK | MB_ICONERROR);
        return 1;
    }
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        className, L"WLAN IP", WS_POPUP, 0, 0, width, kHeight,
        nullptr, nullptr, instance, nullptr);
    if (!window) {
        DWORD error = GetLastError();
        std::wstring message = L"创建窗口失败，错误码：" + std::to_wstring(error);
        MessageBoxW(nullptr, message.c_str(), L"WLAN IP", MB_OK | MB_ICONERROR);
        return 1;
    }
    Refresh(window);
    Draw(window);
    UpdateVisibility(window);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
