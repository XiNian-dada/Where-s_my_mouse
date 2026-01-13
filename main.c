#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <dwmapi.h>
#include <commdlg.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdbool.h>
#include <wchar.h>

// --- 常量与ID ---
#define IDC_BTN_COLOR 101
#define IDC_SLIDER_RADIUS 102
#define IDC_SLIDER_OPACITY 103
#define IDC_CHECK_DOT 104
#define IDC_LABEL_RADIUS 105
#define IDC_LABEL_OPACITY 106
#define IDC_CHECK_VSYNC 107
#define IDC_SLIDER_FPS 108
#define IDC_LABEL_FPS 109
#define IDC_STATUS_BAR 110

// --- 全局配置 ---
typedef struct {
    int radius;         // 半径
    int opacity;        // 不透明度
    COLORREF color;     // 颜色
    int show_dot;       // 中心点
    int vsync;          // 垂直同步 (1=开启)
    int target_fps;     // 目标帧率 (30-144)
} Config;

Config g_conf;
HWND g_hOverlay = NULL;
HWND g_hSettings = NULL;
HBRUSH g_hBrushBg = NULL;
HBRUSH g_hBrushDot = NULL;
HFONT g_hFont = NULL; // 界面字体
bool g_overlayVisible = true;
wchar_t g_iniPath[MAX_PATH];

// --- 性能统计 ---
double g_currentFps = 0.0;
LARGE_INTEGER g_frequency;

// --- 辅助函数 ---

// 1. 设置更现代的字体 (微软雅黑)
void SetModernFont(HWND hwndChild) {
    if (!g_hFont) {
        // 创建系统界面字体
        NONCLIENTMETRICSW ncm = {sizeof(NONCLIENTMETRICSW)};
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICSW), &ncm, 0);
        g_hFont = CreateFontIndirectW(&ncm.lfMessageFont);
    }
    SendMessage(hwndChild, WM_SETFONT, (WPARAM)g_hFont, TRUE);
}

// 2. 宽字符 INI 读写
void LoadConfig() {
    GetCurrentDirectoryW(MAX_PATH, g_iniPath);
    wcscat(g_iniPath, L"\\mouse_highlighter.ini");

    g_conf.radius = GetPrivateProfileIntW(L"Settings", L"Radius", 30, g_iniPath);
    g_conf.opacity = GetPrivateProfileIntW(L"Settings", L"Opacity", 100, g_iniPath);
    g_conf.show_dot = GetPrivateProfileIntW(L"Settings", L"ShowDot", 1, g_iniPath);
    g_conf.vsync = GetPrivateProfileIntW(L"Settings", L"VSync", 1, g_iniPath);
    g_conf.target_fps = GetPrivateProfileIntW(L"Settings", L"TargetFPS", 60, g_iniPath);
    
    int r = GetPrivateProfileIntW(L"Settings", L"ColorR", 255, g_iniPath);
    int g = GetPrivateProfileIntW(L"Settings", L"ColorG", 255, g_iniPath);
    int b = GetPrivateProfileIntW(L"Settings", L"ColorB", 0, g_iniPath);
    g_conf.color = RGB(r, g, b);
}

void SaveConfig() {
    wchar_t buf[32];
    _itow(g_conf.radius, buf, 10); WritePrivateProfileStringW(L"Settings", L"Radius", buf, g_iniPath);
    _itow(g_conf.opacity, buf, 10); WritePrivateProfileStringW(L"Settings", L"Opacity", buf, g_iniPath);
    _itow(g_conf.show_dot, buf, 10); WritePrivateProfileStringW(L"Settings", L"ShowDot", buf, g_iniPath);
    _itow(g_conf.vsync, buf, 10); WritePrivateProfileStringW(L"Settings", L"VSync", buf, g_iniPath);
    _itow(g_conf.target_fps, buf, 10); WritePrivateProfileStringW(L"Settings", L"TargetFPS", buf, g_iniPath);
    
    _itow(GetRValue(g_conf.color), buf, 10); WritePrivateProfileStringW(L"Settings", L"ColorR", buf, g_iniPath);
    _itow(GetGValue(g_conf.color), buf, 10); WritePrivateProfileStringW(L"Settings", L"ColorG", buf, g_iniPath);
    _itow(GetBValue(g_conf.color), buf, 10); WritePrivateProfileStringW(L"Settings", L"ColorB", buf, g_iniPath);
}

