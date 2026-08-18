#include "bbasic_runtime.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>

static COLORREF captured_pixel(HWND window, int x, int y)
{
    RECT client;
    HDC window_dc;
    HDC memory_dc;
    HBITMAP bitmap;
    HGDIOBJ previous;
    COLORREF result;
    GetClientRect(window, &client);
    window_dc = GetDC(window);
    memory_dc = CreateCompatibleDC(window_dc);
    bitmap = CreateCompatibleBitmap(window_dc, client.right, client.bottom);
    previous = SelectObject(memory_dc, bitmap);
    PrintWindow(window, memory_dc, PW_CLIENTONLY);
    result = GetPixel(memory_dc, x, y);
    SelectObject(memory_dc, previous);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(window, window_dc);
    return result;
}

int main(void)
{
    static const double starts[4] = {-0.01, -4.7124, -3.1416, -1.5708};
    static const double ends[4] = {-1.5708, -0.01, -4.7124, -3.1416};
    static const double fill_x[4] = {152.0, 152.0, 148.0, 148.0};
    static const double fill_y[4] = {38.0, 42.0, 42.0, 38.0};
    HWND window;
    FILE *marker;

    bb_window_name("BasicBasic pie smoke");
    bb_screen(1000.0, 256.0);
    bb_position(0.0, 0.0, 220.0, 80.0, 0.0);
    window = FindWindowA("ModernBasicBasicWindow", "BasicBasic pie smoke");
    if (window == NULL) return 2;

    for (int quadrant = 0; quadrant < 4; ++quadrant) {
        bb_graphics_line(0.0, 0.0, 220.0, 80.0, 4.0, 2.0);
        bb_graphics_circle(150.0, 40.0, 10.0, 12.0, 0.0, 0.0);
        bb_graphics_paint(150.0, 40.0, 12.0, 12.0);
        bb_graphics_circle(150.0, 40.0, 10.0, 7.0,
                           starts[quadrant], ends[quadrant]);
        bb_graphics_paint(fill_x[quadrant], fill_y[quadrant], 7.0, 7.0);
        if (captured_pixel(window, 5, 70) != RGB(128, 0, 0)) return 3;
    }

    marker = fopen("win32-pie-smoke.ok", "w");
    if (marker == NULL) return 4;
    fputs("PASS\n", marker);
    fclose(marker);
    return 0;
}
