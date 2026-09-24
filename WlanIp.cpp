#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <string>
#include <cstring>
#include <utility>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "advapi32.lib")

namespace {
constexpr int kWidth = 205;
constexpr int kHeight = 38;
constexpr UINT_PTR kTimer = 1;
constexpr UINT kRefreshMs = 3000;
constexpr UINT kExit = 100;
std::wstring currentIp;
HFONT font = nullptr;

bool UseLightTheme() {
    DWORD value = 0;
    DWORD size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS
        ? value != 0 : true;
}

void PositionAtBottomRight(HWND window) {
    MONITORINFO info = { sizeof(info) };
    HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY);
    if (GetMonitorInfoW(monitor, &info)) {
        const RECT& work = info.rcWork;
        SetWindowPos(window, HWND_TOPMOST, work.right - kWidth - 16,
            work.bottom - kHeight - 12, 0, 0,
            SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
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
        font = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        Refresh(window);
        SetTimer(window, kTimer, kRefreshMs, nullptr);
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        PositionAtBottomRight(window);
        InvalidateRect(window, nullptr, TRUE);
        return 0;
    case WM_TIMER:
        if (wParam == kTimer) Refresh(window);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        RECT area;
        GetClientRect(window, &area);
        const bool light = UseLightTheme();
        HBRUSH brush = CreateSolidBrush(light ? RGB(241, 243, 245) : RGB(45, 48, 54));
        FillRect(dc, &area, brush);
        DeleteObject(brush);
        SetBkMode(dc, TRANSPARENT);
        if (font) SelectObject(dc, font);
        std::wstring label = currentIp.empty() ? L"● 未连接" : L"● " + currentIp;
        SetTextColor(dc, currentIp.empty()
            ? (light ? RGB(107, 114, 128) : RGB(161, 161, 170))
            : (light ? RGB(31, 92, 68) : RGB(165, 235, 194)));
        area.left += 14;
        DrawTextW(dc, label.c_str(), -1, &area, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_LBUTTONDBLCLK:
        CopyIp(window);
        return 0;
    case WM_CONTEXTMENU: {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, kExit, L"退出");
        POINT point;
        GetCursorPos(&point);
        SetForegroundWindow(window);
        UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                      point.x, point.y, 0, window, nullptr);
        DestroyMenu(menu);
        if (command == kExit) DestroyWindow(window);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(window, kTimer);
        if (font) DeleteObject(font);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    const wchar_t className[] = L"WlanIpWidgetWindow";
    WNDCLASSW cls = {};
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = instance;
    cls.lpszClassName = className;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.style = CS_DBLCLKS;
    if (!RegisterClassW(&cls)) return 1;

    HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, className,
        L"WLAN IP", WS_POPUP, 0, 0, kWidth, kHeight, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    PositionAtBottomRight(window);
    UpdateWindow(window);

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
