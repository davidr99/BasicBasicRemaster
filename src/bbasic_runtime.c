#include "bbasic_runtime.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <conio.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

#define BB_TEMPORARY_COUNT 16
#define BB_MAX_FILES 256
#define BB_MAX_FIELDS 64

typedef struct BbFieldBinding {
    char *destination;
    size_t capacity;
    size_t width;
} BbFieldBinding;

typedef struct BbOpenFile {
    FILE *stream;
#ifdef _WIN32
    HANDLE serial;
#endif
    size_t record_length;
    int mode;
    int eof_flag;
    BbFieldBinding fields[BB_MAX_FIELDS];
    size_t field_count;
} BbOpenFile;

static char temporary_strings[BB_TEMPORARY_COUNT][BB_STRING_CAPACITY];
static unsigned temporary_index;
static char input_line[BB_STRING_CAPACITY];
static char *input_cursor;
static BbOpenFile open_files[BB_MAX_FILES];
static int configured_error_level = 7;

static char *next_temporary(void)
{
    char *result = temporary_strings[temporary_index];
    temporary_index = (temporary_index + 1U) % BB_TEMPORARY_COUNT;
    result[0] = '\0';
    return result;
}

double bb_ostype(void)
{
#ifdef _WIN32
    return 2.0;
#else
    return 1.0;
#endif
}

double bb_system(double selector)
{
    int code = (int)selector;
    double gui_value = bb_gui_system(selector);
    if (!isnan(gui_value)) return gui_value;
#ifdef _WIN32
    HWND window = GetConsoleWindow();
    RECT rectangle = {0, 0, 640, 480};
    CONSOLE_SCREEN_BUFFER_INFO console_info;
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (window != NULL) (void)GetWindowRect(window, &rectangle);
    switch (code) {
    case 1:
        if (GetConsoleScreenBufferInfo(output, &console_info))
            return (double)(console_info.srWindow.Right - console_info.srWindow.Left);
        return 639.0;
    case 2:
        if (GetConsoleScreenBufferInfo(output, &console_info))
            return (double)(console_info.srWindow.Bottom - console_info.srWindow.Top);
        return 479.0;
    case 3: return (double)(GetSystemMetrics(SM_CXSCREEN) - 1);
    case 4: return (double)(GetSystemMetrics(SM_CYSCREEN) - 1);
    case 5: return 255.0;
    case 6: return 15.0;
    case 7: return 1000.0;
    case 8: return (double)rectangle.left;
    case 9: return (double)rectangle.top;
    case 10: return (double)(rectangle.right - rectangle.left);
    case 11: return (double)(rectangle.bottom - rectangle.top);
    case 12: return window == GetForegroundWindow() ? 1.0 : 0.0;
    case 13: return 0.0;
    case 14: return 0.0;
    case 15: return 4.0;
    case 16: return 0.0;
    case 17: {
        HDC device = GetDC(window);
        int colors = device != NULL ? GetDeviceCaps(device, SIZEPALETTE) : 0;
        if (device != NULL) (void)ReleaseDC(window, device);
        return colors > 0 ? (double)colors : 256.0;
    }
    case 18: return 0.0;
    case 19: return window != NULL && IsIconic(window) ? 1.0 : 0.0;
    default: return 0.0;
    }
#else
    switch (code) {
    case 1: return 79.0;
    case 2: return 24.0;
    case 3: return 79.0;
    case 4: return 24.0;
    case 5: return 255.0;
    case 6: return 15.0;
    case 7: return 0.0;
    case 12: return 1.0;
    case 17: return 256.0;
    default: return 0.0;
    }
#endif
}

const char *bb_inkey(void)
{
    char *result = next_temporary();
    if (bb_gui_inkey(result, BB_STRING_CAPACITY)) return result;
#ifdef _WIN32
    if (_kbhit()) {
        int value = _getch();
        if (value == 0 || value == 224) {
            int extended = _getch();
            result[0] = BB_NUL_SENTINEL;
            result[1] = (char)extended;
            result[2] = '\0';
        } else {
            result[0] = (char)value;
            result[1] = '\0';
        }
    }
#else
    struct timeval timeout = {0, 0};
    fd_set input;
    struct termios old_settings;
    struct termios raw_settings;

    FD_ZERO(&input);
    FD_SET(STDIN_FILENO, &input);
    if (tcgetattr(STDIN_FILENO, &old_settings) == 0) {
        raw_settings = old_settings;
        raw_settings.c_lflag &= (tcflag_t)~(ICANON | ECHO);
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &raw_settings);
        if (select(STDIN_FILENO + 1, &input, NULL, NULL, &timeout) > 0) {
            ssize_t ignored = read(STDIN_FILENO, result, 1);
            (void)ignored;
        }
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &old_settings);
    }
