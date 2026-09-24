#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <dwmapi.h>
#include <string>
#include <cstring>
#include <cwchar>
#include <utility>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")

namespace {
constexpr int kInitialWidth = 185;
int width = kInitialWidth;
constexpr int kHeight = 32;
constexpr UINT_PTR kTimer = 1;
constexpr UINT_PTR kVisibilityTimer = 2;
constexpr UINT kRefreshMs = 3000;
constexpr UINT kToggleWarning = 100;
constexpr UINT kExit = 101;
constexpr UINT kAutoStart = 102;
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunName[] = L"WlanIpWidget";
constexpr wchar_t kSettings[] = L"Software\\WlanIpWidget";
bool showWarning = false;
bool autoStart = false;
bool menuOpen = false;
HWND desktopView = nullptr;
std::wstring currentIp;
HFONT font = nullptr;

bool AutoStartEnabled() {
    wchar_t command[32768] = {};
    DWORD bytes = sizeof(command);
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunName, RRF_RT_REG_SZ,
                        nullptr, command, &bytes) == ERROR_SUCCESS;
}

LONG SetAutoStart(bool enabled) {
    if (!enabled)
        return RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunName);
    std::wstring path(32768, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
        return ERROR_FILENAME_EXCED_RANGE;
    path.resize(length);
    std::wstring command = L"\"" + path + L"\"";
    return RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunName, REG_SZ,
                           command.c_str(), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
}

BOOL CALLBACK FindDesktopView(HWND top, LPARAM data) {
    HWND view = FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr);
    if (view) {
        *reinterpret_cast<HWND*>(data) = view;
        return FALSE;
    }
    return TRUE;
}

bool DesktopVisibleAtWidget(HWND window) {
    RECT widget = {};
    GetWindowRect(window, &widget);
    HWND desktopRoot = desktopView && IsWindow(desktopView)
        ? GetAncestor(desktopView, GA_ROOT) : GetShellWindow();

    // Inspect windows from front to back. Only windows above the desktop
    // can cover the widget; menus and other shell surfaces are ignored.
    for (HWND candidate = GetTopWindow(nullptr); candidate;
         candidate = GetWindow(candidate, GW_HWNDNEXT)) {
        if (candidate == desktopRoot) return true;
        if (candidate == window || !IsWindowVisible(candidate) || IsIconic(candidate))
            continue;
        wchar_t className[64] = {};
        GetClassNameW(candidate, className, 64);
        if (wcscmp(className, L"Progman") == 0 ||
            wcscmp(className, L"WorkerW") == 0 ||
            wcscmp(className, L"Shell_TrayWnd") == 0 ||
            wcscmp(className, L"Shell_SecondaryTrayWnd") == 0 ||
            wcscmp(className, L"#32768") == 0) continue;

        DWORD cloaked = 0;
        if (DwmGetWindowAttribute(candidate, DWMWA_CLOAKED,
                                  &cloaked, sizeof(cloaked)) == S_OK && cloaked)
            continue;
        RECT bounds = {};
        RECT overlap = {};
        if (GetWindowRect(candidate, &bounds) &&
            IntersectRect(&overlap, &widget, &bounds))
            return false;
    }
    return true;
}

void UpdateVisibility(HWND window) {
    if (menuOpen) return;
    bool visible = DesktopVisibleAtWidget(window);
    if (visible != (IsWindowVisible(window) != FALSE))
        ShowWindow(window, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
}

std::wstring Label() {
    std::wstring label = currentIp.empty() ? L"未连接" : currentIp;
    if (showWarning) label += L"  严禁处理涉密信息";
    return label;
}

void ResizeToContent(HWND window) {
    if (!font) return;
    std::wstring label = Label();
    HDC dc = GetDC(window);
    HGDIOBJ previous = SelectObject(dc, font);
    SIZE size = {};
    GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &size);
    SelectObject(dc, previous);
    ReleaseDC(window, dc);
    width = size.cx + 20;
}


