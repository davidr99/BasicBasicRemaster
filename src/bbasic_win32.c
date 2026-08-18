#include "bbasic_runtime.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>

#define BB_GUI_QUEUE 256
#define BB_GUI_FONTS 64
#define BB_GUI_BITMAPS 64
#define BB_GUI_DIALOG_VALUES 256

typedef struct BbQueuedKey {
    char bytes[3];
} BbQueuedKey;

typedef struct BbBitmapSlot {
    HBITMAP bitmap;
    HDC dc;
    HGDIOBJ previous;
    int width;
    int height;
} BbBitmapSlot;

typedef struct BbCaptureSlot {
    BbNumArray *key;
    BbBitmapSlot image;
} BbCaptureSlot;

static HWND main_window;
static HWND dialog_window;
static HDC display_dc;
static HBITMAP display_bitmap;
static HGDIOBJ display_previous;
static HDC selected_dc;
static int display_width = 640;
static int display_height = 480;
static int screen_mode;
static int text_x;
static int text_y;
static COLORREF text_foreground = RGB(255, 255, 255);
static COLORREF text_background = RGB(0, 0, 0);
static RECT scroll_rectangle;
static int scroll_enabled;
static HDC printer_dc;
static char configured_window_name[256] = "Modern BasicBasic";
static int hinted_width = 640;
static int hinted_height = 400;
static int viewport_enabled;
static int viewport_left;
static int viewport_top;
static int viewport_width = 640;
static int viewport_height = 480;
static COLORREF colors[256];
static int colors_ready;
static HFONT fonts[BB_GUI_FONTS];
static HFONT selected_font;
static HFONT default_text_font;
static HFONT compact_control_font;
static BbBitmapSlot bitmaps[BB_GUI_BITMAPS];
static BbCaptureSlot captures[BB_GUI_BITMAPS];
static BbQueuedKey key_queue[BB_GUI_QUEUE];
static unsigned key_read;
static unsigned key_write;
static double mouse_x_value;
static double mouse_y_value;
static double mouse_button_value;
static char dialog_values[BB_GUI_DIALOG_VALUES][BB_STRING_CAPACITY];
static HMENU menu_bar;
static HMENU submenus[6];
static HANDLE directory_search = INVALID_HANDLE_VALUE;
static WIN32_FIND_DATAA directory_data;
static int directory_attributes;

static void initialize_colors(void)
{
    static const COLORREF defaults[16] = {
        RGB(0, 0, 0), RGB(0, 0, 128), RGB(0, 128, 0), RGB(0, 128, 128),
        RGB(128, 0, 0), RGB(128, 0, 128), RGB(128, 128, 0), RGB(192, 192, 192),
        RGB(128, 128, 128), RGB(0, 0, 255), RGB(0, 255, 0), RGB(0, 255, 255),
        RGB(255, 0, 0), RGB(255, 0, 255), RGB(255, 255, 0), RGB(255, 255, 255)
    };
    if (colors_ready) return;
    memcpy(colors, defaults, sizeof(defaults));
    for (int index = 16; index < 256; ++index) {
        int value = index;
        colors[index] = RGB(value, value, value);
    }
    colors_ready = 1;
}

static COLORREF basic_color(double value)
{
    int index = (int)value;
    initialize_colors();
    if (index < 0) index = 0;
    return colors[index & 255];
}

static void queue_key(int first, int second)
{
    unsigned next = (key_write + 1U) % BB_GUI_QUEUE;
    if (next == key_read) return;
    key_queue[key_write].bytes[0] = first == 0 ? BB_NUL_SENTINEL : (char)first;
    key_queue[key_write].bytes[1] = second >= 0 ? (char)second : '\0';
    key_queue[key_write].bytes[2] = '\0';
    key_write = next;
}

static LRESULT colored_control_brush(WPARAM wparam, LPARAM lparam)
{
    HWND control = (HWND)lparam;
    HBRUSH brush = (HBRUSH)GetPropA(control, "BasicBasicBrush");
    HANDLE foreground = GetPropA(control, "BasicBasicForeground");
    HANDLE background = GetPropA(control, "BasicBasicBackground");
    if (brush == NULL || foreground == NULL || background == NULL) return 0;
    SetTextColor((HDC)wparam,
                 (COLORREF)((UINT_PTR)foreground - (UINT_PTR)1));
    SetBkColor((HDC)wparam,
               (COLORREF)((UINT_PTR)background - (UINT_PTR)1));
    return (LRESULT)brush;
}

static void destroy_control_window(HWND control)
{
    HBRUSH brush;
    if (control == NULL) return;
    brush = (HBRUSH)RemovePropA(control, "BasicBasicBrush");
    (void)RemovePropA(control, "BasicBasicForeground");
    (void)RemovePropA(control, "BasicBasicBackground");
    if (brush != NULL) DeleteObject(brush);
    DestroyWindow(control);
}

static LRESULT CALLBACK bb_window_proc(HWND window, UINT message,
                                       WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        if (display_dc != NULL) {
            int source_x = viewport_enabled ? (viewport_left - 1) * 8 : 0;
            int source_y = viewport_enabled ? (viewport_top - 1) * 16 : 0;
            int width = viewport_enabled ? viewport_width : display_width;
            int height = viewport_enabled ? viewport_height : display_height;
            if (source_x < 0) source_x = 0;
            if (source_y < 0) source_y = 0;
            BitBlt(dc, 0, 0, width, height, display_dc, source_x, source_y,
                   SRCCOPY);
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_COMMAND:
        if (HIWORD(wparam) == BN_CLICKED || HIWORD(wparam) == CBN_SELCHANGE ||
            HIWORD(wparam) == 0) {
            int identifier = LOWORD(wparam);
            queue_key(0, identifier >= 1000 ? identifier - 1000 : identifier);
        }
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        LRESULT brush = colored_control_brush(wparam, lparam);
        if (brush != 0) return brush;
        return DefWindowProcA(window, message, wparam, lparam);
    }
    case WM_CHAR:
        queue_key((int)wparam, -1);
        return 0;
    case WM_MOUSEMOVE:
        mouse_x_value = (double)(short)LOWORD(lparam);
        mouse_y_value = (double)(short)HIWORD(lparam);
        return 0;
    case WM_LBUTTONDOWN:
        mouse_button_value = 1.0;
        return 0;
    case WM_LBUTTONUP:
        mouse_button_value = 0.0;
        return 0;
    case WM_CLOSE:
        queue_key(0, 68);
        ShowWindow(window, SW_HIDE);
        return 0;
    default:
        return DefWindowProcA(window, message, wparam, lparam);
    }
}

static LRESULT CALLBACK bb_dialog_proc(HWND window, UINT message,
                                       WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_COMMAND:
        if (HIWORD(wparam) == BN_CLICKED || HIWORD(wparam) == CBN_SELCHANGE ||
            HIWORD(wparam) == LBN_SELCHANGE || HIWORD(wparam) == 0) {
            int identifier = LOWORD(wparam);
            queue_key(0, identifier >= 1000 ? identifier - 1000 : identifier);
        }
        return 0;
    case WM_CLOSE:
        queue_key(0, 68);
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        LRESULT brush = colored_control_brush(wparam, lparam);
        HDC dc = (HDC)wparam;
        if (brush != 0) return brush;
        SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
        SetBkColor(dc, GetSysColor(COLOR_3DFACE));
        return (LRESULT)GetSysColorBrush(COLOR_3DFACE);
    }
    default:
        return DefWindowProcA(window, message, wparam, lparam);
    }
}

