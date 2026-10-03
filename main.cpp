#define UNICODE
#define _UNICODE

#include <windows.h>
#include <shellapi.h>

#include <string>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

// ============================================================
// Configuration
// ============================================================

constexpr UINT WM_TRAYICON = WM_USER + 1;
constexpr UINT_PTR TIMER_ID = 1;

// Update every 10 seconds.
constexpr UINT UPDATE_INTERVAL = 10000;

// Menu commands.
constexpr UINT ID_REFRESH = 1001;
constexpr UINT ID_EXIT    = 1002;


// ============================================================
// Globals
// ============================================================

HWND g_hwnd = nullptr;
HICON g_icon = nullptr;
NOTIFYICONDATAW g_tray{};


// ============================================================
// Get free space on C:
// ============================================================

unsigned long long GetDiskFreeSpaceBytes()
{
    ULARGE_INTEGER freeBytes{};
    ULARGE_INTEGER totalBytes{};
    ULARGE_INTEGER totalFreeBytes{};

    if (!GetDiskFreeSpaceExW(
        L"C:\\",
        &freeBytes,
        &totalBytes,
        &totalFreeBytes))
    {
        return 0;
    }

    return totalFreeBytes.QuadPart;
}


// ============================================================
// Determine icon size based on Windows DPI
// ============================================================

int GetIconSize(HWND hwnd)
{
    UINT dpi = 96;

    // GetDpiForWindow is available on modern Windows 10/11.
    if (hwnd)
    {
        dpi = GetDpiForWindow(hwnd);
    }

    // System tray icons are normally approximately 16px at
    // 100% scaling. Scale appropriately for high-DPI displays.
    int size = MulDiv(16, dpi, 96);

    // Keep the generated bitmap within sensible limits.
    if (size < 16)
        size = 16;

    if (size > 64)
        size = 64;

    return size;
}


// ============================================================
// Create the tray icon
// ============================================================

HICON CreateDiskIcon(
    unsigned long long freeBytes,
    int size)
{
    constexpr unsigned long long MB =
        1024ULL * 1024ULL;

    constexpr unsigned long long GB =
        1024ULL * 1024ULL * 1024ULL;

    // --------------------------------------------------------
    // Work out the text
    // --------------------------------------------------------

    std::wstring text;

    double freeGB =
        static_cast<double>(freeBytes) /
        static_cast<double>(GB);

    if (freeBytes < GB)
    {
        unsigned long long freeMB =
            freeBytes / MB;

        text =
            std::to_wstring(freeMB) + L"M";
    }
    else
    {
        std::wstringstream ss;

        // Use one decimal place below 10 GB.
        if (freeGB < 10.0)
        {
            ss << std::fixed
               << std::setprecision(1)
               << freeGB;
        }
        else
        {
            ss << std::fixed
               << std::setprecision(0)
               << freeGB;
        }

        text = ss.str() + L"G";
    }


    // --------------------------------------------------------
    // Select color
    // --------------------------------------------------------

    COLORREF background;

    if (freeGB < 5.0)
    {
        // Critical
        background = RGB(210, 55, 55);
    }
    else if (freeGB < 20.0)
    {
        // Warning
        background = RGB(210, 165, 45);
    }
    else
    {
        // Normal
        background = RGB(45, 155, 85);
    }


    // --------------------------------------------------------
    // Create drawing surface
    // --------------------------------------------------------

    HDC screenDC =
        GetDC(nullptr);

    HDC dc =
        CreateCompatibleDC(screenDC);

    HBITMAP bitmap =
        CreateCompatibleBitmap(
            screenDC,
            size,
            size
        );

    HGDIOBJ oldBitmap =
        SelectObject(dc, bitmap);


    // --------------------------------------------------------
    // Background
    // --------------------------------------------------------

    HBRUSH brush =
        CreateSolidBrush(background);

    RECT rect{
        0,
        0,
        size,
        size
    };

    FillRect(
        dc,
        &rect,
        brush
    );

    DeleteObject(brush);


    // --------------------------------------------------------
    // Draw text
    // --------------------------------------------------------

    int fontHeight =
        static_cast<int>(size * 0.43);

    if (fontHeight < 8)
        fontHeight = 8;

    HFONT font =
        CreateFontW(
            fontHeight,
            0,
            0,
            0,
            FW_BOLD,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_SWISS,
            L"Segoe UI"
        );

    HGDIOBJ oldFont =
        SelectObject(dc, font);

    SetBkMode(
        dc,
        TRANSPARENT
    );

    SetTextColor(
        dc,
        RGB(255, 255, 255)
    );

    DrawTextW(
        dc,
        text.c_str(),
        -1,
        &rect,
        DT_CENTER |
        DT_VCENTER |
        DT_SINGLELINE |
        DT_NOPREFIX
    );

    SelectObject(
        dc,
        oldFont
    );

    DeleteObject(font);


    // --------------------------------------------------------
    // Create mask
    // --------------------------------------------------------

    HBITMAP mask =
        CreateBitmap(
            size,
            size,
            1,
            1,
            nullptr
        );

    HDC maskDC =
        CreateCompatibleDC(screenDC);

    HGDIOBJ oldMask =
        SelectObject(maskDC, mask);

    PatBlt(
        maskDC,
        0,
        0,
        size,
        size,
        WHITENESS
    );

    SelectObject(
        maskDC,
        oldMask
    );

    DeleteDC(maskDC);


    // --------------------------------------------------------
    // Create Windows icon
    // --------------------------------------------------------

    ICONINFO iconInfo{};

    iconInfo.fIcon = TRUE;
    iconInfo.hbmColor = bitmap;
    iconInfo.hbmMask = mask;

    HICON icon =
        CreateIconIndirect(&iconInfo);


    // --------------------------------------------------------
    // Cleanup
    // --------------------------------------------------------

    SelectObject(
        dc,
        oldBitmap
    );

    DeleteObject(bitmap);
    DeleteObject(mask);

    DeleteDC(dc);

    ReleaseDC(
        nullptr,
        screenDC
    );

    return icon;
}


