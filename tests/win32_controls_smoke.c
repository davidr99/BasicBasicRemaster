#include "bbasic_runtime.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>

static volatile LONG control_checks;

static DWORD WINAPI activate_control(LPVOID unused)
{
    HWND window = NULL;
    HWND button = NULL;
    RECT client;
    RECT button_rectangle;
    unsigned attempts = 0U;
    (void)unused;
    while (button == NULL && attempts++ < 500U) {
        Sleep(10U);
        window = FindWindowA("ModernBasicBasicWindow", "Control smoke");
        if (window != NULL) button = GetDlgItem(window, 1068);
    }
    if (button == NULL) return 1U;
    GetClientRect(window, &client);
    GetWindowRect(button, &button_rectangle);
    MapWindowPoints(NULL, window, (POINT *)&button_rectangle, 2U);
    if (client.right != 136 || client.bottom != 112 ||
        button_rectangle.left != 88 || button_rectangle.top != 0 ||
        button_rectangle.right - button_rectangle.left != 48 ||
        button_rectangle.bottom - button_rectangle.top != 16 ||
        SendMessageA(button, WM_GETFONT, 0, 0) == 0)
        return 2U;
    InterlockedExchange(&control_checks, 1L);
    SendMessageA(button, BM_CLICK, 0, 0);
    return 0U;
}

int main(void)
{
    HANDLE sender;
    DWORD sender_result = 99U;
    char key[8] = "";
    FILE *marker;

    bb_window_name("Control smoke");
    bb_window_size_hint(29.0, 9.0, 45.0, 15.0);
    bb_gui_cls();
    bb_control("Exit", 1068.0, 0.0, "Push", 0.0,
               40.0, 9.0, 6.0, 1.0, 7.0, 4.0);
    sender = CreateThread(NULL, 0U, activate_control, NULL, 0U, NULL);
    if (sender == NULL) return 3;
    for (unsigned attempts = 0U; attempts < 500U; ++attempts) {
        if (bb_gui_inkey(key, sizeof(key))) break;
        Sleep(10U);
    }
    WaitForSingleObject(sender, INFINITE);
    GetExitCodeThread(sender, &sender_result);
    CloseHandle(sender);
    if (control_checks != 1L || sender_result != 0U) return 4;
    if ((unsigned char)key[0] != (unsigned char)BB_NUL_SENTINEL ||
        (unsigned char)key[1] != 68U)
        return 5;

    marker = fopen("win32-controls-smoke.ok", "w");
    if (marker == NULL) return 6;
    fputs("PASS\n", marker);
    fclose(marker);
    return 0;
}