static void create_display_surface(int width, int height)
{
    HDC window_dc;
    HBITMAP bitmap;
    if (main_window == NULL) return;
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    window_dc = GetDC(main_window);
    if (display_dc == NULL) display_dc = CreateCompatibleDC(window_dc);
    if (default_text_font == NULL) {
        default_text_font = CreateFontA(
            -16, 8, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
            FIXED_PITCH | FF_MODERN, "Consolas");
        fonts[0] = default_text_font;
    }
    bitmap = CreateCompatibleBitmap(window_dc, width, height);
    if (bitmap != NULL) {
        HGDIOBJ previous = SelectObject(display_dc, bitmap);
        if (display_bitmap != NULL) {
            SelectObject(display_dc, display_previous);
            DeleteObject(display_bitmap);
            display_previous = SelectObject(display_dc, bitmap);
        } else {
            display_previous = previous;
        }
        display_bitmap = bitmap;
        display_width = width;
        display_height = height;
        selected_dc = display_dc;
        if (selected_font == NULL && default_text_font != NULL)
            SelectObject(display_dc, default_text_font);
        PatBlt(display_dc, 0, 0, width, height, BLACKNESS);
    }
    ReleaseDC(main_window, window_dc);
}

static void ensure_window(void)
{
    static int registered;
    HINSTANCE instance = GetModuleHandleA(NULL);
    if (!registered) {
        WNDCLASSA window_class;
        memset(&window_class, 0, sizeof(window_class));
        window_class.style = CS_HREDRAW | CS_VREDRAW;
        window_class.lpfnWndProc = bb_window_proc;
        window_class.hInstance = instance;
        window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
        window_class.hIcon = LoadIconA(instance, MAKEINTRESOURCEA(1));
        if (window_class.hIcon == NULL)
            window_class.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        window_class.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        window_class.lpszClassName = "ModernBasicBasicWindow";
        (void)RegisterClassA(&window_class);
        registered = 1;
    }
    if (main_window == NULL) {
        DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
        RECT rectangle = {0, 0, hinted_width, hinted_height};
        AdjustWindowRect(&rectangle, style, FALSE);
        main_window = CreateWindowExA(
            0, "ModernBasicBasicWindow", configured_window_name,
            style, CW_USEDEFAULT, CW_USEDEFAULT,
            rectangle.right - rectangle.left,
            rectangle.bottom - rectangle.top, NULL, NULL, instance, NULL);
        create_display_surface(640, 480);
        ShowWindow(main_window, SW_SHOW);
        UpdateWindow(main_window);
    }
}

static int create_dialog_window(double x, double y, double width,
                                double height, const char *title)
{
    static int registered;
    HINSTANCE instance = GetModuleHandleA(NULL);
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
    DWORD extended_style = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
    RECT rectangle = {0, 0, (LONG)width, (LONG)height};
    if (!registered) {
        WNDCLASSA window_class;
        memset(&window_class, 0, sizeof(window_class));
        window_class.lpfnWndProc = bb_dialog_proc;
        window_class.hInstance = instance;
        window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
        window_class.hIcon = LoadIconA(instance, MAKEINTRESOURCEA(1));
        if (window_class.hIcon == NULL)
            window_class.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        window_class.hbrBackground = GetSysColorBrush(COLOR_3DFACE);
        window_class.lpszClassName = "ModernBasicBasicDialog";
        (void)RegisterClassA(&window_class);
        registered = 1;
    }
    if (rectangle.right < 100) rectangle.right = 100;
    if (rectangle.bottom < 80) rectangle.bottom = 80;
    AdjustWindowRectEx(&rectangle, style, FALSE, extended_style);
    dialog_window = CreateWindowExA(
        extended_style, "ModernBasicBasicDialog",
        title != NULL ? title : "Dialog", style, (int)x, (int)y,
        rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
        main_window, NULL, instance, NULL);
    if (dialog_window == NULL) return 0;
    EnableWindow(main_window, FALSE);
    ShowWindow(dialog_window, SW_SHOW);
    UpdateWindow(dialog_window);
    return 1;
}

static HWND control_parent(void)
{
    return dialog_window != NULL ? dialog_window : main_window;
}

static void refresh(void)
{
    if (main_window != NULL) {
        InvalidateRect(main_window, NULL, FALSE);
        bb_gui_pump();
    }
}

static void scale_control_coordinates(int *x, int *y, int *width, int *height)
{
    if (screen_mode == 0) {
        if (viewport_enabled && dialog_window == NULL) {
            *x -= viewport_left;
            *y -= viewport_top;
        } else if (dialog_window == NULL) {
            /* Text-mode CONTROL coordinates are one-based, like LOCATE. */
            *x -= 1;
            *y -= 1;
        }
        *x *= 8;
        *y *= 16;
        *width *= 8;
        *height *= 16;
    }
}

int bb_gui_inkey(char *destination, size_t capacity)
{
    if (destination == NULL || capacity == 0U) return 0;
    bb_gui_pump();
    if (key_read == key_write) return 0;
    bb_set_string(destination, capacity, key_queue[key_read].bytes);
    key_read = (key_read + 1U) % BB_GUI_QUEUE;
    return 1;
}

void bb_gui_pump(void)
{
    MSG message;
    while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) {
        if (dialog_window != NULL &&
            IsDialogMessageA(dialog_window, &message))
            continue;
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
}

int bb_gui_active(void)
{
    return main_window != NULL && IsWindow(main_window);
}

void bb_gui_input_line(char *destination, size_t capacity, const char *prompt)
{
    char key[8];
    size_t length = 0U;
    int input_x;
    int input_y;
    TEXTMETRICA metrics;
    if (destination == NULL || capacity == 0U) return;
    destination[0] = '\0';
    ensure_window();
    if (prompt != NULL && prompt[0] != '\0') bb_gui_print(prompt);
    input_x = text_x;
    input_y = text_y;
    GetTextMetricsA(selected_dc, &metrics);
    SetFocus(main_window);
    key_read = key_write;
    while (main_window != NULL && IsWindow(main_window)) {
        if (!bb_gui_inkey(key, sizeof(key))) {
            WaitMessage();
            continue;
        }
        if (key[0] == BB_NUL_SENTINEL) {
            if ((unsigned char)key[1] == 68U) break;
            continue;
        }
        if (key[0] == '\r' || key[0] == '\n') break;
        if (key[0] == '\b') {
            if (length > 0U) destination[--length] = '\0';
        } else if ((unsigned char)key[0] >= 32U && length + 1U < capacity) {
            destination[length++] = key[0];
            destination[length] = '\0';
        } else {
            continue;
        }
        {
            RECT input_rectangle = {input_x, input_y, display_width,
                                    input_y + metrics.tmHeight +
                                        metrics.tmExternalLeading};
            SIZE size = {0, 0};
            HBRUSH brush = CreateSolidBrush(text_background);
            FillRect(selected_dc, &input_rectangle, brush);
            DeleteObject(brush);
            SetTextColor(selected_dc, text_foreground);
            SetBkColor(selected_dc, text_background);
            SetBkMode(selected_dc, OPAQUE);
            TextOutA(selected_dc, input_x, input_y, destination, (int)length);
            GetTextExtentPoint32A(selected_dc, destination, (int)length, &size);
            text_x = input_x + size.cx;
            refresh();
        }
    }
    bb_gui_newline();
}

double bb_gui_system(double selector)
{
    RECT rectangle;
    int code = (int)selector;
    if (!bb_gui_active()) return NAN;
    GetWindowRect(main_window, &rectangle);
    switch (code) {
    case 1: return (double)(display_width - 1);
    case 2: return (double)(display_height - 1);
    case 3: return (double)(GetSystemMetrics(SM_CXSCREEN) - 1);
    case 4: return (double)(GetSystemMetrics(SM_CYSCREEN) - 1);
    case 5: return 255.0;
    case 6: return 15.0;
    case 7: return (double)screen_mode;
    case 8: return (double)rectangle.left;
    case 9: return (double)rectangle.top;
    case 10: return (double)display_width;
    case 11: return (double)display_height;
    case 12: return main_window == GetForegroundWindow() ? 1.0 : 0.0;
    case 17: return 256.0;
    case 18: return 0.0;
    case 19: return IsIconic(main_window) ? 1.0 : 0.0;
    default: return 0.0;
    }
}