#endif
    return result;
}

double bb_len(const char *value)
{
    return (double)strlen(value != NULL ? value : "");
}

double bb_asc(const char *value)
{
    if (value == NULL || value[0] == '\0') {
        return 0.0;
    }
    return value[0] == BB_NUL_SENTINEL ? 0.0
                                       : (double)(unsigned char)value[0];
}

const char *bb_chr(double value)
{
    char *result = next_temporary();
    result[0] = ((int)value & 0xff) == 0
                    ? BB_NUL_SENTINEL
                    : (char)(unsigned char)((int)value & 0xff);
    result[1] = '\0';
    return result;
}

static size_t bounded_count(double count, size_t maximum)
{
    if (!isfinite(count) || count <= 0.0) return 0U;
    if (count >= (double)maximum) return maximum;
    return (size_t)count;
}

const char *bb_left(const char *value, double count)
{
    char *result = next_temporary();
    const char *source = value != NULL ? value : "";
    size_t length = bounded_count(count, strlen(source));
    memcpy(result, source, length);
    result[length] = '\0';
    return result;
}

const char *bb_right(const char *value, double count)
{
    char *result = next_temporary();
    const char *source = value != NULL ? value : "";
    size_t source_length = strlen(source);
    size_t length = bounded_count(count, source_length);
    memcpy(result, source + source_length - length, length);
    result[length] = '\0';
    return result;
}

const char *bb_mid(const char *value, double start, double count)
{
    char *result = next_temporary();
    const char *source = value != NULL ? value : "";
    size_t source_length = strlen(source);
    size_t offset = start <= 1.0 ? 0U : (size_t)(start - 1.0);
    size_t length;
    if (offset >= source_length) return result;
    length = bounded_count(count, source_length - offset);
    memcpy(result, source + offset, length);
    result[length] = '\0';
    return result;
}

const char *bb_str(double value)
{
    char *result = next_temporary();
    if (isfinite(value) && value == floor(value))
        (void)snprintf(result, BB_STRING_CAPACITY, " %.0f", value);
    else
        (void)snprintf(result, BB_STRING_CAPACITY, " %g", value);
    return result;
}

const char *bb_space(double count)
{
    char *result = next_temporary();
    size_t length = bounded_count(count, BB_STRING_CAPACITY - 1U);
    memset(result, ' ', length);
    result[length] = '\0';
    return result;
}

const char *bb_ucase(const char *value)
{
    char *result = next_temporary();
    const unsigned char *source = (const unsigned char *)(value != NULL ? value : "");
    size_t index = 0U;
    while (source[index] != '\0' && index + 1U < BB_STRING_CAPACITY) {
        result[index] = (char)toupper(source[index]);
        ++index;
    }
    result[index] = '\0';
    return result;
}

const char *bb_format_using(const char *format, double value)
{
    char *result = next_temporary();
    char number[256];
    char grouped[256];
    const char *pattern = format != NULL ? format : "";
    const char *first = strchr(pattern, '#');
    const char *last;
    const char *decimal;
    int decimals = 0;
    bool commas = false;
    if (first == NULL) {
        (void)snprintf(result, BB_STRING_CAPACITY, "%s", pattern);
        return result;
    }
    last = first;
    while (*last == '#' || *last == ',' || *last == '.') {
        if (*last == ',') commas = true;
        ++last;
    }
    decimal = strchr(first, '.');
    if (decimal != NULL && decimal < last) {
        const char *cursor = decimal + 1;
        while (cursor < last && *cursor == '#') {
            ++decimals;
            ++cursor;
        }
    }
    (void)snprintf(number, sizeof(number), "%.*f", decimals, value);
    if (commas) {
        const char *dot = strchr(number, '.');
        size_t integer_length = dot != NULL ? (size_t)(dot - number) : strlen(number);
        size_t source = 0U, destination = 0U;
        bool negative = number[0] == '-';
        if (negative) grouped[destination++] = number[source++];
        for (; source < integer_length && destination + 2U < sizeof(grouped);
             ++source) {
            size_t remaining = integer_length - source;
            if (source > (negative ? 1U : 0U) && remaining % 3U == 0U)
                grouped[destination++] = ',';
            grouped[destination++] = number[source];
        }
        while (number[source] != '\0' && destination + 1U < sizeof(grouped))
            grouped[destination++] = number[source++];
        grouped[destination] = '\0';
    } else {
        bb_set_string(grouped, sizeof(grouped), number);
    }
    (void)snprintf(result, BB_STRING_CAPACITY, "%.*s%s%s",
                   (int)(first - pattern), pattern, grouped, last);
    return result;
}

