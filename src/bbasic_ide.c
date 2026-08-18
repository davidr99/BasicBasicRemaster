#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IDE_TITLE "BasicBasic IDE"
#define WM_BUILD_COMPLETE (WM_APP + 1)

enum {
    ID_FILE_NEW = 100,
    ID_FILE_OPEN,
    ID_FILE_SAVE,
    ID_FILE_SAVE_AS,
    ID_FILE_EXIT,
    ID_EDIT_UNDO = 200,
    ID_EDIT_CUT,
    ID_EDIT_COPY,
    ID_EDIT_PASTE,
    ID_EDIT_SELECT_ALL,
    ID_BUILD_COMPILE = 300,
    ID_BUILD_RUN,
    ID_HELP_ABOUT = 400,
    ID_EDITOR = 500,
    ID_OUTPUT,
    ID_STATUS
};

typedef struct TextBuffer {
    char *data;
    size_t length;
    size_t capacity;
} TextBuffer;

typedef struct BuildJob {
    HWND notify_window;
    char source[MAX_PATH];
    char generated[MAX_PATH];
    char executable[MAX_PATH];
    char compiler[MAX_PATH];
    char gcc[MAX_PATH];
    char repository[MAX_PATH];
    bool run_after_build;
} BuildJob;

typedef struct BuildResult {
    bool success;
    bool run_after_build;
    char source[MAX_PATH];
    char executable[MAX_PATH];
    char *output;
} BuildResult;

static HINSTANCE application_instance;
static HWND main_window;
static HWND source_editor;
static HWND output_editor;
static HWND status_label;
static HWND compile_button;
static HWND run_button;
static HFONT editor_font;
static HFONT ui_font;
static char current_file[MAX_PATH];
static char current_executable[MAX_PATH];
static char repository_root[MAX_PATH];
static char compiler_path[MAX_PATH];
static char gcc_path[MAX_PATH];
static char build_directory[MAX_PATH];
static bool document_dirty;
static bool loading_document;
static bool build_running;

static void copy_text(char *destination, size_t capacity, const char *source)
{
    if (capacity == 0U) return;
    if (source == NULL) source = "";
    (void)snprintf(destination, capacity, "%s", source);
}

static void join_path(char *destination, size_t capacity,
                      const char *directory, const char *name)
{
    size_t length = strlen(directory);
    (void)snprintf(destination, capacity, "%s%s%s", directory,
                   length > 0U && directory[length - 1U] != '\\' ? "\\" : "",
                   name);
}

static void directory_of(char *destination, size_t capacity, const char *path)
{
    char *separator;
    copy_text(destination, capacity, path);
    separator = strrchr(destination, '\\');
    if (separator == NULL) separator = strrchr(destination, '/');
    if (separator != NULL)
        *separator = '\0';
    else
        copy_text(destination, capacity, ".");
}

static void parent_directory(char *path)
{
    char *separator = strrchr(path, '\\');
    if (separator == NULL) separator = strrchr(path, '/');
    if (separator != NULL) *separator = '\0';
}

static void base_name_without_extension(char *destination, size_t capacity,
                                        const char *path)
{
    const char *base = strrchr(path, '\\');
    const char *slash = strrchr(path, '/');
    char *extension;
    if (slash != NULL && (base == NULL || slash > base)) base = slash;
    base = base != NULL ? base + 1 : path;
    copy_text(destination, capacity, base);
    extension = strrchr(destination, '.');
    if (extension != NULL) *extension = '\0';
}

static bool file_exists(const char *path)
{
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool buffer_reserve(TextBuffer *buffer, size_t additional)
{
    size_t needed = buffer->length + additional + 1U;
    size_t capacity;
    char *resized;
    if (needed <= buffer->capacity) return true;
    capacity = buffer->capacity != 0U ? buffer->capacity : 4096U;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2U) return false;
        capacity *= 2U;
    }
    resized = (char *)realloc(buffer->data, capacity);
    if (resized == NULL) return false;
    buffer->data = resized;
    buffer->capacity = capacity;
    return true;
}

static bool buffer_append_n(TextBuffer *buffer, const char *text, size_t length)
{
    if (!buffer_reserve(buffer, length)) return false;
    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return true;
}

static bool buffer_append(TextBuffer *buffer, const char *text)
{
    return buffer_append_n(buffer, text, strlen(text));
}