void bb_gui_print(const char *text)
{
    SIZE size = {0, 0};
    int length;
    ensure_window();
    if (text == NULL) text = "";
    length = (int)strlen(text);
    SetTextColor(selected_dc, text_foreground);
    SetBkColor(selected_dc, text_background);
    if (screen_mode == 0) {
        RECT cells = {text_x, text_y, text_x + length * 8, text_y + 16};
        HBRUSH background = CreateSolidBrush(text_background);
        FillRect(selected_dc, &cells, background);
        DeleteObject(background);
        SetBkMode(selected_dc, TRANSPARENT);
    } else {
        SetBkMode(selected_dc, OPAQUE);
    }
    TextOutA(selected_dc, text_x, text_y, text, length);
    if (screen_mode == 0) {
        text_x += length * 8;
    } else {
        GetTextExtentPoint32A(selected_dc, text, length, &size);
        text_x += size.cx;
    }
    refresh();
}

void bb_gui_newline(void)
{
    TEXTMETRICA metrics;
    int line_height;
    ensure_window();
    GetTextMetricsA(selected_dc, &metrics);
    line_height = screen_mode == 0
                      ? 16
                      : metrics.tmHeight + metrics.tmExternalLeading;
    text_x = 0;
    text_y += line_height;
    if (scroll_enabled && text_y + metrics.tmHeight > scroll_rectangle.bottom) {
        int width = scroll_rectangle.right - scroll_rectangle.left;
        int height = scroll_rectangle.bottom - scroll_rectangle.top;
        HBRUSH brush;
        BitBlt(selected_dc, scroll_rectangle.left, scroll_rectangle.top,
               width, height - line_height, selected_dc, scroll_rectangle.left,
               scroll_rectangle.top + line_height, SRCCOPY);
        brush = CreateSolidBrush(text_background);
        {
            RECT bottom = {scroll_rectangle.left,
                           scroll_rectangle.bottom - line_height,
                           scroll_rectangle.right, scroll_rectangle.bottom};
            FillRect(selected_dc, &bottom, brush);
        }
        DeleteObject(brush);
        text_y = scroll_rectangle.bottom - line_height;
        refresh();
    }
}

void bb_gui_cls(void)
{
    HBRUSH brush;
    RECT rectangle = {0, 0, display_width, display_height};
    ensure_window();
    brush = CreateSolidBrush(text_background);
    FillRect(selected_dc, &rectangle, brush);
    DeleteObject(brush);
    text_x = 0;
    text_y = 0;
    refresh();
}

void bb_gui_color(double foreground, double background)
{
    text_foreground = basic_color(foreground);
    text_background = basic_color(background);
}

void bb_gui_locate(double row, double column)
{
    TEXTMETRICA metrics;
    ensure_window();
    if (screen_mode == 1000) {
        text_x = (int)column;
        text_y = (int)row;
    } else if (screen_mode == 0) {
        text_x = ((int)column - 1) * 8;
        text_y = ((int)row - 1) * 16;
    } else {
        GetTextMetricsA(selected_dc, &metrics);
        text_x = ((int)column - 1) * metrics.tmAveCharWidth;
        text_y = ((int)row - 1) * metrics.tmHeight;
    }
    if (text_x < 0) text_x = 0;
    if (text_y < 0) text_y = 0;
}

void bb_scroll_area(double left, double top, double right, double bottom)
{
    TEXTMETRICA metrics;
    ensure_window();
    if (screen_mode == 1000) {
        scroll_rectangle.left = (LONG)left;
        scroll_rectangle.top = (LONG)top;
        scroll_rectangle.right = (LONG)right + 1;
        scroll_rectangle.bottom = (LONG)bottom + 1;
    } else {
        GetTextMetricsA(selected_dc, &metrics);
        scroll_rectangle.left = ((LONG)left - 1) * metrics.tmAveCharWidth;
        scroll_rectangle.top = ((LONG)top - 1) * metrics.tmHeight;
        scroll_rectangle.right = (LONG)right * metrics.tmAveCharWidth;
        scroll_rectangle.bottom = (LONG)bottom * metrics.tmHeight;
    }
    scroll_enabled = 1;
}

void bb_screen(double mode, double color_count)
{
    (void)color_count;
    screen_mode = (int)mode;
    if (screen_mode != 0) viewport_enabled = 0;
    ensure_window();
    ShowWindow(main_window, SW_SHOW);
    refresh();
}

void bb_window_name(const char *name)
{
    bb_set_string(configured_window_name, sizeof(configured_window_name), name);
    if (main_window != NULL) SetWindowTextA(main_window, configured_window_name);
}

void bb_window_size_hint(double left, double top, double right, double bottom)
{
    viewport_enabled = 1;
    viewport_left = (int)left;
    viewport_top = (int)top;
    viewport_width = (int)((right - left + 1.0) * 8.0);
    viewport_height = (int)((bottom - top + 1.0) * 16.0);
    hinted_width = viewport_width;
    hinted_height = viewport_height;
    if (hinted_width < 100) hinted_width = 100;
    if (hinted_height < 80) hinted_height = 80;
}

void bb_on_paint(void)
{
    ensure_window();
    /* WM_PAINT restores the persistent display bitmap automatically. */
}

void bb_position(double x, double y, double width, double height,
                 double repaint)
{
    RECT rectangle;
    int px = (int)x;
    int py = (int)y;
    int w = (int)width;
    int h = (int)height;
    (void)repaint;
    ensure_window();
    viewport_enabled = 0;
    if (px == -2) px = CW_USEDEFAULT;
    if (py == -2) py = CW_USEDEFAULT;
    if (w <= 0) w = display_width;
    if (h <= 0) h = display_height;
    rectangle.left = 0;
    rectangle.top = 0;
    rectangle.right = w;
    rectangle.bottom = h;
    AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, menu_bar != NULL);
    SetWindowPos(main_window, NULL, px, py, rectangle.right - rectangle.left,
                 rectangle.bottom - rectangle.top, SWP_NOZORDER | SWP_SHOWWINDOW);
    create_display_surface(w, h);
    refresh();
}

void bb_graphics_line(double x1, double y1, double x2, double y2,
                      double color, double style)
{
    HPEN pen;
    HGDIOBJ old_pen;
    HBRUSH brush;
    ensure_window();
    if (selected_dc == NULL) selected_dc = display_dc;
    pen = CreatePen(PS_SOLID, 1, basic_color(color));
    old_pen = SelectObject(selected_dc, pen);
    if ((int)style == 2) {
        RECT rectangle = {(LONG)x1, (LONG)y1, (LONG)x2 + 1, (LONG)y2 + 1};
        brush = CreateSolidBrush(basic_color(color));
        FillRect(selected_dc, &rectangle, brush);
        DeleteObject(brush);
    } else if ((int)style == 1) {
        HGDIOBJ old_brush = SelectObject(selected_dc, GetStockObject(NULL_BRUSH));
        Rectangle(selected_dc, (int)x1, (int)y1, (int)x2 + 1, (int)y2 + 1);
        SelectObject(selected_dc, old_brush);
    } else {
        MoveToEx(selected_dc, (int)x1, (int)y1, NULL);
        LineTo(selected_dc, (int)x2, (int)y2);
    }
    SelectObject(selected_dc, old_pen);
    DeleteObject(pen);
    refresh();
}