double bb_instr(double start, const char *haystack, const char *needle)
{
    const char *source = haystack != NULL ? haystack : "";
    const char *wanted = needle != NULL ? needle : "";
    size_t offset = start <= 1.0 ? 0U : (size_t)(start - 1.0);
    const char *found;
    if (offset > strlen(source)) return 0.0;
    found = strstr(source + offset, wanted);
    return found != NULL ? (double)(found - source + 1) : 0.0;
}

double bb_int(double value)
{
    return floor(value);
}

double bb_abs(double value)
{
    return fabs(value);
}

double bb_val(const char *value)
{
    return value != NULL ? strtod(value, NULL) : 0.0;
}

double bb_freemem(void)
{
#ifdef _WIN32
    MEMORYSTATUSEX status;
    memset(&status, 0, sizeof(status));
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        return (double)status.ullAvailPhys;
    }
    return 0.0;
#else
    long pages = sysconf(_SC_AVPHYS_PAGES);
    long page_size = sysconf(_SC_PAGESIZE);
    if (pages < 0 || page_size < 0) return 0.0;
    return (double)pages * (double)page_size;
#endif
}

double bb_rnd(void)
{
    return (double)rand() / ((double)RAND_MAX + 1.0);
}

double bb_timer(void)
{
#ifdef _WIN32
    SYSTEMTIME local_time;
    bb_gui_pump();
    GetLocalTime(&local_time);
    return (double)(local_time.wHour * 3600 + local_time.wMinute * 60 +
                    local_time.wSecond) +
           (double)local_time.wMilliseconds / 1000.0;
#else
    time_t now = time(NULL);
    struct tm local_time;
    if (localtime_r(&now, &local_time) == NULL) return 0.0;
    return (double)(local_time.tm_hour * 3600 + local_time.tm_min * 60 +
                    local_time.tm_sec);
#endif
}

const char *bb_time_string(void)
{
    char *result = next_temporary();
    time_t now = time(NULL);
    struct tm local_time;
#ifdef _WIN32
    if (localtime_s(&local_time, &now) != 0) return result;
#else
    if (localtime_r(&now, &local_time) == NULL) return result;
#endif
    (void)strftime(result, BB_STRING_CAPACITY, "%H:%M:%S", &local_time);
    return result;
}

const char *bb_date_string(void)
{
    char *result = next_temporary();
    time_t now = time(NULL);
    struct tm local_time;
#ifdef _WIN32
    if (localtime_s(&local_time, &now) != 0) return result;
#else
    if (localtime_r(&now, &local_time) == NULL) return result;
#endif
    (void)strftime(result, BB_STRING_CAPACITY, "%m-%d-%Y", &local_time);
    return result;
}

const char *bb_concat(const char *left, const char *right)
{
    char *result = next_temporary();
    const char *safe_left = left != NULL ? left : "";
    const char *safe_right = right != NULL ? right : "";
    (void)snprintf(result, BB_STRING_CAPACITY, "%s%s", safe_left, safe_right);
    return result;
}

void bb_set_string(char *destination, size_t capacity, const char *value)
{
    if (destination == NULL || capacity == 0U) {
        return;
    }
    (void)snprintf(destination, capacity, "%s", value != NULL ? value : "");
}

void bb_print_string(const char *value)
{
    if (bb_gui_active()) {
        bb_gui_print(value != NULL ? value : "");
        return;
    }
    fputs(value != NULL ? value : "", stdout);
}

void bb_print_number(double value)
{
    char formatted[128];
    if (bb_gui_active()) {
        if (isfinite(value) && value == floor(value))
            (void)snprintf(formatted, sizeof(formatted), " %.0f ", value);
        else
            (void)snprintf(formatted, sizeof(formatted), " %g ", value);
        bb_gui_print(formatted);
        return;
    }
    if (isfinite(value) && value == floor(value)) {
        printf(" %.0f ", value);
    } else {
        printf(" %g ", value);
    }
}

