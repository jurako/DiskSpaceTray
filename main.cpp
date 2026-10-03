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

constexpr UINT UPDATE_INTERVAL = 10000; // 10 seconds

constexpr UINT ID_REFRESH = 1001;
constexpr UINT ID_EXIT    = 1002;


// ============================================================
// Global variables
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
// Determine appropriate icon size for current DPI
// ============================================================

int GetIconSize(HWND hwnd)
{
    UINT dpi = 96;

    if (hwnd)
    {
        dpi = GetDpiForWindow(hwnd);
    }

    // Base tray icon size: 16x16 at 100% scaling.
    int size = MulDiv(16, dpi, 96);

    if (size < 16)
        size = 16;

    if (size > 64)
        size = 64;

    return size;
}


// ============================================================
// Create tray icon containing free-space text
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
    // Create text
    // --------------------------------------------------------

    std::wstring text;

    double freeGB =
        static_cast<double>(freeBytes) /
        static_cast<double>(GB);

    if (freeBytes < GB)
    {
        // Less than 1 GB -> show MB.

        unsigned long long freeMB =
            freeBytes / MB;

        text =
            std::to_wstring(freeMB) + L"M";
    }
    else
    {
        // 1 GB or more -> show GB.

        std::wstringstream ss;

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
    // Select background color
    // --------------------------------------------------------

    COLORREF background;

    if (freeGB < 5.0)
    {
        // Critical: red
        background = RGB(210, 55, 55);
    }
    else if (freeGB < 20.0)
    {
        // Warning: amber
        background = RGB(210, 165, 45);
    }
    else
    {
        // Normal: green
        background = RGB(45, 155, 85);
    }


    // --------------------------------------------------------
    // Create drawing DC
    // --------------------------------------------------------

    HDC screenDC =
        GetDC(nullptr);

    if (!screenDC)
        return nullptr;

    HDC dc =
        CreateCompatibleDC(screenDC);

    if (!dc)
    {
        ReleaseDC(nullptr, screenDC);
        return nullptr;
    }

    HBITMAP bitmap =
        CreateCompatibleBitmap(
            screenDC,
            size,
            size
        );

    if (!bitmap)
    {
        DeleteDC(dc);
        ReleaseDC(nullptr, screenDC);
        return nullptr;
    }

    HGDIOBJ oldBitmap =
        SelectObject(dc, bitmap);


    // --------------------------------------------------------
    // Fill background
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

    // Make short values very large.
    //
    // Examples:
    //
    //   25G   -> approximately 72% of icon height
    //   850M  -> approximately 72%
    //   9.8G  -> approximately 62%
    //   12.5G -> approximately 52%
    //
    // This allows short values to look much closer to
    // the apparent size of Windows tray text such as "ENG".

    int fontHeight;

    if (text.length() >= 5)
    {
        fontHeight =
            static_cast<int>(size * 0.52);
    }
    else if (text.length() >= 4)
    {
        fontHeight =
            static_cast<int>(size * 0.62);
    }
    else
    {
        fontHeight =
            static_cast<int>(size * 0.72);
    }

    if (fontHeight < 9)
        fontHeight = 9;


    // --------------------------------------------------------
    // Create Segoe UI Bold font
    // --------------------------------------------------------

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

    if (!font)
    {
        SelectObject(dc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);
        ReleaseDC(nullptr, screenDC);
        return nullptr;
    }


    HGDIOBJ oldFont =
        SelectObject(dc, font);


    // --------------------------------------------------------
    // Text rendering settings
    // --------------------------------------------------------

    SetBkMode(
        dc,
        TRANSPARENT
    );

    SetTextColor(
        dc,
        RGB(255, 255, 255)
    );


    // --------------------------------------------------------
    // Text rectangle
    //
    // Leave a tiny margin around the text so the large
    // characters don't touch the icon edges.
    // --------------------------------------------------------

    int margin =
        max(0, size / 16);

    RECT textRect{
        margin,
        margin,
        size - margin,
        size - margin
    };


    // --------------------------------------------------------
    // Draw centered text
    // --------------------------------------------------------

    DrawTextW(
        dc,
        text.c_str(),
        -1,
        &textRect,
        DT_CENTER |
        DT_VCENTER |
        DT_SINGLELINE |
        DT_NOPREFIX |
        DT_NOCLIP
    );


    // --------------------------------------------------------
    // Restore font and delete it
    // --------------------------------------------------------

    SelectObject(
        dc,
        oldFont
    );

    DeleteObject(font);


    // --------------------------------------------------------
    // Create icon mask
    //
    // BLACK = opaque
    // WHITE = transparent
    // --------------------------------------------------------

    HBITMAP mask =
        CreateBitmap(
            size,
            size,
            1,
            1,
            nullptr
        );

    if (!mask)
    {
        SelectObject(dc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);
        ReleaseDC(nullptr, screenDC);
        return nullptr;
    }


    HDC maskDC =
        CreateCompatibleDC(screenDC);

    if (!maskDC)
    {
        SelectObject(dc, oldBitmap);
        DeleteObject(bitmap);
        DeleteObject(mask);
        DeleteDC(dc);
        ReleaseDC(nullptr, screenDC);
        return nullptr;
    }


    HGDIOBJ oldMask =
        SelectObject(maskDC, mask);


    // Black means the icon is opaque.
    PatBlt(
        maskDC,
        0,
        0,
        size,
        size,
        BLACKNESS
    );


    SelectObject(
        maskDC,
        oldMask
    );

    DeleteDC(maskDC);


    // --------------------------------------------------------
    // Create Windows HICON
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
// Update tray icon and tooltip
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
    // Create detailed tooltip
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
    // Tell Windows to update the tray icon
    // --------------------------------------------------------

    Shell_NotifyIconW(
        NIM_MODIFY,
        &g_tray
    );
}


// ============================================================
// Show right-click menu
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


    // Required for tray menus.
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

        // Recreate the icon at the new DPI.
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
    // Tell Windows that this application is DPI aware.
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
    // Configure notification-area icon
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
    // Set initial value
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
    // Windows message loop
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