void bb_graphics_circle(double x, double y, double radius, double color,
                        double start_angle, double end_angle)
{
    HPEN pen;
    HGDIOBJ old_pen;
    HGDIOBJ old_brush;
    bool radial_start = start_angle < 0.0;
    bool radial_end = end_angle < 0.0;
    ensure_window();
    pen = CreatePen(PS_SOLID, 1, basic_color(color));
    old_pen = SelectObject(selected_dc, pen);
    old_brush = SelectObject(selected_dc, GetStockObject(NULL_BRUSH));
    if (start_angle == 0.0 && end_angle == 0.0) {
        Ellipse(selected_dc, (int)(x - radius), (int)(y - radius),
                (int)(x + radius) + 1, (int)(y + radius) + 1);
    } else {
        double sweep;
        int segments;
        int start_x;
        int start_y;
        int end_x;
        int end_y;
        if (radial_start) start_angle = -start_angle;
        if (radial_end) end_angle = -end_angle;
        while (start_angle < 0.0) start_angle += 6.28318530717958647692;
        while (end_angle < 0.0) end_angle += 6.28318530717958647692;
        while (end_angle <= start_angle) end_angle += 6.28318530717958647692;
        sweep = end_angle - start_angle;
        segments = (int)ceil(fabs(radius) * sweep);
        if (segments < 8) segments = 8;
        if (segments > 4096) segments = 4096;
        start_x = (int)lround(x + radius * cos(start_angle));
        start_y = (int)lround(y - radius * sin(start_angle));
        MoveToEx(selected_dc, start_x, start_y, NULL);
        for (int segment = 1; segment <= segments; ++segment) {
            double angle = start_angle + sweep * (double)segment /
                                             (double)segments;
            LineTo(selected_dc,
                   (int)lround(x + radius * cos(angle)),
                   (int)lround(y - radius * sin(angle)));
        }
        end_x = (int)lround(x + radius * cos(end_angle));
        end_y = (int)lround(y - radius * sin(end_angle));
        /* BasicBasic follows the classic BASIC convention: a negative
           start or end angle requests a radial line at that endpoint,
           allowing CIRCLE plus PAINT to create a closed pie section. */
        if (radial_start) {
            MoveToEx(selected_dc, (int)x, (int)y, NULL);
            LineTo(selected_dc, start_x, start_y);
        }
        if (radial_end) {
            MoveToEx(selected_dc, (int)x, (int)y, NULL);
            LineTo(selected_dc, end_x, end_y);
        }
    }
    SelectObject(selected_dc, old_brush);
    SelectObject(selected_dc, old_pen);
    DeleteObject(pen);
    refresh();
}

void bb_graphics_paint(double x, double y, double color, double border)
{
    HBRUSH brush;
    HGDIOBJ old_brush;
    ensure_window();
    brush = CreateSolidBrush(basic_color(color));
    old_brush = SelectObject(selected_dc, brush);
    ExtFloodFill(selected_dc, (int)x, (int)y, basic_color(border),
                 FLOODFILLBORDER);
    SelectObject(selected_dc, old_brush);
    DeleteObject(brush);
    refresh();
}

void bb_graphics_pset(double x, double y, double color)
{
    ensure_window();
    SetPixel(selected_dc, (int)x, (int)y, basic_color(color));
    refresh();
}

void bb_palette(double index, double color)
{
    unsigned long value = (unsigned long)color;
    int slot = (int)index & 255;
    initialize_colors();
    colors[slot] = RGB(value & 255U, (value >> 8U) & 255U,
                       (value >> 16U) & 255U);
}

void bb_control(const char *text, double identifier, double status,
                const char *type, double extra, double x, double y,
                double width, double height, double foreground,
                double background)
{
    const char *class_name = "STATIC";
    DWORD style = WS_CHILD | WS_VISIBLE;
    HWND control;
    bool accepts_colors = false;
    int ix = (int)x, iy = (int)y, iw = (int)width, ih = (int)height;
    (void)status;
    (void)extra;
    (void)foreground;
    (void)background;
    ensure_window();
    if (type != NULL && _stricmp(type, "edit") == 0) {
        class_name = "EDIT";
        style |= WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP;
    } else if (type != NULL &&
               (_stricmp(type, "radio") == 0 ||
                _stricmp(type, "radiobutton") == 0)) {
        class_name = "BUTTON";
        style |= BS_RADIOBUTTON | WS_TABSTOP;
        accepts_colors = true;
    } else if (type != NULL && _stricmp(type, "checkbox") == 0) {
        class_name = "BUTTON";
        style |= BS_AUTOCHECKBOX | WS_TABSTOP;
        accepts_colors = true;
    } else if (type != NULL && _stricmp(type, "combo") == 0) {
        class_name = "COMBOBOX";
        style |= CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP;
    } else if (type != NULL &&
               (_stricmp(type, "push") == 0 || _stricmp(type, "button") == 0 ||
                _stricmp(type, "pushbutton") == 0 || _stricmp(type, "ok") == 0)) {
        class_name = "BUTTON";
        style |= (_stricmp(type, "ok") == 0 ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON) |
                 WS_TABSTOP;
    } else if (type != NULL && _stricmp(type, "filelist") == 0) {
        class_name = "LISTBOX";
        style |= WS_BORDER | WS_VSCROLL | LBS_NOTIFY | WS_TABSTOP;
    } else if (type != NULL &&
               (_stricmp(type, "filename") == 0 ||
                _stricmp(type, "filepath") == 0)) {
        class_name = "EDIT";
        style |= WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP;
    } else if (type != NULL && _stricmp(type, "group") == 0) {
        class_name = "BUTTON";
        style |= BS_GROUPBOX;
    } else {
        style |= SS_LEFT | SS_CENTERIMAGE;
        accepts_colors = true;
    }
    scale_control_coordinates(&ix, &iy, &iw, &ih);
    control = CreateWindowExA(0, class_name, text != NULL ? text : "", style,
                              ix, iy, iw, ih, control_parent(),
                              (HMENU)(INT_PTR)(int)identifier,
                              GetModuleHandleA(NULL), NULL);
    if (control != NULL) {
        HFONT control_font = selected_font;
        if (dialog_window == NULL && screen_mode == 0 && ih <= 16) {
            if (compact_control_font == NULL)
                compact_control_font = CreateFontA(
                    -12, 6, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN,
                    "Consolas");
            control_font = compact_control_font;
        }
        else if (control_font != NULL) {
            LOGFONTA description;
            memset(&description, 0, sizeof(description));
            if (GetObjectA(control_font, sizeof(description), &description) == 0 ||
                abs(description.lfHeight) + 4 > ih)
                control_font = NULL;
        }
        if (control_font == NULL)
            control_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        SendMessageA(control, WM_SETFONT, (WPARAM)control_font, TRUE);
        if (accepts_colors && (foreground != 0.0 || background != 0.0)) {
            COLORREF foreground_color = basic_color(foreground);
            COLORREF background_color = basic_color(background);
            HBRUSH brush = CreateSolidBrush(background_color);
            if (brush != NULL) {
                SetPropA(control, "BasicBasicBrush", brush);
                SetPropA(control, "BasicBasicForeground",
                         (HANDLE)((UINT_PTR)foreground_color + (UINT_PTR)1));
                SetPropA(control, "BasicBasicBackground",
                         (HANDLE)((UINT_PTR)background_color + (UINT_PTR)1));
            }
        }
    }
}

void bb_delete_control(double identifier)
{
    HWND parent = control_parent();
    HWND control = parent != NULL ? GetDlgItem(parent, (int)identifier) : NULL;
    if (control != NULL) destroy_control_window(control);
}

void bb_set_control_text(double identifier, const char *text)
{
    HWND parent = control_parent();
    HWND control = parent != NULL ? GetDlgItem(parent, (int)identifier) : NULL;
    if (control != NULL) SetWindowTextA(control, text != NULL ? text : "");
}

const char *bb_get_control_text(double identifier)
{
    static char result[BB_STRING_CAPACITY];
    HWND parent = control_parent();
    HWND control = parent != NULL ? GetDlgItem(parent, (int)identifier) : NULL;
    result[0] = '\0';
    if (control != NULL) GetWindowTextA(control, result, (int)sizeof(result));
    return result;
}

void bb_radio(double identifier, double enabled)
{
    HWND parent = control_parent();
    HWND control = parent != NULL ? GetDlgItem(parent, (int)identifier) : NULL;
    if (control != NULL)
        SendMessageA(control, BM_SETCHECK, enabled != 0.0 ? BST_CHECKED : BST_UNCHECKED, 0);
}

double bb_set_focus(double identifier)
{
    HWND parent = control_parent();
    HWND control = parent != NULL ? GetDlgItem(parent, (int)identifier) : NULL;
    if (control == NULL) return 0.0;
    SetFocus(control);
    return identifier;
}

double bb_get_focus(void)
{
    HWND focus = GetFocus();
    return focus != NULL ? (double)GetDlgCtrlID(focus) : 0.0;
}