// ============================================================
// Update tray icon
// ============================================================

void UpdateTray()
{
    unsigned long long freeBytes =
        GetDiskFreeSpaceBytes();

    int iconSize =
        GetIconSize(g_hwnd);

    HICON newIcon =
        CreateDiskIcon(
            freeBytes,
            iconSize
        );

    if (!newIcon)
        return;


    // Remove old icon object.
    if (g_icon)
    {
        DestroyIcon(g_icon);
    }

    g_icon = newIcon;

    g_tray.hIcon =
        g_icon;


    // --------------------------------------------------------
    // Tooltip
    // --------------------------------------------------------

    constexpr unsigned long long MB =
        1024ULL * 1024ULL;

    constexpr unsigned long long GB =
        1024ULL * 1024ULL * 1024ULL;

    std::wstringstream tooltip;

    if (freeBytes >= GB)
    {
        double gb =
            static_cast<double>(freeBytes) /
            static_cast<double>(GB);

        tooltip << L"C: "
                << std::fixed
                << std::setprecision(1)
                << gb
                << L" GB free";
    }
    else
    {
        unsigned long long mb =
            freeBytes / MB;

        tooltip << L"C: "
                << mb
                << L" MB free";
    }

    wcsncpy_s(
        g_tray.szTip,
        tooltip.str().c_str(),
        _TRUNCATE
    );


    // --------------------------------------------------------
    // Tell Windows to redraw the tray icon.
    // --------------------------------------------------------

    Shell_NotifyIconW(
        NIM_MODIFY,
        &g_tray
    );
}


// ============================================================
// Context menu
// ============================================================

void ShowContextMenu()
{
    POINT point{};

    GetCursorPos(&point);

    HMENU menu =
        CreatePopupMenu();

    if (!menu)
        return;


    AppendMenuW(
        menu,
        MF_STRING,
        ID_REFRESH,
        L"Refresh"
    );

    AppendMenuW(
        menu,
        MF_SEPARATOR,
        0,
        nullptr
    );

    AppendMenuW(
        menu,
        MF_STRING,
        ID_EXIT,
        L"Exit"
    );


    // Required for correct tray-menu behaviour.
    SetForegroundWindow(g_hwnd);


    UINT command =
        TrackPopupMenu(
            menu,
            TPM_RIGHTBUTTON |
            TPM_RETURNCMD |
            TPM_NONOTIFY,
            point.x,
            point.y,
            0,
            g_hwnd,
            nullptr
        );


    DestroyMenu(menu);


    switch (command)
    {
    case ID_REFRESH:

        UpdateTray();

        break;

    case ID_EXIT:

        DestroyWindow(g_hwnd);

        break;
    }
}