static void format_windows_error(char *destination, size_t capacity,
                                 DWORD error)
{
    DWORD length = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error,
        0, destination, (DWORD)capacity, NULL);
    while (length > 0U &&
           (destination[length - 1U] == '\r' ||
            destination[length - 1U] == '\n'))
        destination[--length] = '\0';
    if (length == 0U)
        (void)snprintf(destination, capacity, "Windows error %lu",
                       (unsigned long)error);
}

static bool run_process_capture(const char *command, const char *working,
                                TextBuffer *output, DWORD *exit_code)
{
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    HANDLE read_pipe = NULL;
    HANDLE write_pipe = NULL;
    char *mutable_command;
    bool started;
    char error_text[512];

    buffer_append(output, "> ");
    buffer_append(output, command);
    buffer_append(output, "\r\n");
    if (!CreatePipe(&read_pipe, &write_pipe, &security, 0U)) return false;
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0U);
    memset(&startup, 0, sizeof(startup));
    memset(&process, 0, sizeof(process));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = write_pipe;
    startup.hStdError = write_pipe;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    mutable_command = _strdup(command);
    if (mutable_command == NULL) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return false;
    }
    started = CreateProcessA(NULL, mutable_command, NULL, NULL, TRUE,
                             CREATE_NO_WINDOW, NULL, working, &startup,
                             &process) != FALSE;
    free(mutable_command);
    CloseHandle(write_pipe);
    if (!started) {
        format_windows_error(error_text, sizeof(error_text), GetLastError());
        buffer_append(output, "Unable to start process: ");
        buffer_append(output, error_text);
        buffer_append(output, "\r\n");
        CloseHandle(read_pipe);
        return false;
    }
    for (;;) {
        char chunk[4096];
        DWORD received = 0U;
        if (!ReadFile(read_pipe, chunk, sizeof(chunk), &received, NULL) ||
            received == 0U)
            break;
        if (!buffer_append_n(output, chunk, received)) break;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(read_pipe);
    if (output->length == 0U ||
        (output->data[output->length - 1U] != '\n' &&
         output->data[output->length - 1U] != '\r'))
        buffer_append(output, "\r\n");
    return true;
}