double bb_list(double identifier, double operation, double index,
               const char *text)
{
    HWND parent = control_parent();
    HWND control = parent != NULL ? GetDlgItem(parent, (int)identifier) : NULL;
    LRESULT result = 0;
    if (control == NULL) return -1.0;
    switch ((int)operation) {
    case 1: result = SendMessageA(control, CB_ADDSTRING, 0, (LPARAM)(text != NULL ? text : "")); break;
    case 2:
        result = SendMessageA(control, CB_FINDSTRINGEXACT, (WPARAM)-1,
                              (LPARAM)(text != NULL ? text : ""));
        if (result != CB_ERR) SendMessageA(control, CB_SETCURSEL, (WPARAM)result, 0);
        break;
    case 11: SendMessageA(control, CB_RESETCONTENT, 0, 0); break;
    default: result = SendMessageA(control, CB_GETCURSEL, 0, 0); break;
    }
    (void)index;
    return (double)result;
}

void bb_create_font(double identifier, double height, double width,
                    double escapement, double orientation, double weight,
                    double italic, double underline, double strikeout,
                    double charset, double output_precision,
                    double clipping_precision, double quality,
                    double pitch_family, const char *face)
{
    int slot = (int)identifier;
    if (slot < 0 || slot >= BB_GUI_FONTS) return;
    if (fonts[slot] != NULL) DeleteObject(fonts[slot]);
    fonts[slot] = CreateFontA((int)-fabs(height), (int)width, (int)escapement,
                              (int)orientation, (int)weight,
                              italic != 0.0, underline != 0.0,
                              strikeout != 0.0, (DWORD)charset,
                              (DWORD)output_precision,
                              (DWORD)clipping_precision, (DWORD)quality,
                              (DWORD)pitch_family, face != NULL ? face : "");
}

void bb_select_font(double identifier)
{
    int slot = (int)identifier;
    ensure_window();
    if (slot < 0 || slot >= BB_GUI_FONTS || fonts[slot] == NULL) return;
    selected_font = fonts[slot];
    SelectObject(selected_dc, selected_font);
}

double bb_text_length(const char *text)
{
    SIZE size = {0, 0};
    ensure_window();
    GetTextExtentPoint32A(selected_dc, text != NULL ? text : "",
                          (int)strlen(text != NULL ? text : ""), &size);
    return (double)size.cx;
}

double bb_font_info(double selector)
{
    TEXTMETRICA metrics;
    ensure_window();
    memset(&metrics, 0, sizeof(metrics));
    GetTextMetricsA(selected_dc, &metrics);
    switch ((int)selector) {
    case 1: return (double)metrics.tmHeight;
    case 7: return (double)metrics.tmAveCharWidth;
    default: return 0.0;
    }
}

double bb_device_info(double selector)
{
    HDC dc;
    int result = 0;
    ensure_window();
    dc = GetDC(main_window);
    switch ((int)selector) {
    case 4: result = GetDeviceCaps(dc, HORZSIZE); break;
    case 6: result = GetDeviceCaps(dc, VERTSIZE); break;
    case 8: result = GetDeviceCaps(dc, HORZRES); break;
    case 10: result = GetDeviceCaps(dc, VERTRES); break;
    default: result = 0; break;
    }
    ReleaseDC(main_window, dc);
    return (double)result;
}

double bb_bitmap_header(const char *filename, BbNumArray *information)
{
    HBITMAP bitmap;
    BITMAP details;
    if (filename == NULL || information == NULL) return 0.0;
    bitmap = (HBITMAP)LoadImageA(NULL, filename, IMAGE_BITMAP, 0, 0,
                                 LR_LOADFROMFILE | LR_CREATEDIBSECTION);
    if (bitmap == NULL) return 0.0;
    memset(&details, 0, sizeof(details));
    GetObjectA(bitmap, sizeof(details), &details);
    bb_num_array_set(information, 40.0, 1U, 0.0);
    bb_num_array_set(information, (double)details.bmWidth, 1U, 1.0);
    bb_num_array_set(information, (double)details.bmHeight, 1U, 2.0);
    bb_num_array_set(information, 1.0, 1U, 3.0);
    bb_num_array_set(information, (double)details.bmBitsPixel, 1U, 4.0);
    bb_num_array_set(information, 0.0, 1U, 5.0);
    bb_num_array_set(information,
                     (double)(details.bmWidthBytes * details.bmHeight), 1U,
                     6.0);
    DeleteObject(bitmap);
    return 1.0;
}

double bb_bitmap_colors(const char *filename, BbNumArray *color_array)
{
    HBITMAP bitmap;
    HDC dc;
    HGDIOBJ previous;
    RGBQUAD entries[256];
    UINT count;
    if (filename == NULL || color_array == NULL) return 0.0;
    bitmap = (HBITMAP)LoadImageA(NULL, filename, IMAGE_BITMAP, 0, 0,
                                 LR_LOADFROMFILE | LR_CREATEDIBSECTION);
    if (bitmap == NULL) return 0.0;
    dc = CreateCompatibleDC(NULL);
    previous = SelectObject(dc, bitmap);
    count = GetDIBColorTable(dc, 0, 256, entries);
    for (UINT index = 0; index < count; ++index) {
        double packed = (double)(entries[index].rgbRed |
                                 (entries[index].rgbGreen << 8U) |
                                 (entries[index].rgbBlue << 16U));
        bb_num_array_set(color_array, packed, 1U, (double)index);
    }
    SelectObject(dc, previous);
    DeleteDC(dc);
    DeleteObject(bitmap);
    return (double)count;
}

static HDC bitmap_source_dc(int identifier)
{
    if (identifier <= 0) return display_dc;
    if (identifier >= BB_GUI_BITMAPS) return NULL;
    return bitmaps[identifier].dc;
}

static DWORD raster_operation(double operation)
{
    return (int)operation == 0 ? SRCCOPY : SRCINVERT;
}

void bb_create_bitmap(double identifier, double mode, double width,
                      double height)
{
    int slot = (int)identifier;
    HDC reference;
    (void)mode;
    ensure_window();
    if (slot <= 0 || slot >= BB_GUI_BITMAPS) return;
    if (bitmaps[slot].dc != NULL) {
        SelectObject(bitmaps[slot].dc, bitmaps[slot].previous);
        DeleteObject(bitmaps[slot].bitmap);
        DeleteDC(bitmaps[slot].dc);
        memset(&bitmaps[slot], 0, sizeof(bitmaps[slot]));
    }
    reference = GetDC(main_window);
    bitmaps[slot].dc = CreateCompatibleDC(reference);
    bitmaps[slot].bitmap =
        CreateCompatibleBitmap(reference, (int)width, (int)height);
    bitmaps[slot].previous =
        SelectObject(bitmaps[slot].dc, bitmaps[slot].bitmap);
    bitmaps[slot].width = (int)width;
    bitmaps[slot].height = (int)height;
    PatBlt(bitmaps[slot].dc, 0, 0, (int)width, (int)height, BLACKNESS);
    ReleaseDC(main_window, reference);
}

void bb_select_bitmap(double identifier)
{
    int slot = (int)identifier;
    ensure_window();
    if (slot == 0)
        selected_dc = display_dc;
    else if (slot > 0 && slot < BB_GUI_BITMAPS && bitmaps[slot].dc != NULL)
        selected_dc = bitmaps[slot].dc;
}

void bb_select_display(void)
{
    ensure_window();
    selected_dc = display_dc;
}

void bb_load_bitmap(const char *filename, double source, double x, double y,
                    double source_x, double source_y, double width,
                    double height, double convert, double x_scale,
                    double y_scale)
{
    HBITMAP bitmap;
    HDC source_dc;
    HGDIOBJ previous;
    BITMAP details;
    int destination_width;
    int destination_height;
    (void)source;
    (void)convert;
    ensure_window();
    bitmap = (HBITMAP)LoadImageA(NULL, filename, IMAGE_BITMAP, 0, 0,
                                 LR_LOADFROMFILE | LR_CREATEDIBSECTION);
    if (bitmap == NULL) return;
    GetObjectA(bitmap, sizeof(details), &details);
    source_dc = CreateCompatibleDC(selected_dc);
    previous = SelectObject(source_dc, bitmap);
    destination_width = (int)((width > 0.0 ? width : details.bmWidth) *
                              (x_scale == 0.0 ? 1.0 : x_scale));
    destination_height = (int)((height > 0.0 ? height : details.bmHeight) *
                               (y_scale == 0.0 ? 1.0 : y_scale));
    StretchBlt(selected_dc, (int)x, (int)y, destination_width,
               destination_height, source_dc, (int)source_x, (int)source_y,
               width > 0.0 ? (int)width : details.bmWidth,
               height > 0.0 ? (int)height : details.bmHeight, SRCCOPY);
    SelectObject(source_dc, previous);
    DeleteDC(source_dc);
    DeleteObject(bitmap);
    refresh();
}