void bb_print_newline(void)
{
    if (bb_gui_active()) {
        bb_gui_newline();
        return;
    }
    putchar('\n');
    fflush(stdout);
}

void bb_cls(void)
{
    if (bb_gui_active()) {
        bb_gui_cls();
        return;
    }
#ifdef _WIN32
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    DWORD written;
    COORD origin = {0, 0};
    if (output != INVALID_HANDLE_VALUE &&
        GetConsoleScreenBufferInfo(output, &info)) {
        DWORD cells = (DWORD)info.dwSize.X * (DWORD)info.dwSize.Y;
        (void)FillConsoleOutputCharacterA(output, ' ', cells, origin, &written);
        (void)FillConsoleOutputAttribute(output, info.wAttributes, cells, origin,
                                         &written);
        (void)SetConsoleCursorPosition(output, origin);
    }
#else
    fputs("\033[2J\033[H", stdout);
    fflush(stdout);
#endif
}

void bb_color(double foreground, double background)
{
    int foreground_value = (int)foreground & 15;
    int background_value = (int)background & 15;
    if (bb_gui_active()) {
        bb_gui_color(foreground, background);
        return;
    }
#ifdef _WIN32
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    (void)SetConsoleTextAttribute(
        output, (WORD)(foreground_value | (background_value << 4)));
#else
    static const int ansi_colors[8] = {30, 34, 32, 36, 31, 35, 33, 37};
    int bright = foreground_value >= 8 ? 60 : 0;
    int foreground_code = ansi_colors[foreground_value & 7] + bright;
    int background_code = ansi_colors[background_value & 7] + 10;
    printf("\033[%d;%dm", foreground_code, background_code);
#endif
}

