#include "bbasic_runtime.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <string.h>

static volatile LONG dialog_checks;

static DWORD WINAPI inspect_dialog(LPVOID unused)
{
    HWND dialog = NULL;
    HWND owner;
    HWND label;
    HWND filename;
    HWND filelist;
    HWND button;
    char selected[MAX_PATH] = "";
    unsigned attempts = 0U;
    (void)unused;
    while (dialog == NULL && attempts++ < 500U) {
        Sleep(10U);
        dialog = FindWindowA("ModernBasicBasicDialog", "Dialog smoke");
    }
    if (dialog == NULL) return 1U;

    owner = GetWindow(dialog, GW_OWNER);
    label = GetDlgItem(dialog, 100);
    filename = GetDlgItem(dialog, 151);
    filelist = GetDlgItem(dialog, 152);
    button = GetDlgItem(dialog, 104);
    if (owner == NULL || IsWindowEnabled(owner) || label == NULL ||
        filename == NULL || filelist == NULL || button == NULL ||
        GetParent(label) != dialog ||
        SendMessageA(label, WM_GETFONT, 0, 0) == 0 ||
        (HBRUSH)GetClassLongPtrA(dialog, GCLP_HBRBACKGROUND) !=
            GetSysColorBrush(COLOR_3DFACE))
        return 2U;

    if (SendMessageA(filelist, LB_GETCOUNT, 0, 0) < 1) return 3U;
    SendMessageA(filelist, LB_SETCURSEL, 0, 0);
    PostMessageA(dialog, WM_COMMAND, MAKEWPARAM(152, LBN_SELCHANGE),
                 (LPARAM)filelist);
    for (attempts = 0U; attempts < 200U && selected[0] == '\0'; ++attempts) {
        Sleep(10U);
        GetWindowTextA(filename, selected, sizeof(selected));
    }
    if (_stricmp(selected, "dialog-smoke.bmp") != 0) return 4U;

    InterlockedExchange(&dialog_checks, 1L);
    PostMessageA(dialog, WM_COMMAND, MAKEWPARAM(104, BN_CLICKED),
                 (LPARAM)button);
    return 0U;
}

int main(void)
{
    BbStringArray controls = {0};
    HANDLE inspector;
    DWORD inspector_result = 99U;
    FILE *marker;
    FILE *dummy_bitmap;

    bb_screen(1000.0, 256.0);
    dummy_bitmap = fopen("dialog-smoke.bmp", "wb");
    if (dummy_bitmap == NULL) return 2;
    fputs("BM", dummy_bitmap);
    fclose(dummy_bitmap);

    bb_string_array_dim(&controls, 1U, 3.0);
    bb_string_array_set(&controls, "ltext,10,10,100,30,100,Name:",
                        1U, 0.0);
    bb_string_array_set(&controls, "filename,10,40,140,25,151,",
                        1U, 1.0);
    bb_string_array_set(&controls, "filelist,10,70,140,60,152,*.bmp,0",
                        1U, 2.0);
    bb_string_array_set(&controls, "ok,10,140,70,25,104,Done",
                        1U, 3.0);

    inspector = CreateThread(NULL, 0U, inspect_dialog, NULL, 0U, NULL);
    if (inspector == NULL) return 3;
    bb_custom_dialog(&controls, 4.0, 10.0, 10.0, 180.0, 180.0,
                     "Dialog smoke");
    WaitForSingleObject(inspector, INFINITE);
    GetExitCodeThread(inspector, &inspector_result);
    CloseHandle(inspector);

    remove("dialog-smoke.bmp");
    if (dialog_checks != 1L || inspector_result != 0U) return 5;
    if (strcmp(bb_dialog_value(104.0), "1") != 0) return 6;
    if (strcmp(bb_dialog_value(151.0), "dialog-smoke.bmp") != 0) return 7;

    marker = fopen("win32-dialog-smoke.ok", "w");
    if (marker == NULL) return 8;
    fputs("PASS\n", marker);
    fclose(marker);
    return 0;
}