void bb_copy_bits(double source, double source_x, double source_y,
                  double width, double height, double destination,
                  double destination_x, double destination_y,
                  double operation)
{
    HDC source_dc = bitmap_source_dc((int)source);
    HDC destination_dc = (int)destination == 0
                             ? display_dc
                             : printer_dc != NULL ? printer_dc : selected_dc;
    if (source_dc == NULL || destination_dc == NULL) return;
    BitBlt(destination_dc, (int)destination_x, (int)destination_y, (int)width,
           (int)height, source_dc, (int)source_x, (int)source_y,
           raster_operation(operation));
    refresh();
}

void bb_stretch_bits(double source, double source_x, double source_y,
                     double source_width, double source_height,
                     double destination, double destination_x,
                     double destination_y, double destination_width,
                     double destination_height, double operation)
{
    HDC source_dc = bitmap_source_dc((int)source);
    HDC destination_dc = (int)destination == 0
                             ? display_dc
                             : printer_dc != NULL ? printer_dc : selected_dc;
    if (source_dc == NULL || destination_dc == NULL) return;
    StretchBlt(destination_dc, (int)destination_x, (int)destination_y,
               (int)destination_width, (int)destination_height, source_dc,
               (int)source_x, (int)source_y, (int)source_width,
               (int)source_height, raster_operation(operation));
    refresh();
}

void bb_store_bitmap(double source, const char *filename, double x, double y,
                     double width, double height, double compression,
                     double reserved)
{
    HDC source_dc = bitmap_source_dc((int)source);
    HDC temporary_dc;
    HBITMAP temporary_bitmap;
    HGDIOBJ previous;
    BITMAPINFO information;
    BITMAPFILEHEADER file_header;
    unsigned char *pixels;
    size_t row_bytes;
    size_t image_bytes;
    FILE *output;
    (void)compression;
    (void)reserved;
    if (source_dc == NULL || filename == NULL || width <= 0.0 || height <= 0.0)
        return;
    temporary_dc = CreateCompatibleDC(source_dc);
    temporary_bitmap = CreateCompatibleBitmap(source_dc, (int)width, (int)height);
    previous = SelectObject(temporary_dc, temporary_bitmap);
    BitBlt(temporary_dc, 0, 0, (int)width, (int)height, source_dc, (int)x,
           (int)y, SRCCOPY);
    memset(&information, 0, sizeof(information));
    information.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    information.bmiHeader.biWidth = (LONG)width;
    information.bmiHeader.biHeight = -(LONG)height;
    information.bmiHeader.biPlanes = 1;
    information.bmiHeader.biBitCount = 32;
    information.bmiHeader.biCompression = BI_RGB;
    row_bytes = (size_t)(int)width * 4U;
    image_bytes = row_bytes * (size_t)(int)height;
    pixels = (unsigned char *)malloc(image_bytes);
    if (pixels != NULL &&
        GetDIBits(temporary_dc, temporary_bitmap, 0, (UINT)height, pixels,
                  &information, DIB_RGB_COLORS)) {
        output = fopen(filename, "wb");
        if (output != NULL) {
            memset(&file_header, 0, sizeof(file_header));
            file_header.bfType = 0x4d42;
            file_header.bfOffBits = sizeof(file_header) + sizeof(BITMAPINFOHEADER);
            file_header.bfSize = file_header.bfOffBits + (DWORD)image_bytes;
            fwrite(&file_header, sizeof(file_header), 1U, output);
            fwrite(&information.bmiHeader, sizeof(BITMAPINFOHEADER), 1U, output);
            fwrite(pixels, image_bytes, 1U, output);
            fclose(output);
        }
    }
    free(pixels);
    SelectObject(temporary_dc, previous);
    DeleteObject(temporary_bitmap);
    DeleteDC(temporary_dc);
}

void bb_graphics_get(double x1, double y1, double x2, double y2,
                     BbNumArray *storage)
{
    BbCaptureSlot *capture = NULL;
    int width = abs((int)x2 - (int)x1) + 1;
    int height = abs((int)y2 - (int)y1) + 1;
    HDC reference;
    for (int index = 0; index < BB_GUI_BITMAPS; ++index) {
        if (captures[index].key == storage || captures[index].key == NULL) {
            capture = &captures[index];
            break;
        }
    }
    if (capture == NULL) return;
    if (capture->image.dc != NULL) {
        SelectObject(capture->image.dc, capture->image.previous);
        DeleteObject(capture->image.bitmap);
        DeleteDC(capture->image.dc);
    }
    reference = GetDC(main_window);
    capture->image.dc = CreateCompatibleDC(reference);
    capture->image.bitmap = CreateCompatibleBitmap(reference, width, height);
    capture->image.previous =
        SelectObject(capture->image.dc, capture->image.bitmap);
    capture->image.width = width;
    capture->image.height = height;
    capture->key = storage;
    BitBlt(capture->image.dc, 0, 0, width, height, selected_dc, (int)x1,
           (int)y1, SRCCOPY);
    ReleaseDC(main_window, reference);
}

void bb_graphics_put(double x, double y, BbNumArray *storage,
                     double operation)
{
    for (int index = 0; index < BB_GUI_BITMAPS; ++index) {
        if (captures[index].key == storage && captures[index].image.dc != NULL) {
            BitBlt(selected_dc, (int)x, (int)y, captures[index].image.width,
                   captures[index].image.height, captures[index].image.dc, 0, 0,
                   raster_operation(operation));
            refresh();
            return;
        }
    }
}

void bb_select_print(void)
{
    ensure_window();
    selected_dc = printer_dc != NULL ? printer_dc : display_dc;
}

void bb_print_control(double operation, double *result, double from_page,
                      double to_page, double minimum_page,
                      double maximum_page, double copies)
{
    int command = (int)operation;
    if (command == 0 || command == 1) {
        PRINTDLGA dialog;
        DOCINFOA document;
        memset(&dialog, 0, sizeof(dialog));
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = main_window;
        dialog.Flags = PD_RETURNDC | PD_NOSELECTION;
        if (command == 0) dialog.Flags |= PD_RETURNDEFAULT;
        dialog.nFromPage = (WORD)from_page;
        dialog.nToPage = (WORD)to_page;
        dialog.nMinPage = (WORD)minimum_page;
        dialog.nMaxPage = (WORD)maximum_page;
        dialog.nCopies = (WORD)copies;
        if (!PrintDlgA(&dialog)) {
            if (result != NULL) *result = 2.0;
            return;
        }
        printer_dc = dialog.hDC;
        memset(&document, 0, sizeof(document));
        document.cbSize = sizeof(document);
        document.lpszDocName = "Modern BasicBasic";
        if (StartDocA(printer_dc, &document) > 0) (void)StartPage(printer_dc);
        if (result != NULL) *result = 1.0;
    } else if (command == 2 && printer_dc != NULL) {
        (void)EndPage(printer_dc);
    } else if (command == 3 && printer_dc != NULL) {
        (void)EndDoc(printer_dc);
        DeleteDC(printer_dc);
        printer_dc = NULL;
        selected_dc = display_dc;
    }
}

void bb_main_menu(const char *one, const char *two, const char *three,
                  const char *four, const char *five, const char *six)
{
    const char *names[6] = {one, two, three, four, five, six};
    ensure_window();
    if (menu_bar != NULL) DestroyMenu(menu_bar);
    menu_bar = CreateMenu();
    for (int index = 0; index < 6; ++index) {
        submenus[index] = CreatePopupMenu();
        if (names[index] != NULL && names[index][0] != '\0')
            AppendMenuA(menu_bar, MF_POPUP, (UINT_PTR)submenus[index], names[index]);
    }
    SetMenu(main_window, menu_bar);
    DrawMenuBar(main_window);
}

