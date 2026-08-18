#include "bbasic_runtime.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <string.h>

static DWORD WINAPI send_input(LPVOID unused)
{
    HWND window;
    (void)unused;
    do {
        Sleep(10U);
        window = FindWindowA("ModernBasicBasicWindow", NULL);
    } while (window == NULL);
    PostMessageA(window, WM_CHAR, (WPARAM)'4', 0);
    PostMessageA(window, WM_CHAR, (WPARAM)'2', 0);
    PostMessageA(window, WM_CHAR, (WPARAM)'\r', 0);
    return 0U;
}

int main(void)
{
    HANDLE sender;
    FILE *marker;
    const char *value;
    bb_screen(1000.0, 256.0);
    sender = CreateThread(NULL, 0U, send_input, NULL, 0U, NULL);
    if (sender == NULL) return 2;
    bb_input_begin("value: ");
    value = bb_input_next();
    WaitForSingleObject(sender, INFINITE);
    CloseHandle(sender);
    if (strcmp(value, "42") != 0) return 3;
    marker = fopen("win32-input-smoke.ok", "w");
    if (marker == NULL) return 4;
    fputs("PASS\n", marker);
    fclose(marker);
    return 0;
}