static bool file_contains(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    long size;
    char *contents;
    bool found = false;
    if (file == NULL) return false;
    if (fseek(file, 0L, SEEK_END) != 0 || (size = ftell(file)) < 0L ||
        fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    contents = (char *)malloc((size_t)size + 1U);
    if (contents != NULL) {
        size_t read = fread(contents, 1U, (size_t)size, file);
        contents[read] = '\0';
        found = strstr(contents, needle) != NULL;
        free(contents);
    }
    fclose(file);
    return found;
}

static bool file_marker_value(const char *path, const char *marker,
                              char *destination, size_t capacity)
{
    FILE *file = fopen(path, "r");
    char line[4096];
    if (file == NULL || capacity == 0U) return false;
    while (fgets(line, sizeof(line), file) != NULL) {
        char *value = strstr(line, marker);
        char *end;
        size_t length;
        if (value == NULL) continue;
        value += strlen(marker);
        while (isspace((unsigned char)*value)) ++value;
        end = strstr(value, "*/");
        if (end == NULL) continue;
        while (end > value && isspace((unsigned char)end[-1])) --end;
        length = (size_t)(end - value);
        if (length >= capacity) length = capacity - 1U;
        memcpy(destination, value, length);
        destination[length] = '\0';
        fclose(file);
        return length > 0U;
    }
    fclose(file);
    return false;
}

static bool path_is_absolute(const char *path)
{
    return (isalpha((unsigned char)path[0]) && path[1] == ':') ||
           path[0] == '\\' || path[0] == '/';
}

static DWORD WINAPI build_worker(LPVOID parameter)
{
    BuildJob *job = (BuildJob *)parameter;
    BuildResult *result = (BuildResult *)calloc(1U, sizeof(*result));
    TextBuffer output = {0};
    char command[8192];
    char include_path[MAX_PATH];
    char runtime_path[MAX_PATH];
    char win32_path[MAX_PATH];
    char gcc_root[MAX_PATH];
    char gcc_include[MAX_PATH];
    char gcc_stddef[MAX_PATH];
    char system_include_option[MAX_PATH + 32] = "";
    char icon_name[MAX_PATH] = "";
    char icon_path[MAX_PATH * 2] = "";
    char icon_directory[MAX_PATH] = "";
    char windres_path[MAX_PATH] = "";
    char resource_script[MAX_PATH * 2] = "";
    char resource_object[MAX_PATH * 2] = "";
    char resource_option[MAX_PATH * 2 + 8] = "";
    DWORD exit_code = 1U;
    bool windows_application;

    if (result == NULL) {
        free(job);
        return 1U;
    }
    copy_text(result->source, sizeof(result->source), job->source);
    copy_text(result->executable, sizeof(result->executable), job->executable);
    result->run_after_build = job->run_after_build;
    buffer_append(&output, "BasicBasic build\r\n\r\n");
    (void)snprintf(command, sizeof(command), "\"%s\" \"%s\" -o \"%s\"",
                   job->compiler, job->source, job->generated);
    if (!run_process_capture(command, job->repository, &output, &exit_code) ||
        exit_code != 0U) {
        buffer_append(&output, "\r\nTranslation failed.\r\n");
        goto done;
    }
    windows_application =
        file_contains(job->generated, "BBASIC_SUBSYSTEM: WINDOWS");
    join_path(include_path, sizeof(include_path), job->repository, "include");
    join_path(runtime_path, sizeof(runtime_path), job->repository,
              "src\\bbasic_runtime.c");
    join_path(win32_path, sizeof(win32_path), job->repository,
              "src\\bbasic_win32.c");
    directory_of(gcc_root, sizeof(gcc_root), job->gcc);
    parent_directory(gcc_root);
    join_path(gcc_include, sizeof(gcc_include), gcc_root, "include");
    join_path(gcc_stddef, sizeof(gcc_stddef), gcc_include, "stddef.h");
    if (file_exists(gcc_stddef))
        (void)snprintf(system_include_option, sizeof(system_include_option),
                       "-isystem \"%s\" ", gcc_include);
    if (file_marker_value(job->generated, "BBASIC_ICON:", icon_name,
                          sizeof(icon_name))) {
        FILE *resource;
        char resource_icon[MAX_PATH * 2];
        char gcc_directory[MAX_PATH];
        if (path_is_absolute(icon_name)) {
            copy_text(icon_path, sizeof(icon_path), icon_name);
        } else {
            directory_of(icon_directory, sizeof(icon_directory), job->source);
            join_path(icon_path, sizeof(icon_path), icon_directory, icon_name);
        }
        if (!file_exists(icon_path)) {
            buffer_append(&output, "\r\nIcon file was not found: ");
            buffer_append(&output, icon_path);
            buffer_append(&output, "\r\n");
            goto done;
        }
        directory_of(gcc_directory, sizeof(gcc_directory), job->gcc);
        join_path(windres_path, sizeof(windres_path), gcc_directory,
                  "windres.exe");
        if (!file_exists(windres_path)) {
            buffer_append(&output,
                          "\r\nThe ICON directive requires windres.exe "
                          "beside GCC.\r\n");
            goto done;
        }
        (void)snprintf(resource_script, sizeof(resource_script), "%s.icon.rc",
                       job->generated);
        (void)snprintf(resource_object, sizeof(resource_object), "%s.icon.o",
                       job->generated);
        copy_text(resource_icon, sizeof(resource_icon), icon_path);
        for (char *character = resource_icon; *character != '\0'; ++character)
            if (*character == '\\') *character = '/';
        resource = fopen(resource_script, "w");
        if (resource == NULL) {
            buffer_append(&output, "\r\nUnable to create icon resource.\r\n");
            goto done;
        }
        fprintf(resource, "1 ICON \"%s\"\n", resource_icon);
        fclose(resource);
        (void)snprintf(command, sizeof(command),
                       "\"%s\" -i \"%s\" -o \"%s\"", windres_path,
                       resource_script, resource_object);
        exit_code = 1U;
        if (!run_process_capture(command, job->repository, &output,
                                 &exit_code) || exit_code != 0U) {
            buffer_append(&output, "\r\nIcon resource compilation failed.\r\n");
            goto done;
        }
        (void)snprintf(resource_option, sizeof(resource_option), "\"%s\" ",
                       resource_object);
    }
    (void)snprintf(
        command, sizeof(command),
        "\"%s\" -std=c11 -O2 %s-I \"%s\" \"%s\" \"%s\" \"%s\" "
        "%s%s-lm -lgdi32 -luser32 -lcomdlg32 -lwinmm -o \"%s\"",
        job->gcc, system_include_option, include_path, job->generated,
        runtime_path, win32_path, resource_option,
        windows_application ? "-mwindows " : "", job->executable);
    exit_code = 1U;
    if (!run_process_capture(command, job->repository, &output, &exit_code) ||
        exit_code != 0U) {
        buffer_append(&output, "\r\nNative compilation failed.\r\n");
        goto done;
    }
    buffer_append(&output, "\r\nBuild succeeded: ");
    buffer_append(&output, job->executable);
    buffer_append(&output, "\r\n");
    result->success = true;

done:
    if (resource_script[0] != '\0') (void)DeleteFileA(resource_script);
    if (resource_object[0] != '\0') (void)DeleteFileA(resource_object);
    if (output.data == NULL) output.data = _strdup("Build failed.\r\n");
    result->output = output.data;
    if (!PostMessageA(job->notify_window, WM_BUILD_COMPLETE, 0,
                      (LPARAM)result)) {
        free(result->output);
        free(result);
    }
    free(job);
    return 0U;
}

static void set_status(const char *text)
{
    SetWindowTextA(status_label, text != NULL ? text : "");
}

static void update_title(void)
{
    char title[MAX_PATH + 64];
    char name[MAX_PATH];
    if (current_file[0] != '\0')
        base_name_without_extension(name, sizeof(name), current_file);
    else
        copy_text(name, sizeof(name), "Untitled");
    (void)snprintf(title, sizeof(title), "%s - %s%s", IDE_TITLE, name,
                   document_dirty ? " *" : "");
    SetWindowTextA(main_window, title);
}

static void set_dirty(bool dirty)
{
    if (document_dirty == dirty) return;
    document_dirty = dirty;
    update_title();
}

static char *editor_text_from_file(const char *contents, size_t length)
{
    size_t extra = 0U;
    size_t source;
    size_t destination = 0U;
    char *text;
    for (source = 0U; source < length; ++source) {
        if (contents[source] == '\n' &&
            (source == 0U || contents[source - 1U] != '\r'))
            ++extra;
    }
    if (length > SIZE_MAX - extra - 1U) return NULL;
    text = (char *)malloc(length + extra + 1U);
    if (text == NULL) return NULL;
    for (source = 0U; source < length; ++source) {
        if (contents[source] == '\n' &&
            (source == 0U || contents[source - 1U] != '\r'))
            text[destination++] = '\r';
        text[destination++] = contents[source];
    }
    text[destination] = '\0';
    return text;
}

static bool read_source_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    long size;
    char *contents;
    char *editor_text;
    size_t read;
    if (file == NULL) return false;
    if (fseek(file, 0L, SEEK_END) != 0 || (size = ftell(file)) < 0L ||
        fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    contents = (char *)malloc((size_t)size + 1U);
    if (contents == NULL) {
        fclose(file);
        return false;
    }
    read = fread(contents, 1U, (size_t)size, file);
    contents[read] = '\0';
    fclose(file);
    editor_text = editor_text_from_file(contents, read);
    free(contents);
    if (editor_text == NULL) return false;
    loading_document = true;
    SetWindowTextA(source_editor, editor_text);
    SendMessageA(source_editor, EM_SETSEL, 0, 0);
    loading_document = false;
    free(editor_text);
    copy_text(current_file, sizeof(current_file), path);
    current_executable[0] = '\0';
    document_dirty = false;
    update_title();
    set_status(path);
    SetFocus(source_editor);
    return true;
}

static bool write_source_file(const char *path)
{
    int length = GetWindowTextLengthA(source_editor);
    char *contents = (char *)malloc((size_t)length + 1U);
    FILE *file;
    bool success;
    if (contents == NULL) return false;
    GetWindowTextA(source_editor, contents, length + 1);
    file = fopen(path, "wb");
    if (file == NULL) {
        free(contents);
        return false;
    }
    success = fwrite(contents, 1U, (size_t)length, file) == (size_t)length;
    success = fclose(file) == 0 && success;
    free(contents);
    if (success) {
        copy_text(current_file, sizeof(current_file), path);
        document_dirty = false;
        update_title();
        set_status(path);
    }
    return success;
}

static bool choose_save_path(char *path, size_t capacity)
{
    OPENFILENAMEA dialog;
    char selected[MAX_PATH] = "";
    if (current_file[0] != '\0') copy_text(selected, sizeof(selected), current_file);
    memset(&dialog, 0, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = main_window;
    dialog.lpstrFilter = "BasicBasic source (*.bas)\0*.bas\0All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = selected;
    dialog.nMaxFile = sizeof(selected);
    dialog.lpstrDefExt = "bas";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameA(&dialog)) return false;
    copy_text(path, capacity, selected);
    return true;
}

static bool save_document(bool force_dialog)
{
    char path[MAX_PATH];
    if (force_dialog || current_file[0] == '\0') {
        if (!choose_save_path(path, sizeof(path))) return false;
    } else {
        copy_text(path, sizeof(path), current_file);
    }
    if (!write_source_file(path)) {
        MessageBoxA(main_window, "Unable to save the source file.", IDE_TITLE,
                    MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

static bool confirm_discard(void)
{
    int answer;
    if (!document_dirty) return true;
    answer = MessageBoxA(main_window, "Save changes to the current program?",
                         IDE_TITLE, MB_YESNOCANCEL | MB_ICONQUESTION);
    if (answer == IDCANCEL) return false;
    if (answer == IDYES) return save_document(false);
    return true;
}

static void new_document(void)
{
    if (!confirm_discard()) return;
    loading_document = true;
    SetWindowTextA(source_editor,
                   "rem New BasicBasic program\r\n\r\nprint \"Hello\"\r\n");
    loading_document = false;
    current_file[0] = '\0';
    current_executable[0] = '\0';
    document_dirty = false;
    update_title();
    set_status("New program");
    SetFocus(source_editor);
}

static void open_document(void)
{
    OPENFILENAMEA dialog;
    char path[MAX_PATH] = "";
    if (!confirm_discard()) return;
    memset(&dialog, 0, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = main_window;
    dialog.lpstrFilter = "BasicBasic source (*.bas)\0*.bas\0All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = sizeof(path);
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameA(&dialog) && !read_source_file(path))
        MessageBoxA(main_window, "Unable to open the source file.", IDE_TITLE,
                    MB_OK | MB_ICONERROR);
}

static void append_output(const char *text)
{
    int end = GetWindowTextLengthA(output_editor);
    SendMessageA(output_editor, EM_SETSEL, end, end);
    SendMessageA(output_editor, EM_REPLACESEL, FALSE, (LPARAM)text);
    SendMessageA(output_editor, EM_SCROLLCARET, 0, 0);
}

static bool launch_program(const char *executable, const char *source)
{
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    char command[MAX_PATH + 8];
    char working[MAX_PATH];
    char error_text[512];
    memset(&startup, 0, sizeof(startup));
    memset(&process, 0, sizeof(process));
    startup.cb = sizeof(startup);
    (void)snprintf(command, sizeof(command), "\"%s\"", executable);
    directory_of(working, sizeof(working), source);
    if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, 0U, NULL, working,
                        &startup, &process)) {
        format_windows_error(error_text, sizeof(error_text), GetLastError());
        append_output("\r\nUnable to run program: ");
        append_output(error_text);
        append_output("\r\n");
        set_status("Run failed");
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    append_output("\r\nProgram started: ");
    append_output(executable);
    append_output("\r\n");
    set_status("Program running");
    return true;
}

static void start_build(bool run_after_build)
{
    BuildJob *job;
    char base[MAX_PATH];
    char filename[MAX_PATH];
    HANDLE thread;
    if (build_running) return;
    if (current_file[0] == '\0' || document_dirty) {
        if (!save_document(false)) return;
    }
    if (!file_exists(compiler_path) || !file_exists(gcc_path)) {
        MessageBoxA(main_window,
                    "The compiler or GCC could not be found. Re-run "
                    "tools\\build_ide.ps1, or set BBASIC_GCC.",
                    IDE_TITLE, MB_OK | MB_ICONERROR);
        return;
    }
    CreateDirectoryA(build_directory, NULL);
    base_name_without_extension(base, sizeof(base), current_file);
    (void)snprintf(filename, sizeof(filename), "%s.c", base);
    job = (BuildJob *)calloc(1U, sizeof(*job));
    if (job == NULL) return;
    job->notify_window = main_window;
    copy_text(job->source, sizeof(job->source), current_file);
    join_path(job->generated, sizeof(job->generated), build_directory, filename);
    (void)snprintf(filename, sizeof(filename), "%s.exe", base);
    join_path(job->executable, sizeof(job->executable), build_directory, filename);
    copy_text(job->compiler, sizeof(job->compiler), compiler_path);
    copy_text(job->gcc, sizeof(job->gcc), gcc_path);
    copy_text(job->repository, sizeof(job->repository), repository_root);
    job->run_after_build = run_after_build;
    build_running = true;
    EnableWindow(compile_button, FALSE);
    EnableWindow(run_button, FALSE);
    SetWindowTextA(output_editor, run_after_build
                                      ? "Saving, compiling, then running...\r\n"
                                      : "Compiling...\r\n");
    set_status("Build in progress");
    thread = CreateThread(NULL, 0U, build_worker, job, 0U, NULL);
    if (thread == NULL) {
        free(job);
        build_running = false;
        EnableWindow(compile_button, TRUE);
        EnableWindow(run_button, TRUE);
        set_status("Unable to start build");
    } else {
        CloseHandle(thread);
    }
}

static void initialize_tool_paths(void)
{
    char module[MAX_PATH];
    char module_directory[MAX_PATH];
    char candidate[MAX_PATH];
    char environment[MAX_PATH];
    DWORD length;
    GetModuleFileNameA(NULL, module, sizeof(module));
    directory_of(module_directory, sizeof(module_directory), module);
    copy_text(repository_root, sizeof(repository_root), module_directory);
    for (int depth = 0; depth < 5; ++depth) {
        join_path(candidate, sizeof(candidate), repository_root,
                  "src\\bbasic_runtime.c");
        if (file_exists(candidate)) break;
        parent_directory(repository_root);
    }
    join_path(compiler_path, sizeof(compiler_path), module_directory,
              "bbasicc.exe");
    if (!file_exists(compiler_path))
        join_path(compiler_path, sizeof(compiler_path), repository_root,
                  "build-tools\\bbasicc.exe");
    length = GetEnvironmentVariableA("BBASIC_GCC", environment,
                                     (DWORD)sizeof(environment));
    if (length > 0U && length < sizeof(environment) && file_exists(environment)) {
        copy_text(gcc_path, sizeof(gcc_path), environment);
    } else {
        join_path(candidate, sizeof(candidate), module_directory,
                  "toolchain\\bin\\gcc.exe");
        if (file_exists(candidate)) {
            copy_text(gcc_path, sizeof(gcc_path), candidate);
        } else {
            copy_text(candidate, sizeof(candidate),
                      "K:\\msys64\\mingw64\\bin\\gcc.exe");
            if (file_exists(candidate)) {
                copy_text(gcc_path, sizeof(gcc_path), candidate);
            } else if (SearchPathA(NULL, "gcc.exe", NULL, sizeof(candidate),
                                   candidate, NULL) != 0U) {
                copy_text(gcc_path, sizeof(gcc_path), candidate);
            }
        }
    }
    if (gcc_path[0] != '\0') {
        char gcc_directory[MAX_PATH];
        char old_path[32768] = "";
        char new_path[32768];
        directory_of(gcc_directory, sizeof(gcc_directory), gcc_path);
        GetEnvironmentVariableA("PATH", old_path, (DWORD)sizeof(old_path));
        (void)snprintf(new_path, sizeof(new_path), "%s;%s", gcc_directory,
                       old_path);
        SetEnvironmentVariableA("PATH", new_path);
    }
    join_path(build_directory, sizeof(build_directory), repository_root,
              "build-ide-programs");
}

static HMENU create_main_menu(void)
{
    HMENU menu = CreateMenu();
    HMENU file = CreatePopupMenu();
    HMENU edit = CreatePopupMenu();
    HMENU build = CreatePopupMenu();
    HMENU help = CreatePopupMenu();
    AppendMenuA(file, MF_STRING, ID_FILE_NEW, "&New\tCtrl+N");
    AppendMenuA(file, MF_STRING, ID_FILE_OPEN, "&Open...\tCtrl+O");
    AppendMenuA(file, MF_STRING, ID_FILE_SAVE, "&Save\tCtrl+S");
    AppendMenuA(file, MF_STRING, ID_FILE_SAVE_AS, "Save &As...");
    AppendMenuA(file, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file, MF_STRING, ID_FILE_EXIT, "E&xit");
    AppendMenuA(edit, MF_STRING, ID_EDIT_UNDO, "&Undo\tCtrl+Z");
    AppendMenuA(edit, MF_SEPARATOR, 0, NULL);
    AppendMenuA(edit, MF_STRING, ID_EDIT_CUT, "Cu&t\tCtrl+X");
    AppendMenuA(edit, MF_STRING, ID_EDIT_COPY, "&Copy\tCtrl+C");
    AppendMenuA(edit, MF_STRING, ID_EDIT_PASTE, "&Paste\tCtrl+V");
    AppendMenuA(edit, MF_STRING, ID_EDIT_SELECT_ALL, "Select &All\tCtrl+A");
    AppendMenuA(build, MF_STRING, ID_BUILD_COMPILE, "&Compile\tF7");
    AppendMenuA(build, MF_STRING, ID_BUILD_RUN, "Compile and &Run\tF5");
    AppendMenuA(help, MF_STRING, ID_HELP_ABOUT, "&About");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)file, "&File");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)edit, "&Edit");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)build, "&Build");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)help, "&Help");
    return menu;
}