void bb_add_submenu(double menu, const char *text, double identifier)
{
    int slot = (int)menu - 1;
    if (slot < 0 || slot >= 6 || submenus[slot] == NULL) return;
    if (identifier == 0.0 || text == NULL || text[0] == '\0')
        AppendMenuA(submenus[slot], MF_SEPARATOR, 0, NULL);
    else
        AppendMenuA(submenus[slot], MF_STRING, (UINT_PTR)(int)identifier, text);
}

void bb_menu_item_state(double identifier, double enabled)
{
    if (menu_bar != NULL)
        EnableMenuItem(menu_bar, (UINT)(int)identifier,
                       MF_BYCOMMAND | (enabled != 0.0 ? MF_ENABLED : MF_GRAYED));
}

void bb_message_box(const char *message, const char *title, double flags)
{
    int result;
    ensure_window();
    result = MessageBoxA(main_window, message != NULL ? message : "",
                         title != NULL ? title : "", (UINT)flags);
    (void)snprintf(dialog_values[6], BB_STRING_CAPACITY, "%d",
                   result == IDYES || result == IDOK ? 1 : 0);
}

void bb_open_file_dialog(const char *filter, const char *filename,
                         const char *directory, const char *title,
                         double save_dialog)
{
    OPENFILENAMEA dialog;
    char path[MAX_PATH] = "";
    static const char fallback_filter[] = "All Files\0*.*\0\0";
    char decoded_filter[BB_STRING_CAPACITY];
    BOOL success;
    ensure_window();
    if (filename != NULL) bb_set_string(path, sizeof(path), filename);
    memset(&dialog, 0, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = main_window;
    if (filter != NULL && filter[0] != '\0') {
        size_t length = strlen(filter);
        if (length + 2U >= sizeof(decoded_filter))
            length = sizeof(decoded_filter) - 2U;
        for (size_t index = 0U; index < length; ++index)
            decoded_filter[index] = filter[index] == BB_NUL_SENTINEL
                                        ? '\0' : filter[index];
        decoded_filter[length] = '\0';
        decoded_filter[length + 1U] = '\0';
        dialog.lpstrFilter = decoded_filter;
    } else {
        dialog.lpstrFilter = fallback_filter;
    }
    dialog.lpstrFile = path;
    dialog.nMaxFile = sizeof(path);
    dialog.lpstrInitialDir = directory != NULL && directory[0] != '\0' ? directory : NULL;
    dialog.lpstrTitle = title;
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY |
                   (save_dialog != 0.0 ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    success = save_dialog != 0.0 ? GetSaveFileNameA(&dialog) : GetOpenFileNameA(&dialog);
    bb_set_string(dialog_values[100], BB_STRING_CAPACITY, success ? path : "");
    bb_set_string(dialog_values[102], BB_STRING_CAPACITY, success ? "1" : "0");
}

const char *bb_dialog_value(double index)
{
    int slot = (int)index;
    if (slot < 0 || slot >= BB_GUI_DIALOG_VALUES) return "";
    return dialog_values[slot];
}

const char *bb_directory(const char *pattern, double attributes)
{
    static char result[MAX_PATH];
    BOOL found;
    result[0] = '\0';
    if (pattern != NULL) {
        if (directory_search != INVALID_HANDLE_VALUE)
            FindClose(directory_search);
        directory_attributes = (int)attributes;
        directory_search = FindFirstFileA(pattern, &directory_data);
        found = directory_search != INVALID_HANDLE_VALUE;
    } else {
        if (directory_search == INVALID_HANDLE_VALUE) return result;
        found = FindNextFileA(directory_search, &directory_data);
    }
    while (found) {
        bool is_directory =
            (directory_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        bool want_directory = (directory_attributes & 0x10) != 0 ||
                              directory_attributes == 5;
        if ((want_directory && is_directory) ||
            (!want_directory && !is_directory)) {
            bb_set_string(result, sizeof(result), directory_data.cFileName);
            return result;
        }
        found = FindNextFileA(directory_search, &directory_data);
    }
    FindClose(directory_search);
    directory_search = INVALID_HANDLE_VALUE;
    return result;
}

void bb_custom_dialog(BbStringArray *controls, double count, double x,
                      double y, double width, double height,
                      const char *title)
{
    int identifiers[128];
    int button_ids[128];
    int control_count = 0;
    int button_count = 0;
    int chosen = -1;
    int filename_id = -1;
    int filepath_id = -1;
    int filelist_id = -1;
    int previous_mode;
    char key[8];
    ensure_window();
    if (controls == NULL) return;
    /* Discard input belonging to the owner before the popup becomes visible.
       Once visible, every dialog notification must be retained. */
    key_read = key_write;
    if (!create_dialog_window(x, y, width, height, title)) return;
    previous_mode = screen_mode;
    screen_mode = 1000;
    for (int index = 0; index < (int)count && index < 128; ++index) {
        char descriptor[BB_STRING_CAPACITY];
        char *fields[8] = {0};
        int field_count = 0;
        char *cursor;
        const char *source =
            bb_string_array_get(controls, 1U, (double)index);
        bb_set_string(descriptor, sizeof(descriptor), source);
        cursor = descriptor;
        while (field_count < 8) {
            char *comma;
            fields[field_count++] = cursor;
            comma = strchr(cursor, ',');
            if (comma == NULL) break;
            *comma = '\0';
            cursor = comma + 1;
        }
        if (field_count < 7) continue;
        {
            int identifier = atoi(fields[5]);
            double special = field_count >= 8 ? strtod(fields[7], NULL) : 0.0;
            bb_control(fields[6], (double)identifier, 0.0, fields[0], special,
                       strtod(fields[1], NULL), strtod(fields[2], NULL),
                       strtod(fields[3], NULL), strtod(fields[4], NULL), 0.0,
                       7.0);
            identifiers[control_count++] = identifier;
            dialog_values[identifier][0] = '\0';
            if (_stricmp(fields[0], "pushbutton") == 0 ||
                _stricmp(fields[0], "ok") == 0)
                button_ids[button_count++] = identifier;
            if (_stricmp(fields[0], "filename") == 0) filename_id = identifier;
            if (_stricmp(fields[0], "filepath") == 0) {
                char directory[MAX_PATH];
                filepath_id = identifier;
                if (GetCurrentDirectoryA(sizeof(directory), directory) > 0)
                    bb_set_control_text((double)identifier, directory);
            }
            if (_stricmp(fields[0], "filelist") == 0) {
                WIN32_FIND_DATAA data;
                HANDLE search;
                HWND list = GetDlgItem(dialog_window, identifier);
                filelist_id = identifier;
                search = FindFirstFileA(fields[6][0] != '\0' ? fields[6] : "*.*",
                                        &data);
                if (search != INVALID_HANDLE_VALUE) {
                    do {
                        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                            SendMessageA(list, LB_ADDSTRING, 0,
                                         (LPARAM)data.cFileName);
                    } while (FindNextFileA(search, &data));
                    FindClose(search);
                }
            }
            if (special != 0.0 &&
                (_stricmp(fields[0], "radiobutton") == 0 ||
                 _stricmp(fields[0], "checkbox") == 0))
                bb_radio((double)identifier, 1.0);
            if (_stricmp(fields[0], "group") == 0) {
                HWND group = GetDlgItem(dialog_window, identifier);
                if (group != NULL)
                    SetWindowPos(group, HWND_BOTTOM, 0, 0, 0, 0,
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
        }
    }
    screen_mode = previous_mode;
    {
        HWND first = GetNextDlgTabItem(dialog_window, NULL, FALSE);
        if (first != NULL) SetFocus(first);
    }
    while (chosen < 0 && dialog_window != NULL && IsWindow(dialog_window)) {
        if (!bb_gui_inkey(key, sizeof(key))) {
            WaitMessage();
            continue;
        }
        if (key[0] == BB_NUL_SENTINEL) {
            int identifier = (unsigned char)key[1];
            if (identifier == 68) break;
            if (identifier == filelist_id) {
                HWND list = GetDlgItem(dialog_window, filelist_id);
                LRESULT selection = SendMessageA(list, LB_GETCURSEL, 0, 0);
                if (selection != LB_ERR && filename_id >= 0) {
                    char filename[MAX_PATH];
                    SendMessageA(list, LB_GETTEXT, (WPARAM)selection,
                                 (LPARAM)filename);
                    bb_set_control_text((double)filename_id, filename);
                }
                if (filepath_id >= 0) {
                    char directory[MAX_PATH];
                    if (GetCurrentDirectoryA(sizeof(directory), directory) > 0)
                        bb_set_control_text((double)filepath_id, directory);
                }
            }
            for (int index = 0; index < button_count; ++index) {
                if (button_ids[index] == identifier) {
                    chosen = identifier;
                    break;
                }
            }
        }
    }
    for (int index = 0; index < control_count; ++index) {
        int identifier = identifiers[index];
        HWND control = GetDlgItem(dialog_window, identifier);
        if (control != NULL) {
            char class_name[32] = "";
            GetClassNameA(control, class_name, sizeof(class_name));
            if (_stricmp(class_name, "EDIT") == 0)
                GetWindowTextA(control, dialog_values[identifier],
                               BB_STRING_CAPACITY);
            else if (_stricmp(class_name, "BUTTON") == 0)
                bb_set_string(dialog_values[identifier], BB_STRING_CAPACITY,
                              identifier == chosen ||
                                      SendMessageA(control, BM_GETCHECK, 0, 0) == BST_CHECKED
                                  ? "1" : "0");
            else if (_stricmp(class_name, "LISTBOX") == 0) {
                LRESULT selection = SendMessageA(control, LB_GETCURSEL, 0, 0);
                if (selection != LB_ERR)
                    SendMessageA(control, LB_GETTEXT, (WPARAM)selection,
                                 (LPARAM)dialog_values[identifier]);
            }
            destroy_control_window(control);
        }
    }
    if (dialog_window != NULL) {
        DestroyWindow(dialog_window);
        dialog_window = NULL;
    }
    EnableWindow(main_window, TRUE);
    SetForegroundWindow(main_window);
}

void bb_sleep(double seconds)
{
    if (seconds < 0.0) seconds = 0.0;
    Sleep((DWORD)(seconds * 1000.0));
}

double bb_mouse_on(void) { ensure_window(); return -1.0; }
double bb_mouse_x(void) { return mouse_x_value; }
double bb_mouse_y(void) { return mouse_y_value; }
double bb_mouse_button(void) { return mouse_button_value; }

double bb_sound_device(double selector)
{
    (void)selector;
    return waveOutGetNumDevs() > 0 ? 1.0 : 0.0;
}

void bb_play_sound(const char *filename, double asynchronous)
{
    PlaySoundA(filename, NULL,
               SND_FILENAME | (asynchronous != 0.0 ? SND_ASYNC : SND_SYNC));
}

#else

#define STUB_VOID(name, signature) void name signature { }
int bb_gui_inkey(char *destination, size_t capacity)
{ (void)destination; (void)capacity; return 0; }
int bb_gui_active(void){return 0;}
void bb_gui_pump(void){}
void bb_gui_input_line(char *a,size_t b,const char*c)
{if(a!=NULL&&b>0U)a[0]='\0';(void)c;}
double bb_gui_system(double a){(void)a;return NAN;}
void bb_gui_print(const char*a){(void)a;}
void bb_gui_newline(void){}
void bb_gui_cls(void){}
STUB_VOID(bb_gui_color, (double a,double b))
STUB_VOID(bb_gui_locate, (double a,double b))
STUB_VOID(bb_scroll_area, (double a,double b,double c,double d))
STUB_VOID(bb_screen, (double mode, double colors))
void bb_window_name(const char*a){(void)a;}
STUB_VOID(bb_window_size_hint, (double a,double b,double c,double d))
void bb_on_paint(void){}
STUB_VOID(bb_position, (double x,double y,double w,double h,double r))
STUB_VOID(bb_graphics_line, (double a,double b,double c,double d,double e,double f))
STUB_VOID(bb_graphics_circle, (double a,double b,double c,double d,double e,double f))
STUB_VOID(bb_graphics_paint, (double a,double b,double c,double d))
STUB_VOID(bb_graphics_pset, (double a,double b,double c))
STUB_VOID(bb_palette, (double a,double b))
void bb_control(const char *a,double b,double c,const char *d,double e,double f,double g,double h,double i,double j,double k)
{ (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;(void)j;(void)k; }
STUB_VOID(bb_delete_control, (double a))
void bb_set_control_text(double a,const char *b){(void)a;(void)b;}
const char *bb_get_control_text(double a){(void)a;return "";}
STUB_VOID(bb_radio, (double a,double b))
double bb_set_focus(double a){return a;}
double bb_get_focus(void){return 0.0;}
double bb_list(double a,double b,double c,const char*d){(void)a;(void)b;(void)c;(void)d;return 0.0;}
void bb_create_font(double a,double b,double c,double d,double e,double f,double g,double h,double i,double j,double k,double l,double m,double n,const char*o)
{(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;(void)j;(void)k;(void)l;(void)m;(void)n;(void)o;}
STUB_VOID(bb_select_font, (double a))
double bb_text_length(const char *a){return (double)strlen(a != NULL ? a : "");}
double bb_font_info(double a){(void)a;return 8.0;}
double bb_device_info(double a){(void)a;return 0.0;}
double bb_bitmap_header(const char*a,BbNumArray*b){(void)a;(void)b;return 0.0;}
double bb_bitmap_colors(const char*a,BbNumArray*b){(void)a;(void)b;return 0.0;}
STUB_VOID(bb_create_bitmap, (double a,double b,double c,double d))
STUB_VOID(bb_select_bitmap, (double a))
void bb_select_display(void){}
void bb_load_bitmap(const char*a,double b,double c,double d,double e,double f,double g,double h,double i,double j,double k)
{(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;(void)j;(void)k;}
STUB_VOID(bb_copy_bits, (double a,double b,double c,double d,double e,double f,double g,double h,double i))
STUB_VOID(bb_stretch_bits, (double a,double b,double c,double d,double e,double f,double g,double h,double i,double j,double k))
void bb_store_bitmap(double a,const char*b,double c,double d,double e,double f,double g,double h)
{(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;}
void bb_graphics_get(double a,double b,double c,double d,BbNumArray*e){(void)a;(void)b;(void)c;(void)d;(void)e;}
void bb_graphics_put(double a,double b,BbNumArray*c,double d){(void)a;(void)b;(void)c;(void)d;}
void bb_select_print(void){}
void bb_print_control(double a,double*b,double c,double d,double e,double f,double g)
{(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;}
void bb_main_menu(const char*a,const char*b,const char*c,const char*d,const char*e,const char*f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;}
void bb_add_submenu(double a,const char*b,double c){(void)a;(void)b;(void)c;}
STUB_VOID(bb_menu_item_state, (double a,double b))
void bb_message_box(const char*a,const char*b,double c){(void)b;(void)c;fprintf(stderr,"%s\n",a != NULL ? a : "");}
void bb_open_file_dialog(const char*a,const char*b,const char*c,const char*d,double e){(void)a;(void)b;(void)c;(void)d;(void)e;}
const char *bb_dialog_value(double a){(void)a;return "";}
const char *bb_directory(const char*a,double b){(void)a;(void)b;return "";}
void bb_custom_dialog(BbStringArray*a,double b,double c,double d,double e,double f,const char*g)
{(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;}
void bb_sleep(double a){(void)a;}
double bb_mouse_on(void){return 0.0;} double bb_mouse_x(void){return 0.0;}
double bb_mouse_y(void){return 0.0;} double bb_mouse_button(void){return 0.0;}
double bb_sound_device(double a){(void)a;return 0.0;}
void bb_play_sound(const char*a,double b){(void)a;(void)b;}
#undef STUB_VOID

#endif