void bb_locate(double row, double column)
{
    int y = (int)row - 1;
    int x = (int)column - 1;
    if (bb_gui_active()) {
        bb_gui_locate(row, column);
        return;
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;
#ifdef _WIN32
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD position = {(SHORT)x, (SHORT)y};
    (void)SetConsoleCursorPosition(output, position);
#else
    printf("\033[%d;%dH", y + 1, x + 1);
#endif
}

void bb_beep(void)
{
    putchar('\a');
    fflush(stdout);
}

void bb_randomize(double seed)
{
    unsigned value = seed == 0.0 ? (unsigned)time(NULL) : (unsigned)seed;
    srand(value);
}

void bb_input_begin(const char *prompt)
{
    if (bb_gui_active()) {
        bb_gui_input_line(input_line, sizeof(input_line), prompt);
        input_cursor = input_line;
        return;
    }
    if (prompt != NULL) fputs(prompt, stdout);
    fflush(stdout);
    if (fgets(input_line, sizeof(input_line), stdin) == NULL)
        input_line[0] = '\0';
    input_cursor = input_line;
}

const char *bb_input_next(void)
{
    char *result = next_temporary();
    char *start;
    char *end;
    size_t length;
    if (input_cursor == NULL) return result;
    while (isspace((unsigned char)*input_cursor) && *input_cursor != '\n')
        ++input_cursor;
    start = input_cursor;
    if (*start == '"') {
        ++start;
        end = start;
        while (*end != '\0' && *end != '"') ++end;
        input_cursor = *end == '"' ? end + 1 : end;
    } else {
        end = start;
        while (*end != '\0' && *end != ',' && *end != '\n' && *end != '\r')
            ++end;
        input_cursor = end;
        while (end > start && isspace((unsigned char)end[-1])) --end;
    }
    length = (size_t)(end - start);
    if (length >= BB_STRING_CAPACITY) length = BB_STRING_CAPACITY - 1U;
    memcpy(result, start, length);
    result[length] = '\0';
    while (*input_cursor != '\0' && *input_cursor != ',') ++input_cursor;
    if (*input_cursor == ',') ++input_cursor;
    return result;
}

static BbOpenFile *file_slot(double file_number)
{
    int number = (int)file_number;
    if (number < 0 || number >= BB_MAX_FILES) return NULL;
    return &open_files[number];
}

#ifdef _WIN32
static void configure_serial(BbOpenFile *file, const char *settings)
{
    DCB configuration;
    COMMTIMEOUTS timeouts;
    unsigned long baud = 9600;
    unsigned data_bits = 8;
    double stop_bits = 1.0;
    char parity = 'N';
    if (file == NULL || file->serial == NULL ||
        file->serial == INVALID_HANDLE_VALUE)
        return;
    if (settings != NULL) {
        while (*settings == ':' || *settings == ',' || isspace((unsigned char)*settings))
            ++settings;
        (void)sscanf(settings, "%lu,%c,%u,%lf", &baud, &parity, &data_bits,
                     &stop_bits);
    }
    memset(&configuration, 0, sizeof(configuration));
    configuration.DCBlength = sizeof(configuration);
    if (!GetCommState(file->serial, &configuration)) return;
    configuration.BaudRate = (DWORD)baud;
    configuration.ByteSize = (BYTE)data_bits;
    configuration.Parity = (BYTE)(toupper((unsigned char)parity) == 'E'
                                      ? EVENPARITY
                                      : toupper((unsigned char)parity) == 'O'
                                            ? ODDPARITY : NOPARITY);
    configuration.fParity = configuration.Parity != NOPARITY;
    configuration.StopBits = stop_bits >= 2.0 ? TWOSTOPBITS : ONESTOPBIT;
    (void)SetCommState(file->serial, &configuration);
    memset(&timeouts, 0, sizeof(timeouts));
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = 1;
    timeouts.WriteTotalTimeoutConstant = 1000;
    (void)SetCommTimeouts(file->serial, &timeouts);
}
#endif

int bb_file_open(const char *path, double file_number, double mode,
                 double record_length)
{
    BbOpenFile *file = file_slot(file_number);
    const char *open_mode;
    if (file == NULL || path == NULL) return 1;
    if (file->stream != NULL) fclose(file->stream);
#ifdef _WIN32
    if (file->serial != NULL && file->serial != INVALID_HANDLE_VALUE)
        CloseHandle(file->serial);
#endif
    memset(file, 0, sizeof(*file));
    file->mode = (int)mode;
    file->record_length = record_length > 0.0 ? (size_t)record_length : 128U;
#ifdef _WIN32
    if (_strnicmp(path, "com1:", 5) == 0 || _strnicmp(path, "com2:", 5) == 0) {
        char device[16];
        const char *force_virtual = getenv("BBASIC_FORCE_VIRTUAL_SERIAL");
        const char *strict_serial = getenv("BBASIC_STRICT_SERIAL");
        (void)snprintf(device, sizeof(device), "\\\\.\\COM%c", path[3]);
        if (force_virtual != NULL && force_virtual[0] != '\0' &&
            strcmp(force_virtual, "0") != 0)
            file->serial = INVALID_HANDLE_VALUE;
        else
            file->serial = CreateFileA(device, GENERIC_READ | GENERIC_WRITE, 0,
                                       NULL, OPEN_EXISTING, 0, NULL);
        if (file->serial == INVALID_HANDLE_VALUE) {
            file->serial = NULL;
            if (strict_serial != NULL && strict_serial[0] != '\0' &&
                strcmp(strict_serial, "0") != 0)
                return 1;
            /*
             * Most modern systems have no legacy COM1/COM2 hardware.  Keep
             * historical terminal programs usable with a disconnected port:
             * LOC returns zero, INPUT$ is empty, and writes are discarded.
             */
            return 0;
        }
        configure_serial(file, path + 5);
        return 0;
    }
#endif
    if (file->mode == 1) open_mode = "rb";
    else if (file->mode == 2) open_mode = "wb";
    else if (file->mode == 3) open_mode = "ab+";
    else open_mode = "r+b";
    file->stream = fopen(path, open_mode);
    if (file->stream == NULL && file->mode == 4)
        file->stream = fopen(path, "w+b");
    return file->stream == NULL ? 1 : 0;
}

void bb_file_close(double file_number)
{
    BbOpenFile *file = file_slot(file_number);
    if (file == NULL) return;
    if (file->stream != NULL) fclose(file->stream);
#ifdef _WIN32
    if (file->serial != NULL && file->serial != INVALID_HANDLE_VALUE)
        CloseHandle(file->serial);
#endif
    memset(file, 0, sizeof(*file));
}

static void file_write(BbOpenFile *file, const char *data, size_t length)
{
    if (file == NULL || data == NULL) return;
    if (file->stream != NULL) {
        (void)fwrite(data, 1U, length, file->stream);
        return;
    }
#ifdef _WIN32
    if (file->serial != NULL && file->serial != INVALID_HANDLE_VALUE) {
        DWORD written;
        (void)WriteFile(file->serial, data, (DWORD)length, &written, NULL);
    }
#endif
}

void bb_file_print_string(double file_number, const char *value)
{
    BbOpenFile *file = file_slot(file_number);
    const char *safe = value != NULL ? value : "";
    file_write(file, safe, strlen(safe));
}

void bb_file_print_number(double file_number, double value)
{
    BbOpenFile *file = file_slot(file_number);
    char text[128];
    if (file == NULL) return;
    if (isfinite(value) && value == floor(value))
        (void)snprintf(text, sizeof(text), "%.0f", value);
    else
        (void)snprintf(text, sizeof(text), "%g", value);
    file_write(file, text, strlen(text));
}

void bb_file_print_separator(double file_number, double separator)
{
    BbOpenFile *file = file_slot(file_number);
    if ((int)separator == 1) file_write(file, ",", 1U);
}

void bb_file_print_newline(double file_number)
{
    BbOpenFile *file = file_slot(file_number);
    if (file != NULL) {
        file_write(file, "\n", 1U);
        if (file->stream != NULL) fflush(file->stream);
    }
}

const char *bb_file_input_next(double file_number)
{
    BbOpenFile *file = file_slot(file_number);
    char *result = next_temporary();
    int ch;
    size_t length = 0U;
    bool quoted = false;
    if (file == NULL) return result;
#ifdef _WIN32
    if (file->serial != NULL && file->serial != INVALID_HANDLE_VALUE) {
        DWORD received = 0U;
        while (length + 1U < BB_STRING_CAPACITY) {
            unsigned char byte;
            if (!ReadFile(file->serial, &byte, 1U, &received, NULL) || received == 0U)
                break;
            if (byte == ',' || isspace(byte)) {
                if (length > 0U) break;
                continue;
            }
            result[length++] = (char)byte;
        }
        result[length] = '\0';
        return result;
    }
#endif
    if (file->stream == NULL) return result;
    do {
        ch = fgetc(file->stream);
    } while (ch != EOF && (ch == ',' || isspace((unsigned char)ch)));
    if (ch == '"') {
        quoted = true;
        ch = fgetc(file->stream);
    }
    while (ch != EOF && length + 1U < BB_STRING_CAPACITY) {
        if ((quoted && ch == '"') ||
            (!quoted && (ch == ',' || isspace((unsigned char)ch))))
            break;
        result[length++] = (char)ch;
        ch = fgetc(file->stream);
    }
    result[length] = '\0';
    if (ch == EOF) file->eof_flag = 1;
    return result;
}

const char *bb_file_input_string(double count, double file_number)
{
    BbOpenFile *file = file_slot(file_number);
    char *result = next_temporary();
    size_t wanted = bounded_count(count, BB_STRING_CAPACITY - 1U);
    size_t received;
    if (file == NULL) return result;
#ifdef _WIN32
    if (file->serial != NULL && file->serial != INVALID_HANDLE_VALUE) {
        DWORD received = 0U;
        (void)ReadFile(file->serial, result, (DWORD)wanted, &received, NULL);
        result[received] = '\0';
        return result;
    }
#endif
    if (file->stream == NULL) return result;
    received = fread(result, 1U, wanted, file->stream);
    result[received] = '\0';
    if (received < wanted) file->eof_flag = 1;
    return result;
}

double bb_file_eof(double file_number)
{
    BbOpenFile *file = file_slot(file_number);
    int ch;
    if (file == NULL) return 1.0;
#ifdef _WIN32
    if (file->serial != NULL && file->serial != INVALID_HANDLE_VALUE) return 0.0;
#endif
    if (file->stream == NULL) return 1.0;
    if (file->eof_flag) return 1.0;
    ch = fgetc(file->stream);
    if (ch == EOF) {
        file->eof_flag = 1;
        return 1.0;
    }
    ungetc(ch, file->stream);
    return 0.0;
}

double bb_file_loc(double file_number)
{
    BbOpenFile *file = file_slot(file_number);
    long position;
    if (file == NULL) return 0.0;
#ifdef _WIN32
    if (file->serial != NULL && file->serial != INVALID_HANDLE_VALUE) {
        COMSTAT status;
        DWORD errors;
        if (ClearCommError(file->serial, &errors, &status))
            return (double)status.cbInQue;
        return 0.0;
    }
#endif
    if (file->stream == NULL) return 0.0;
    position = ftell(file->stream);
    return position >= 0 ? (double)position : 0.0;
}

void bb_file_field_clear(double file_number)
{
    BbOpenFile *file = file_slot(file_number);
    if (file != NULL) file->field_count = 0U;
}

void bb_file_field_bind(double file_number, double width, char *destination,
                        size_t capacity)
{
    BbOpenFile *file = file_slot(file_number);
    BbFieldBinding *field;
    if (file == NULL || destination == NULL ||
        file->field_count >= BB_MAX_FIELDS)
        return;
    field = &file->fields[file->field_count++];
    field->destination = destination;
    field->capacity = capacity;
    field->width = width > 0.0 ? (size_t)width : 0U;
}

void bb_file_get(double file_number, double record_number)
{
    BbOpenFile *file = file_slot(file_number);
    char *record;
    size_t received;
    size_t position = 0U;
    if (file == NULL || file->stream == NULL || file->record_length == 0U)
        return;
    record = (char *)calloc(file->record_length, 1U);
    if (record == NULL) return;
    if (record_number < 1.0 ||
        fseek(file->stream,
              (long)(((size_t)record_number - 1U) * file->record_length),
              SEEK_SET) != 0) {
        file->eof_flag = 1;
        free(record);
        return;
    }
    received = fread(record, 1U, file->record_length, file->stream);
    file->eof_flag = received < file->record_length;
    for (size_t index = 0U; index < file->field_count; ++index) {
        BbFieldBinding *field = &file->fields[index];
        size_t available = position < received ? received - position : 0U;
        size_t length = field->width < available ? field->width : available;
        if (length >= field->capacity) length = field->capacity - 1U;
        if (length > 0U) memcpy(field->destination, record + position, length);
        field->destination[length] = '\0';
        position += field->width;
    }
    free(record);
}

void bb_file_put(double file_number, double record_number)
{
    BbOpenFile *file = file_slot(file_number);
    char *record;
    size_t position = 0U;
    if (file == NULL || file->stream == NULL || file->record_length == 0U ||
        record_number < 1.0)
        return;
    record = (char *)malloc(file->record_length);
    if (record == NULL) return;
    memset(record, ' ', file->record_length);
    for (size_t index = 0U; index < file->field_count; ++index) {
        BbFieldBinding *field = &file->fields[index];
        size_t length = strlen(field->destination);
        size_t available = position < file->record_length
                               ? file->record_length - position
                               : 0U;
        if (length > field->width) length = field->width;
        if (length > available) length = available;
        if (length > 0U) memcpy(record + position, field->destination, length);
        position += field->width;
    }
    if (fseek(file->stream,
              (long)(((size_t)record_number - 1U) * file->record_length),
              SEEK_SET) == 0) {
        (void)fwrite(record, 1U, file->record_length, file->stream);
        fflush(file->stream);
        file->eof_flag = 0;
    }
    free(record);
}

void bb_lset(char *destination, size_t capacity, const char *value)
{
    bb_set_string(destination, capacity, value);
}

void bb_set_com(double file_number, const char *settings)
{
#ifdef _WIN32
    configure_serial(file_slot(file_number), settings);
#else
    (void)file_number;
    (void)settings;
#endif
}

int bb_runtime_error(const char *message)
{
    fprintf(stderr, "BasicBasic runtime error: %s\n",
            message != NULL ? message : "unknown error");
    return 1;
}

void bb_set_error_level(double level)
{
    configured_error_level = (int)level;
    (void)configured_error_level;
}

static bool array_layout(size_t *dimensions, size_t *total,
                         size_t dimension_count, va_list arguments)
{
    size_t product = 1U;
    if (dimension_count == 0U || dimension_count > BB_MAX_ARRAY_DIMENSIONS)
        return false;
    for (size_t index = 0U; index < dimension_count; ++index) {
        double upper_bound = va_arg(arguments, double);
        size_t extent;
        if (!isfinite(upper_bound) || upper_bound < 0.0) return false;
        extent = (size_t)upper_bound + 1U;
        if (extent == 0U || product > SIZE_MAX / extent) return false;
        dimensions[index] = extent;
        product *= extent;
    }
    *total = product;
    return true;
}

static bool array_offset(const size_t *dimensions, size_t stored_count,
                         size_t total, size_t requested_count,
                         va_list arguments, size_t *offset)
{
    size_t result = 0U;
    if (requested_count != stored_count || total == 0U) return false;
    for (size_t index = 0U; index < requested_count; ++index) {
        double raw = va_arg(arguments, double);
        size_t subscript;
        if (!isfinite(raw) || raw < 0.0) return false;
        subscript = (size_t)raw;
        if (subscript >= dimensions[index]) return false;
        result = result * dimensions[index] + subscript;
    }
    if (result >= total) return false;
    *offset = result;
    return true;
}

void bb_num_array_dim(BbNumArray *array, size_t dimension_count, ...)
{
    va_list arguments;
    size_t dimensions[BB_MAX_ARRAY_DIMENSIONS] = {0U};
    size_t total = 0U;
    if (array == NULL) return;
    va_start(arguments, dimension_count);
    if (!array_layout(dimensions, &total, dimension_count, arguments)) total = 0U;
    va_end(arguments);
    free(array->data);
    memset(array, 0, sizeof(*array));
    if (total == 0U) return;
    array->data = (double *)calloc(total, sizeof(double));
    if (array->data == NULL) return;
    memcpy(array->dimensions, dimensions, sizeof(dimensions));
    array->dimension_count = dimension_count;
    array->total = total;
}

double bb_num_array_get(const BbNumArray *array, size_t dimension_count, ...)
{
    va_list arguments;
    size_t offset = 0U;
    bool valid;
    if (array == NULL || array->data == NULL) return 0.0;
    va_start(arguments, dimension_count);
    valid = array_offset(array->dimensions, array->dimension_count, array->total,
                         dimension_count, arguments, &offset);
    va_end(arguments);
    return valid ? array->data[offset] : 0.0;
}

void bb_num_array_set(BbNumArray *array, double value,
                      size_t dimension_count, ...)
{
    va_list arguments;
    size_t offset = 0U;
    bool valid;
    if (array == NULL || array->data == NULL) return;
    va_start(arguments, dimension_count);
    valid = array_offset(array->dimensions, array->dimension_count, array->total,
                         dimension_count, arguments, &offset);
    va_end(arguments);
    if (valid) array->data[offset] = value;
}

void bb_num_array_fill(BbNumArray *array, double value)
{
    if (array == NULL || array->data == NULL) return;
    for (size_t index = 0U; index < array->total; ++index)
        array->data[index] = value;
}

void bb_string_array_dim(BbStringArray *array, size_t dimension_count, ...)
{
    va_list arguments;
    size_t dimensions[BB_MAX_ARRAY_DIMENSIONS] = {0U};
    size_t total = 0U;
    if (array == NULL) return;
    va_start(arguments, dimension_count);
    if (!array_layout(dimensions, &total, dimension_count, arguments)) total = 0U;
    va_end(arguments);
    free(array->data);
    memset(array, 0, sizeof(*array));
    if (total == 0U || total > SIZE_MAX / BB_STRING_CAPACITY) return;
    array->data = (char *)calloc(total, BB_STRING_CAPACITY);
    if (array->data == NULL) return;
    memcpy(array->dimensions, dimensions, sizeof(dimensions));
    array->dimension_count = dimension_count;
    array->total = total;
}

const char *bb_string_array_get(const BbStringArray *array,
                                size_t dimension_count, ...)
{
    va_list arguments;
    size_t offset = 0U;
    bool valid;
    if (array == NULL || array->data == NULL) return "";
    va_start(arguments, dimension_count);
    valid = array_offset(array->dimensions, array->dimension_count, array->total,
                         dimension_count, arguments, &offset);
    va_end(arguments);
    return valid ? array->data + offset * BB_STRING_CAPACITY : "";
}

void bb_string_array_set(BbStringArray *array, const char *value,
                         size_t dimension_count, ...)
{
    va_list arguments;
    size_t offset = 0U;
    bool valid;
    if (array == NULL || array->data == NULL) return;
    va_start(arguments, dimension_count);
    valid = array_offset(array->dimensions, array->dimension_count, array->total,
                         dimension_count, arguments, &offset);
    va_end(arguments);
    if (valid)
        bb_set_string(array->data + offset * BB_STRING_CAPACITY,
                      BB_STRING_CAPACITY, value);
}

void bb_string_array_fill(BbStringArray *array, const char *value)
{
    if (array == NULL || array->data == NULL) return;
    for (size_t index = 0U; index < array->total; ++index)
        bb_set_string(array->data + index * BB_STRING_CAPACITY,
                      BB_STRING_CAPACITY, value);
}
