/* Runs the production mm3_enter_game_directory() and reports the result as
 * UTF-8 lines: "entered=<dir or ->" then "cwd=<dir>". */
#include <stdio.h>
#include <wchar.h>
#include <windows.h>

#include "production_game_directory.inc"

static void emit(const char *label, const wchar_t *path)
{
    char utf8[8192];
    DWORD written;
    int length = path ? WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof utf8, NULL, NULL) : 0;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    WriteFile(out, label, (DWORD)strlen(label), &written, NULL);
    if (length > 1)
        WriteFile(out, utf8, (DWORD)(length - 1), &written, NULL);
    else
        WriteFile(out, "-", 1, &written, NULL);
    WriteFile(out, "\n", 1, &written, NULL);
}

int main(void)
{
    static wchar_t cwd[4096];
    const wchar_t *entered = mm3_enter_game_directory();
    GetCurrentDirectoryW(4096, cwd);
    emit("entered=", entered);
    emit("cwd=", cwd);
    return 0;
}
