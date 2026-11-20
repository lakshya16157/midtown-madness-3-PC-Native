/* After an update, a window lists what is new in every version since the one the player last saw.
 * The notes are src/whats_new.txt, built into the program (src/mm3.rc); the last version seen is
 * whats-new-seen.txt beside launcher.ini. Updating with the launcher's Check for updates runs Setup,
 * so the next start of the new program is the first one after the update. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mm3_update.h"
#include "mm3_whats_new.h"

static int append(char *out, size_t size, size_t *at, const char *text, size_t length)
{
    if (*at + length + 1 > size) return 0;
    memcpy(out + *at, text, length);
    *at += length;
    out[*at] = 0;
    return 1;
}

size_t mm3_whats_new_text(const char *notes, const char *seen, const char *current, char *out, size_t size)
{
    const char *line = notes;
    size_t at = 0;
    int include = 0, any = 0;
    if (!out || !size) return 0;
    out[0] = 0;
    if (!notes || !current || !mm3_update_version_valid(current)) return 0;
    while (*line) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        while (length && line[length - 1] == '\r') length--;
        if (length > 3 && !strncmp(line, "## ", 3)) {
            char version[64];
            size_t n = length - 3 < sizeof version - 1 ? length - 3 : sizeof version - 1;
            memcpy(version, line + 3, n);
            version[n] = 0;
            include = mm3_update_version_valid(version) && mm3_update_compare(version, current) <= 0 &&
                      (seen && *seen && mm3_update_version_valid(seen) ? mm3_update_compare(version, seen) > 0
                                                                      : mm3_update_compare(version, current) == 0);
            if (include) {
                if ((any && !append(out, size, &at, "\r\n", 2)) || !append(out, size, &at, "Version ", 8) ||
                    !append(out, size, &at, version, strlen(version)) || !append(out, size, &at, "\r\n", 2))
                    return at;
                any = 1;
            }
        } else if (include && length) {
            if (!append(out, size, &at, line, length) || !append(out, size, &at, "\r\n", 2)) return at;
        }
        if (!end) break;
        line = end + 1;
    }
    return at;
}

#ifdef _WIN32
#include <windows.h>
#include "mm3_launcher_config.h"
#include "mm3_whats_new_ids.h"

#ifndef MM3_VERSION
#define MM3_VERSION "development build"
#endif

static INT_PTR CALLBACK whats_new_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_INITDIALOG) {
        wchar_t title[160];
        _snwprintf(title, sizeof title / sizeof title[0], L"You now have Midtown Madness 3 PC %hs. What is new:", MM3_VERSION);
        title[sizeof title / sizeof title[0] - 1] = 0;
        SetDlgItemTextW(dialog, IDC_WHATS_NEW_TITLE, title);
        SetDlgItemTextW(dialog, IDC_WHATS_NEW_TEXT, (const wchar_t *)lparam);
        SendDlgItemMessageW(dialog, IDC_WHATS_NEW_TEXT, EM_SETSEL, 0, 0);
        SetFocus(GetDlgItem(dialog, IDOK));
        return FALSE;
    }
    if (message == WM_COMMAND && (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL)) {
        EndDialog(dialog, 0);
        return TRUE;
    }
    return FALSE;
}

/* "<folder of launcher.ini>\<name>" as a wide path. */
static int beside_settings(const wchar_t *name, wchar_t *out, size_t count)
{
    char utf8[32768], error[256];
    wchar_t settings[32768], *slash;
    if (!mm3_launcher_config_path(utf8, sizeof utf8, error, sizeof error) ||
        !MultiByteToWideChar(CP_UTF8, 0, utf8, -1, settings, (int)(sizeof settings / sizeof settings[0])))
        return 0;
    if (out != settings && name) {
        slash = wcsrchr(settings, L'\\');
        { wchar_t *other = wcsrchr(settings, L'/'); if (other && (!slash || other > slash)) slash = other; }
        if (slash) slash[1] = 0; else settings[0] = 0;
        if (_snwprintf(out, count, L"%s%s", settings, name) < 0) return 0;
        out[count - 1] = 0;
    }
    return 1;
}

void mm3_whats_new(void)
{
    const char *setting = getenv("MM3_WHATS_NEW"), *launcher = getenv("MM3_LAUNCHER");
    wchar_t seen_path[32768], settings_path[32768];
    char seen[64] = "", *text;
    FILE *file;
    HRSRC found;
    HGLOBAL loaded;
    DWORD bytes;
    if ((setting && !strcmp(setting, "0")) || getenv("MM3_RENDERER_HANDOFF") || (launcher && !strcmp(launcher, "0")) ||
        !mm3_update_version_valid(MM3_VERSION) ||
        !beside_settings(L"whats-new-seen.txt", seen_path, sizeof seen_path / sizeof seen_path[0]) ||
        !beside_settings(L"launcher.ini", settings_path, sizeof settings_path / sizeof settings_path[0]))
        return;
    if ((file = _wfopen(seen_path, L"rb")) != NULL) {
        size_t n = fread(seen, 1, sizeof seen - 1, file);
        fclose(file);
        seen[n] = 0;
        while (n && (seen[n - 1] == '\n' || seen[n - 1] == '\r' || seen[n - 1] == ' ')) seen[--n] = 0;
        if (!strcmp(seen, MM3_VERSION) || (mm3_update_version_valid(seen) && mm3_update_compare(MM3_VERSION, seen) <= 0))
            return;                     /* seen already, or an older program started after a newer one */
    } else if (GetFileAttributesW(settings_path) == INVALID_FILE_ATTRIBUTES) {
        seen[0] = 1;                    /* a fresh install: nothing to tell, only remember the version */
    }
    if (seen[0] != 1 && (found = FindResourceW(NULL, MAKEINTRESOURCEW(IDR_WHATS_NEW), (LPCWSTR)RT_RCDATA)) &&
        (loaded = LoadResource(NULL, found)) && (bytes = SizeofResource(NULL, found)) != 0) {
        char *notes = (char *)malloc(bytes + 1);
        text = (char *)malloc(bytes * 2 + 512);
        if (notes && text) {
            memcpy(notes, LockResource(loaded), bytes);
            notes[bytes] = 0;
            if (mm3_whats_new_text(notes, seen, MM3_VERSION, text, bytes * 2 + 512)) {
                int count = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
                wchar_t *wide = count > 0 ? (wchar_t *)malloc(sizeof(wchar_t) * count) : NULL;
                if (wide && MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, count))
                    DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_WHATS_NEW), NULL,
                                    whats_new_proc, (LPARAM)wide);
                free(wide);
            }
        }
        free(notes);
        free(text);
    }
    if ((file = _wfopen(seen_path, L"wb")) != NULL) {
        fputs(MM3_VERSION, file);
        fclose(file);
    }
}
#else
void mm3_whats_new(void) {}
#endif