void PositionAtBottomRight(HWND window) {
    MONITORINFO info = { sizeof(info) };
    HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY);
    if (GetMonitorInfoW(monitor, &info)) {
        const RECT& work = info.rcWork;
        SetWindowPos(window, nullptr, work.right - width - 12,
            work.bottom - kHeight - 10, width, kHeight,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
}


std::wstring FindWirelessIpv4() {
    ULONG size = 0;
    constexpr ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                            GAA_FLAG_SKIP_DNS_SERVER;
    ULONG result = GetAdaptersAddresses(AF_INET, flags, nullptr, nullptr, &size);
    if (result != ERROR_BUFFER_OVERFLOW) return L"";
    for (int attempt = 0; attempt < 3; ++attempt) {
        std::vector<BYTE> buffer(size);
        auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        result = GetAdaptersAddresses(AF_INET, flags, nullptr, adapters, &size);
        if (result == ERROR_BUFFER_OVERFLOW) continue;
        if (result != NO_ERROR) return L"";
        for (auto* adapter = adapters; adapter; adapter = adapter->Next) {
            if (adapter->IfType != IF_TYPE_IEEE80211 || adapter->OperStatus != IfOperStatusUp)
                continue;
            for (auto* address = adapter->FirstUnicastAddress; address; address = address->Next) {
                if (!address->Address.lpSockaddr ||
                    address->Address.lpSockaddr->sa_family != AF_INET)
                    continue;
                auto* ipv4 = reinterpret_cast<sockaddr_in*>(address->Address.lpSockaddr);
                const BYTE* bytes = reinterpret_cast<const BYTE*>(&ipv4->sin_addr);
                if (bytes[0] == 169 && bytes[1] == 254) continue;
                if (bytes[0] == 127 || bytes[0] == 0) continue;
                wchar_t ip[INET_ADDRSTRLEN] = {};
                if (InetNtopW(AF_INET, &ipv4->sin_addr, ip, INET_ADDRSTRLEN))
                    return ip;
            }
        }
        return L"";
    }
    return L"";
}

void Refresh(HWND window) {
    auto next = FindWirelessIpv4();
    if (next != currentIp) {
        currentIp = std::move(next);
        ResizeToContent(window);
        PositionAtBottomRight(window);
        InvalidateRect(window, nullptr, TRUE);
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
                memory = nullptr; // Clipboard takes ownership.
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
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        Refresh(window);
        SetTimer(window, kTimer, kRefreshMs, nullptr);
        SetTimer(window, kVisibilityTimer, 200, nullptr);
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        PositionAtBottomRight(window);
        InvalidateRect(window, nullptr, TRUE);
        return 0;
    case WM_TIMER:
        if (wParam == kTimer) Refresh(window);
        if (wParam == kVisibilityTimer) UpdateVisibility(window);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        RECT area;
        GetClientRect(window, &area);
        HBRUSH brush = CreateSolidBrush(RGB(245, 246, 248));
        FillRect(dc, &area, brush);
        DeleteObject(brush);
        SetBkMode(dc, TRANSPARENT);
        if (font) SelectObject(dc, font);
        std::wstring label = Label();
        SetTextColor(dc, RGB(20, 20, 20));
        area.left += 10;
        DrawTextW(dc, label.c_str(), -1, &area, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_LBUTTONDBLCLK:
        CopyIp(window);
        return 0;
    case WM_CONTEXTMENU: {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING | (showWarning ? MF_CHECKED : MF_UNCHECKED),
                    kToggleWarning, L"显示严禁处理涉密信息");
        AppendMenuW(menu, MF_STRING | (autoStart ? MF_CHECKED : MF_UNCHECKED),
                    kAutoStart, L"开机自启动");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kExit, L"退出");
        POINT point;
        GetCursorPos(&point);
        menuOpen = true;
        SetForegroundWindow(window);
        UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                      point.x, point.y, 0, window, nullptr);
        DestroyMenu(menu);
        menuOpen = false;
        PostMessageW(window, WM_NULL, 0, 0);
        if (command == kToggleWarning) {
            showWarning = !showWarning;
            DWORD value = showWarning ? 1 : 0;
            RegSetKeyValueW(HKEY_CURRENT_USER, kSettings, L"ShowWarning",
                            REG_DWORD, &value, sizeof(value));
            ResizeToContent(window);
            PositionAtBottomRight(window);
            InvalidateRect(window, nullptr, TRUE);
        } else if (command == kAutoStart) {
            LONG error = SetAutoStart(!autoStart);
            if (error == ERROR_SUCCESS) autoStart = !autoStart;
            else MessageBoxW(window, L"设置开机自启动失败。", L"WLAN IP", MB_OK | MB_ICONERROR);
        } else if (command == kExit) DestroyWindow(window);
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
    HANDLE singleInstance = CreateMutexW(nullptr, TRUE, L"Local\\WlanIpWidget.SingleInstance");
    if (!singleInstance) {
        MessageBoxW(nullptr, L"单实例检查失败。", L"WLAN IP", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(singleInstance);
        return 0;
    }
    EnumWindows(FindDesktopView, reinterpret_cast<LPARAM>(&desktopView));
    autoStart = AutoStartEnabled();
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

    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE, className,
        L"WLAN IP", WS_POPUP, 0, 0, width, kHeight, nullptr, nullptr, instance, nullptr);
    if (!window) {
        DWORD error = GetLastError();
        std::wstring message = L"创建窗口失败，错误码：" + std::to_wstring(error);
        MessageBoxW(nullptr, message.c_str(), L"WLAN IP", MB_OK | MB_ICONERROR);
        return 1;
    }
    SetLayeredWindowAttributes(window, 0, 226, LWA_ALPHA);
    Refresh(window);
    ResizeToContent(window);
    PositionAtBottomRight(window);
    UpdateVisibility(window);
    UpdateWindow(window);

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CloseHandle(singleInstance);
    return static_cast<int>(message.wParam);
}