void UpdateOverlayStyle() {
    if (!g_hOverlay) return;
    if (g_hBrushBg) DeleteObject(g_hBrushBg);
    g_hBrushBg = CreateSolidBrush(g_conf.color);

    int d = g_conf.radius * 2;
    HRGN hRgn = CreateEllipticRgn(0, 0, d, d);
    SetWindowRgn(g_hOverlay, hRgn, TRUE);
    SetWindowPos(g_hOverlay, HWND_TOPMOST, 0, 0, d, d, SWP_NOMOVE | SWP_NOACTIVATE);
    SetLayeredWindowAttributes(g_hOverlay, 0, (BYTE)g_conf.opacity, LWA_ALPHA);
    InvalidateRect(g_hOverlay, NULL, TRUE);
}

// --- 窗口过程 ---
LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rect;
        GetClientRect(hwnd, &rect);
        FillRect(hdc, &rect, g_hBrushBg);
        if (g_conf.show_dot) {
            HGDIOBJ oldBrush = SelectObject(hdc, g_hBrushDot);
            HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
            int cx = g_conf.radius, cy = g_conf.radius;
            Ellipse(hdc, cx - 3, cy - 3, cx + 3, cy + 3);
            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBrush);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hSliderRadius, hSliderOpacity, hBtnColor, hCheckDot, hLblRadius, hLblOpacity;
    static HWND hCheckVsync, hSliderFps, hLblFps, hStatus;
    wchar_t buf[64];

    switch (msg) {
        case WM_CREATE: {
            int y = 10;
            // 样式设置组
            SetModernFont(CreateWindowW(L"BUTTON", L"外观设置", WS_VISIBLE | WS_CHILD | BS_GROUPBOX, 10, y, 260, 170, hwnd, NULL, NULL, NULL));
            
            y += 25;
            SetModernFont(CreateWindowW(L"STATIC", L"大小 (Radius):", WS_VISIBLE | WS_CHILD, 20, y, 100, 20, hwnd, NULL, NULL, NULL));
            hSliderRadius = CreateWindowW(TRACKBAR_CLASSW, NULL, WS_VISIBLE | WS_CHILD | TBS_AUTOTICKS, 20, y + 20, 180, 30, hwnd, (HMENU)IDC_SLIDER_RADIUS, NULL, NULL);
            SendMessage(hSliderRadius, TBM_SETRANGE, TRUE, MAKELPARAM(10, 150));
            SendMessage(hSliderRadius, TBM_SETPOS, TRUE, g_conf.radius);
            hLblRadius = CreateWindowW(L"STATIC", L"", WS_VISIBLE | WS_CHILD, 210, y + 20, 50, 20, hwnd, (HMENU)IDC_LABEL_RADIUS, NULL, NULL);
            SetModernFont(hLblRadius);

            y += 60;
            SetModernFont(CreateWindowW(L"STATIC", L"不透明度 (Opacity):", WS_VISIBLE | WS_CHILD, 20, y, 150, 20, hwnd, NULL, NULL, NULL));
            hSliderOpacity = CreateWindowW(TRACKBAR_CLASSW, NULL, WS_VISIBLE | WS_CHILD | TBS_AUTOTICKS, 20, y + 20, 180, 30, hwnd, (HMENU)IDC_SLIDER_OPACITY, NULL, NULL);
            SendMessage(hSliderOpacity, TBM_SETRANGE, TRUE, MAKELPARAM(20, 255));
            SendMessage(hSliderOpacity, TBM_SETPOS, TRUE, g_conf.opacity);
            hLblOpacity = CreateWindowW(L"STATIC", L"", WS_VISIBLE | WS_CHILD, 210, y + 20, 50, 20, hwnd, (HMENU)IDC_LABEL_OPACITY, NULL, NULL);
            SetModernFont(hLblOpacity);

            y += 50;
            hBtnColor = CreateWindowW(L"BUTTON", L"更改颜色...", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 20, y, 100, 25, hwnd, (HMENU)IDC_BTN_COLOR, NULL, NULL);
            SetModernFont(hBtnColor);
            
            hCheckDot = CreateWindowW(L"BUTTON", L"显示中心红点", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 130, y+3, 120, 20, hwnd, (HMENU)IDC_CHECK_DOT, NULL, NULL);
            SetModernFont(hCheckDot);
            if (g_conf.show_dot) SendMessage(hCheckDot, BM_SETCHECK, BST_CHECKED, 0);

            // 性能设置组
            y += 40;
            SetModernFont(CreateWindowW(L"BUTTON", L"性能与帧率", WS_VISIBLE | WS_CHILD | BS_GROUPBOX, 10, y, 260, 100, hwnd, NULL, NULL, NULL));
            
            y += 25;
            hCheckVsync = CreateWindowW(L"BUTTON", L"开启垂直同步 (VSync, 推荐)", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 20, y, 230, 20, hwnd, (HMENU)IDC_CHECK_VSYNC, NULL, NULL);
            SetModernFont(hCheckVsync);
            if (g_conf.vsync) SendMessage(hCheckVsync, BM_SETCHECK, BST_CHECKED, 0);

            y += 30;
            SetModernFont(CreateWindowW(L"STATIC", L"目标帧率 (关闭VSync时):", WS_VISIBLE | WS_CHILD, 20, y, 200, 20, hwnd, NULL, NULL, NULL));
            y += 20;
            hSliderFps = CreateWindowW(TRACKBAR_CLASSW, NULL, WS_VISIBLE | WS_CHILD | TBS_AUTOTICKS, 20, y, 180, 30, hwnd, (HMENU)IDC_SLIDER_FPS, NULL, NULL);
            SendMessage(hSliderFps, TBM_SETRANGE, TRUE, MAKELPARAM(30, 200));
            SendMessage(hSliderFps, TBM_SETPOS, TRUE, g_conf.target_fps);
            hLblFps = CreateWindowW(L"STATIC", L"", WS_VISIBLE | WS_CHILD, 210, y, 50, 20, hwnd, (HMENU)IDC_LABEL_FPS, NULL, NULL);
            SetModernFont(hLblFps);

            // 底部状态栏
            hStatus = CreateWindowW(L"STATIC", L"初始化...", WS_VISIBLE | WS_CHILD | SS_SUNKEN, 0, 320, 300, 20, hwnd, (HMENU)IDC_STATUS_BAR, NULL, NULL);
            SetModernFont(hStatus);

            // 触发一次UI刷新
            SendMessage(hwnd, WM_HSCROLL, 0, (LPARAM)hSliderRadius);
            SendMessage(hwnd, WM_HSCROLL, 0, (LPARAM)hSliderOpacity);
            SendMessage(hwnd, WM_HSCROLL, 0, (LPARAM)hSliderFps);
            break;
        }

        case WM_HSCROLL: {
            // UI 更新逻辑
            if ((HWND)lParam == hSliderRadius) {
                g_conf.radius = SendMessage(hSliderRadius, TBM_GETPOS, 0, 0);
                swprintf(buf, 64, L"%d px", g_conf.radius); SetWindowTextW(hLblRadius, buf);
                UpdateOverlayStyle();
            } else if ((HWND)lParam == hSliderOpacity) {
                g_conf.opacity = SendMessage(hSliderOpacity, TBM_GETPOS, 0, 0);
                swprintf(buf, 64, L"%d%%", (int)(g_conf.opacity * 100 / 255)); SetWindowTextW(hLblOpacity, buf);
                UpdateOverlayStyle();
            } else if ((HWND)lParam == hSliderFps) {
                g_conf.target_fps = SendMessage(hSliderFps, TBM_GETPOS, 0, 0);
                swprintf(buf, 64, L"%d FPS", g_conf.target_fps); SetWindowTextW(hLblFps, buf);
                // 如果开启了 VSync，禁用 FPS 滑动条
                BOOL vsync = (SendMessage(hCheckVsync, BM_GETCHECK, 0, 0) == BST_CHECKED);
                EnableWindow(hSliderFps, !vsync);
            }
            SaveConfig();
            break;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDC_BTN_COLOR) {
                CHOOSECOLORW cc = {sizeof(cc)};
                static COLORREF customColors[16];
                cc.hwndOwner = hwnd;
                cc.lpCustColors = customColors;
                cc.rgbResult = g_conf.color;
                cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                if (ChooseColorW(&cc)) {
                    g_conf.color = cc.rgbResult;
                    UpdateOverlayStyle();
                    SaveConfig();
                }
            } else if (id == IDC_CHECK_DOT) {
                g_conf.show_dot = (SendMessage(hCheckDot, BM_GETCHECK, 0, 0) == BST_CHECKED);
                InvalidateRect(g_hOverlay, NULL, TRUE);
                SaveConfig();
            } else if (id == IDC_CHECK_VSYNC) {
                g_conf.vsync = (SendMessage(hCheckVsync, BM_GETCHECK, 0, 0) == BST_CHECKED);
                // 刷新 FPS 滑动条状态
                SendMessage(hwnd, WM_HSCROLL, 0, (LPARAM)hSliderFps);
                SaveConfig();
            }
            break;
        }
        
        // 接收主循环发来的 FPS 更新消息
        case WM_USER + 1: {
            swprintf(buf, 64, L" 实时性能: %.1f FPS | 快捷键: Ctrl + F1", g_currentFps);
            SetWindowTextW(hStatus, buf);
            break;
        }

        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// wWinMain 是 Unicode 程序的入口
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    SetProcessDPIAware();
    LoadConfig();
    
    // 高精度计时器频率
    QueryPerformanceFrequency(&g_frequency);
    
    INITCOMMONCONTROLSEX icex = {sizeof(icex), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icex);

    WNDCLASSW wcOverlay = {0};
    wcOverlay.lpfnWndProc = OverlayWndProc;
    wcOverlay.hInstance = hInstance;
    wcOverlay.lpszClassName = L"OverlayClass";
    wcOverlay.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wcOverlay);

    WNDCLASSW wcSettings = {0};
    wcSettings.lpfnWndProc = SettingsWndProc;
    wcSettings.hInstance = hInstance;
    wcSettings.lpszClassName = L"SettingsClass";
    wcSettings.hbrBackground = (HBRUSH)(COLOR_WINDOW); // 使用系统颜色
    wcSettings.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wcSettings);

    int d = g_conf.radius * 2;
    g_hOverlay = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        L"OverlayClass", L"Highlight", WS_POPUP, 0, 0, d, d, NULL, NULL, hInstance, NULL
    );
    g_hBrushDot = CreateSolidBrush(RGB(255, 0, 0));
    UpdateOverlayStyle();
    ShowWindow(g_hOverlay, SW_SHOWNOACTIVATE);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    g_hSettings = CreateWindowW(
        L"SettingsClass", L"高亮设置 (Mouse Highlighter)", 
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 
        (screenW - 300) / 2, (screenH - 350) / 2, 300, 380, 
        NULL, NULL, hInstance, NULL
    );

    SetProcessWorkingSetSize(GetCurrentProcess(), -1, -1);

    MSG msg = {0};
    bool lastCtrlState = false;
    bool lastSettingsKeyState = false;
    
    // FPS 计算变量
    LARGE_INTEGER lastTime, currentTime;
    QueryPerformanceCounter(&lastTime);
    int frameCount = 0;
    double timeAccumulator = 0.0;

    // --- 主循环 ---
    while (true) {
        // 处理消息队列
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return 0;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // 1. 快捷键逻辑
        bool currentCtrlState = (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0;
        if (currentCtrlState && !lastCtrlState) {
            g_overlayVisible = !g_overlayVisible;
            if (g_overlayVisible) {
                ShowWindow(g_hOverlay, SW_SHOWNOACTIVATE);
                // 唤醒时瞬移
                POINT pt; GetCursorPos(&pt);
                SetWindowPos(g_hOverlay, HWND_TOPMOST, pt.x - g_conf.radius, pt.y - g_conf.radius, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOOWNERZORDER);
            } else {
                ShowWindow(g_hOverlay, SW_HIDE);
                SetProcessWorkingSetSize(GetCurrentProcess(), -1, -1);
            }
        }
        lastCtrlState = currentCtrlState;

        bool ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool f1Down = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
        if (ctrlDown && f1Down && !lastSettingsKeyState) {
            if (IsWindowVisible(g_hSettings)) ShowWindow(g_hSettings, SW_HIDE);
            else {
                ShowWindow(g_hSettings, SW_SHOWNORMAL);
                SetForegroundWindow(g_hSettings);
            }
        }
        lastSettingsKeyState = (ctrlDown && f1Down);

        // 2. 窗口跟随与渲染
        if (g_overlayVisible) {
            POINT pt;
            GetCursorPos(&pt);
            SetWindowPos(g_hOverlay, HWND_TOPMOST, 
                         pt.x - g_conf.radius, pt.y - g_conf.radius, 
                         0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOOWNERZORDER);
        }

        // 3. 帧率控制 (VSync 或 手动限帧)
        if (g_conf.vsync) {
            DwmFlush(); // 垂直同步，由系统决定等待时间
        } else {
            // 手动限帧 (Spin-wait 或者 Sleep)
            double targetFrameTime = 1.0 / (double)g_conf.target_fps;
            LARGE_INTEGER now;
            do {
                QueryPerformanceCounter(&now);
                double elapsed = (double)(now.QuadPart - lastTime.QuadPart) / (double)g_frequency.QuadPart;
                if (elapsed >= targetFrameTime) break;
                // 如果剩余时间较多，可以短暂 Sleep 节省 CPU，否则忙等待以保证精度
                if (targetFrameTime - elapsed > 0.002) Sleep(1); 
            } while (true);
        }

        // 4. 计算真实 FPS
        QueryPerformanceCounter(&currentTime);
        double dt = (double)(currentTime.QuadPart - lastTime.QuadPart) / (double)g_frequency.QuadPart;
        lastTime = currentTime;
        
        timeAccumulator += dt;
        frameCount++;
        if (timeAccumulator >= 0.5) { // 每0.5秒更新一次显示
            g_currentFps = frameCount / timeAccumulator;
            frameCount = 0;
            timeAccumulator = 0.0;
            // 发送更新消息给设置窗口
            if (IsWindowVisible(g_hSettings)) {
                SendMessage(g_hSettings, WM_USER + 1, 0, 0);
            }
        }
    }
    return 0;
}