// ============================================================
// Window procedure
// ============================================================

LRESULT CALLBACK WindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (message)
    {
    case WM_TRAYICON:

        if (lParam == WM_RBUTTONUP)
        {
            ShowContextMenu();
        }
        else if (lParam == WM_LBUTTONDBLCLK)
        {
            UpdateTray();
        }

        return 0;


    case WM_TIMER:

        if (wParam == TIMER_ID)
        {
            UpdateTray();
        }

        return 0;


    case WM_DPICHANGED:

        // Recreate icon at the new DPI.
        UpdateTray();

        return 0;


    case WM_DESTROY:

        KillTimer(
            hwnd,
            TIMER_ID
        );

        Shell_NotifyIconW(
            NIM_DELETE,
            &g_tray
        );

        if (g_icon)
        {
            DestroyIcon(g_icon);
            g_icon = nullptr;
        }

        PostQuitMessage(0);

        return 0;
    }


    return DefWindowProcW(
        hwnd,
        message,
        wParam,
        lParam
    );
}


// ============================================================
// Program entry point
// ============================================================

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int)
{
    // --------------------------------------------------------
    // Tell Windows that this application understands DPI.
    // --------------------------------------------------------

    SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    );


    // --------------------------------------------------------
    // Register window class
    // --------------------------------------------------------

    const wchar_t CLASS_NAME[] =
        L"DiskSpaceTrayMonitor";

    WNDCLASSW wc{};

    wc.lpfnWndProc =
        WindowProc;

    wc.hInstance =
        instance;

    wc.lpszClassName =
        CLASS_NAME;

    wc.hCursor =
        LoadCursorW(
            nullptr,
            IDC_ARROW
        );

    RegisterClassW(&wc);


    // --------------------------------------------------------
    // Create invisible message window
    // --------------------------------------------------------

    g_hwnd =
        CreateWindowExW(
            0,
            CLASS_NAME,
            L"C: Disk Space",
            0,
            0,
            0,
            0,
            0,
            HWND_MESSAGE,
            nullptr,
            instance,
            nullptr
        );

    if (!g_hwnd)
        return 1;


    // --------------------------------------------------------
    // Create initial icon
    // --------------------------------------------------------

    g_icon =
        CreateDiskIcon(
            GetDiskFreeSpaceBytes(),
            GetIconSize(g_hwnd)
        );

    if (!g_icon)
    {
        DestroyWindow(g_hwnd);
        return 1;
    }


    // --------------------------------------------------------
    // Configure tray notification
    // --------------------------------------------------------

    g_tray.cbSize =
        sizeof(NOTIFYICONDATAW);

    g_tray.hWnd =
        g_hwnd;

    g_tray.uID =
        1;

    g_tray.uFlags =
        NIF_ICON |
        NIF_MESSAGE |
        NIF_TIP;

    g_tray.uCallbackMessage =
        WM_TRAYICON;

    g_tray.hIcon =
        g_icon;

    wcscpy_s(
        g_tray.szTip,
        L"C: Disk Space"
    );


    // --------------------------------------------------------
    // Add icon to Windows notification area
    // --------------------------------------------------------

    if (!Shell_NotifyIconW(
        NIM_ADD,
        &g_tray))
    {
        DestroyIcon(g_icon);
        g_icon = nullptr;

        DestroyWindow(g_hwnd);

        return 1;
    }


    // --------------------------------------------------------
    // Update immediately
    // --------------------------------------------------------

    UpdateTray();


    // --------------------------------------------------------
    // Start update timer
    // --------------------------------------------------------

    SetTimer(
        g_hwnd,
        TIMER_ID,
        UPDATE_INTERVAL,
        nullptr
    );


    // --------------------------------------------------------
    // Message loop
    // --------------------------------------------------------

    MSG message{};

    while (GetMessageW(
        &message,
        nullptr,
        0,
        0))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }


    return 0;
}