static HWND create_button(HWND parent, int identifier, const char *text,
                          int x, int width)
{
    HWND button = CreateWindowExA(0, "BUTTON", text,
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                      BS_PUSHBUTTON,
                                  x, 6, width, 28, parent,
                                  (HMENU)(INT_PTR)identifier,
                                  application_instance, NULL);
    SendMessageA(button, WM_SETFONT, (WPARAM)ui_font, TRUE);
    return button;
}

static void layout_children(int width, int height)
{
    const int toolbar_height = 40;
    const int status_height = 22;
    int output_height = height / 4;
    int editor_height;
    if (output_height < 110) output_height = 110;
    if (output_height > 220) output_height = 220;
    editor_height = height - toolbar_height - output_height - status_height;
    if (editor_height < 80) editor_height = 80;
    MoveWindow(source_editor, 6, toolbar_height, width - 12, editor_height,
               TRUE);
    MoveWindow(output_editor, 6, toolbar_height + editor_height + 4,
               width - 12, output_height - 8, TRUE);
    MoveWindow(status_label, 0, height - status_height, width, status_height,
               TRUE);
}

static LRESULT CALLBACK ide_window_proc(HWND window, UINT message,
                                        WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_CREATE: {
        RECT client;
        ui_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        editor_font = CreateFontA(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  FIXED_PITCH | FF_MODERN, "Consolas");
        create_button(window, ID_FILE_NEW, "New", 6, 58);
        create_button(window, ID_FILE_OPEN, "Open", 68, 58);
        create_button(window, ID_FILE_SAVE, "Save", 130, 58);
        compile_button = create_button(window, ID_BUILD_COMPILE, "Compile",
                                       204, 72);
        run_button = create_button(window, ID_BUILD_RUN, "Run", 280, 58);
        source_editor = CreateWindowExA(
            WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_LEFT |
                ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN |
                ES_NOHIDESEL,
            0, 0, 0, 0, window, (HMENU)(INT_PTR)ID_EDITOR,
            application_instance, NULL);
        output_editor = CreateWindowExA(
            WS_EX_CLIENTEDGE, "EDIT", "Build output appears here.",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE |
                ES_AUTOVSCROLL | ES_READONLY,
            0, 0, 0, 0, window, (HMENU)(INT_PTR)ID_OUTPUT,
            application_instance, NULL);
        status_label = CreateWindowExA(
            0, "STATIC", "Ready", WS_CHILD | WS_VISIBLE | SS_SUNKEN,
            0, 0, 0, 0, window, (HMENU)(INT_PTR)ID_STATUS,
            application_instance, NULL);
        SendMessageA(source_editor, WM_SETFONT, (WPARAM)editor_font, TRUE);
        SendMessageA(source_editor, EM_SETLIMITTEXT, 0x7ffffffe, 0);
        SendMessageA(output_editor, WM_SETFONT, (WPARAM)editor_font, TRUE);
        SendMessageA(output_editor, EM_SETLIMITTEXT, 0x7ffffffe, 0);
        SendMessageA(status_label, WM_SETFONT, (WPARAM)ui_font, TRUE);
        GetClientRect(window, &client);
        layout_children(client.right, client.bottom);
        new_document();
        return 0;
    }
    case WM_SIZE:
        layout_children(LOWORD(lparam), HIWORD(lparam));
        return 0;
    case WM_SETFOCUS:
        SetFocus(source_editor);
        return 0;
    case WM_COMMAND: {
        int identifier = LOWORD(wparam);
        if (identifier == ID_EDITOR && HIWORD(wparam) == EN_CHANGE &&
            !loading_document)
            set_dirty(true);
        switch (identifier) {
        case ID_FILE_NEW: new_document(); break;
        case ID_FILE_OPEN: open_document(); break;
        case ID_FILE_SAVE: (void)save_document(false); break;
        case ID_FILE_SAVE_AS: (void)save_document(true); break;
        case ID_FILE_EXIT: SendMessageA(window, WM_CLOSE, 0, 0); break;
        case ID_EDIT_UNDO: SendMessageA(source_editor, EM_UNDO, 0, 0); break;
        case ID_EDIT_CUT: SendMessageA(source_editor, WM_CUT, 0, 0); break;
        case ID_EDIT_COPY: SendMessageA(source_editor, WM_COPY, 0, 0); break;
        case ID_EDIT_PASTE: SendMessageA(source_editor, WM_PASTE, 0, 0); break;
        case ID_EDIT_SELECT_ALL:
            SendMessageA(source_editor, EM_SETSEL, 0, -1);
            break;
        case ID_BUILD_COMPILE: start_build(false); break;
        case ID_BUILD_RUN: start_build(true); break;
        case ID_HELP_ABOUT:
            MessageBoxA(window,
                        "Modern BasicBasic IDE\r\n\r\nA clean native editor "
                        "inspired by the original WBBE workflow.\r\nBuilds "
                        "with the modern BasicBasic compiler and GCC.",
                        "About BasicBasic IDE", MB_OK | MB_ICONINFORMATION);
            break;
        default: break;
        }
        return 0;
    }
    case WM_BUILD_COMPLETE: {
        BuildResult *result = (BuildResult *)lparam;
        build_running = false;
        EnableWindow(compile_button, TRUE);
        EnableWindow(run_button, TRUE);
        SetWindowTextA(output_editor,
                       result->output != NULL ? result->output : "");
        if (result->success) {
            if (_stricmp(result->source, current_file) == 0)
                copy_text(current_executable, sizeof(current_executable),
                          result->executable);
            set_status("Build succeeded");
            if (result->run_after_build)
                (void)launch_program(result->executable, result->source);
        } else {
            set_status("Build failed");
        }
        free(result->output);
        free(result);
        return 0;
    }
    case WM_CLOSE:
        if (!confirm_discard()) return 0;
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        if (editor_font != NULL) DeleteObject(editor_font);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcA(window, message, wparam, lparam);
    }
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line,
                   int show_command)
{
    WNDCLASSA window_class;
    MSG message;
    HACCEL accelerators;
    ACCEL keys[] = {
        {FCONTROL | FVIRTKEY, 'N', ID_FILE_NEW},
        {FCONTROL | FVIRTKEY, 'O', ID_FILE_OPEN},
        {FCONTROL | FVIRTKEY, 'S', ID_FILE_SAVE},
        {FVIRTKEY, VK_F7, ID_BUILD_COMPILE},
        {FVIRTKEY, VK_F5, ID_BUILD_RUN},
    };
    (void)previous;
    application_instance = instance;
    initialize_tool_paths();
    memset(&window_class, 0, sizeof(window_class));
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = ide_window_proc;
    window_class.hInstance = instance;
    window_class.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
    window_class.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    window_class.lpszClassName = "ModernBasicBasicIDE";
    if (!RegisterClassA(&window_class)) return 1;
    main_window = CreateWindowExA(
        0, window_class.lpszClassName, IDE_TITLE,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
        1000, 720, NULL, create_main_menu(), instance, NULL);
    if (main_window == NULL) return 2;
    accelerators = CreateAcceleratorTableA(keys,
                                            (int)(sizeof(keys) / sizeof(keys[0])));
    ShowWindow(main_window, show_command);
    UpdateWindow(main_window);
    if (command_line != NULL && *command_line != '\0') {
        char initial_file[MAX_PATH] = "";
        const char *start = command_line;
        size_t length;
        while (*start == ' ' || *start == '\t') ++start;
        if (*start == '"') {
            const char *end = strchr(++start, '"');
            length = end != NULL ? (size_t)(end - start) : strlen(start);
        } else {
            length = strlen(start);
        }
        if (length >= sizeof(initial_file)) length = sizeof(initial_file) - 1U;
        memcpy(initial_file, start, length);
        initial_file[length] = '\0';
        if (file_exists(initial_file) && !read_source_file(initial_file))
            MessageBoxA(main_window, "Unable to open the requested source file.",
                        IDE_TITLE, MB_OK | MB_ICONERROR);
    }
    while (GetMessageA(&message, NULL, 0, 0) > 0) {
        if (accelerators == NULL ||
            !TranslateAcceleratorA(main_window, accelerators, &message)) {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
    }
    if (accelerators != NULL) DestroyAcceleratorTable(accelerators);
    return (int)message.wParam;
}
