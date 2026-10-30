#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mm3_update.h"

#ifndef NT_SUCCESS
#define NT_SUCCESS(status) ((status) >= 0)
#endif

#define SETUP_NAME "MM3-Setup.exe"
#define RELEASE_LIMIT (2u * 1024u * 1024u)

static void say(char *message, size_t size, const char *text)
{
    if (message && size) snprintf(message, size, "%s", text);
}

/* ------------------------------------------------------------------ versions */

typedef struct Version {
    int numbers[8], count, rank, pre;
} Version;

static int version_parse(const char *text, Version *out)
{
    const char *p = text, *rest;
    char lower[96];
    size_t length;
    memset(out, 0, sizeof *out);
    if (!text) return 0;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == 'v' || *p == 'V') ++p;
    if (!isdigit((unsigned char)*p)) return 0;
    for (;;) {
        long value = 0;
        while (isdigit((unsigned char)*p)) { if (value < 100000000) value = value * 10 + (*p - '0'); ++p; }
        if (out->count < 8) out->numbers[out->count++] = (int)value;
        if (*p == '.' && isdigit((unsigned char)p[1])) ++p; else break;
    }
    while (out->count > 1 && out->numbers[out->count - 1] == 0) --out->count;
    rest = p;
    length = strlen(rest);
    if (length >= sizeof lower) length = sizeof lower - 1;
    for (size_t i = 0; i < length; ++i) lower[i] = (char)tolower((unsigned char)rest[i]);
    lower[length] = 0;
    out->rank = 3; /* a final release sorts after its alpha, beta and rc */
    for (const char *q = lower; *q; ++q) {
        static const struct { const char *name; int rank; } words[] = {
            {"alpha", 0}, {"beta", 1}, {"pre", 1}, {"rc", 2}};
        int found = 0;
        for (int w = 0; w < 4 && !found; ++w) {
            size_t n = strlen(words[w].name);
            if (!strncmp(q, words[w].name, n)) {
                out->rank = words[w].rank;
                q += n;
                while (*q && !isdigit((unsigned char)*q)) ++q;
                out->pre = atoi(q);
                found = 1;
            }
        }
        if (found) break;
    }
    return 1;
}

int mm3_update_version_valid(const char *text)
{
    Version version;
    return version_parse(text, &version);
}

int mm3_update_compare(const char *a, const char *b)
{
    Version x, y;
    if (!version_parse(a, &x) || !version_parse(b, &y)) return 0;
    for (int i = 0; i < 8 && (i < x.count || i < y.count); ++i) {
        int left = i < x.count ? x.numbers[i] : -1, right = i < y.count ? y.numbers[i] : -1;
        if (left != right) return left < right ? -1 : 1;
    }
    if (x.rank != y.rank) return x.rank < y.rank ? -1 : 1;
    if (x.pre != y.pre) return x.pre < y.pre ? -1 : 1;
    return 0;
}

/* ---------------------------------------------------------------------- JSON
 * Just enough to read GitHub's release answer: string values by key. */

/* Finds "key": "value" at or after `from`; copies the unescaped value and
 * returns the position after it, or NULL. */
static const char *json_string(const char *from, const char *key, char *out, size_t size)
{
    char quoted[64];
    size_t used = 0;
    const char *p = from;
    snprintf(quoted, sizeof quoted, "\"%s\"", key);
    for (;;) {
        p = strstr(p, quoted);
        if (!p) return NULL;
        p += strlen(quoted);
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
        if (*p != ':') continue;
        ++p;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
        if (*p == '"') break;
    }
    for (++p; *p && *p != '"'; ++p) {
        char c = *p;
        if (c == '\\' && p[1]) {
            ++p;
            c = *p == 'n' ? '\n' : *p == 't' ? '\t' : *p == 'r' ? '\r' : *p;
            if (*p == 'u') return NULL; /* no real link or tag holds one */
        }
        if (used + 1 >= size) return NULL;
        out[used++] = c;
    }
    if (*p != '"') return NULL;
    out[used] = 0;
    return p + 1;
}

static int ends_with(const char *text, const char *tail)
{
    size_t a = strlen(text), b = strlen(tail);
    return a >= b && !strcmp(text + a - b, tail);
}

int mm3_update_parse_release(const char *json, const char *current, const char *trusted_prefix,
                             MM3Update *update, char *message, size_t message_size)
{
    char tag[64], url[512];
    const char *p;
    size_t prefix;
    int have_setup = 0, have_sum = 0;
    if (!trusted_prefix) trusted_prefix = MM3_UPDATE_TRUSTED_PREFIX;
    prefix = strlen(trusted_prefix);
    memset(update, 0, sizeof *update);
    if (!json || !json_string(json, "tag_name", tag, sizeof tag)) {
        say(message, message_size, "GitHub's answer could not be understood.");
        return -1;
    }
    if (!mm3_update_version_valid(tag) || !mm3_update_version_valid(current)) {
        snprintf(message, message_size, "Cannot compare version \"%s\" with \"%s\".", tag, current ? current : "");
        return -1;
    }
    if (mm3_update_compare(tag, current) <= 0) return 0;
    snprintf(update->version, sizeof update->version, "%s", tag[0] == 'v' || tag[0] == 'V' ? tag + 1 : tag);
    for (p = json; (p = json_string(p, "browser_download_url", url, sizeof url)) != NULL;) {
        int setup = ends_with(url, "/" SETUP_NAME), sum = ends_with(url, "/" SETUP_NAME ".sha256");
        if (!setup && !sum) continue;
        if (strncmp(url, trusted_prefix, prefix)) {
            say(message, message_size, "The release points to a download outside the project, so it was not used.");
            return -1;
        }
        if (setup && !have_setup) { snprintf(update->setup_url, sizeof update->setup_url, "%s", url); have_setup = 1; }
        if (sum && !have_sum) { snprintf(update->sha256_url, sizeof update->sha256_url, "%s", url); have_sum = 1; }
    }
    if (!have_setup || !have_sum) {
        snprintf(message, message_size, "Version %s is published, but its Setup is not attached to the release yet. "
                 "Try again later.", update->version);
        return -1;
    }
    return 1;
}

/* ---------------------------------------------------------------------- HTTP */

/* Receives each piece of an answer; returns 0 to stop. `total` is the length
 * the server announced, or 0. */
typedef int (*HttpSink)(void *context, const unsigned char *data, DWORD size, unsigned long long total);

typedef struct Buffer {
    char *data;
    size_t used, limit;
    int overflow;
} Buffer;

static int buffer_sink(void *context, const unsigned char *data, DWORD size, unsigned long long total)
{
    Buffer *buffer = (Buffer *)context;
    (void)total;
    if (buffer->used + size > buffer->limit) { buffer->overflow = 1; return 0; }
    memcpy(buffer->data + buffer->used, data, size);
    buffer->used += size;
    buffer->data[buffer->used] = 0;
    return 1;
}

static int http_get(const char *url, const char *accept, HttpSink sink, void *context,
                    char *message, size_t message_size, int *stopped)
{
    static const char *const unreachable = "Could not reach GitHub. Check your internet connection and try again.";
    wchar_t wide[1024], host[256], path[1024], headers[128];
    URL_COMPONENTSW parts;
    HINTERNET session = NULL, connection = NULL, request = NULL;
    DWORD status = 0, size = sizeof status, length_size, got;
    DWORD64 total = 0;
    unsigned char chunk[65536];
    int ok = 0;
    *stopped = 0;
    if (!MultiByteToWideChar(CP_UTF8, 0, url, -1, wide, (int)(sizeof wide / sizeof wide[0]))) {
        say(message, message_size, "The update link is not valid.");
        return 0;
    }
    memset(&parts, 0, sizeof parts);
    parts.dwStructSize = sizeof parts;
    parts.lpszHostName = host; parts.dwHostNameLength = (DWORD)(sizeof host / sizeof host[0]);
    parts.lpszUrlPath = path; parts.dwUrlPathLength = (DWORD)(sizeof path / sizeof path[0]);
    if (!WinHttpCrackUrl(wide, 0, 0, &parts) ||
        (parts.nScheme != INTERNET_SCHEME_HTTPS && parts.nScheme != INTERNET_SCHEME_HTTP)) {
        say(message, message_size, "The update link is not valid.");
        return 0;
    }
    session = WinHttpOpen(L"MM3-Launcher", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) /* before Windows 8.1 */
        session = WinHttpOpen(L"MM3-Launcher", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { say(message, message_size, unreachable); return 0; }
    WinHttpSetTimeouts(session, 10000, 10000, 20000, 20000);
    connection = WinHttpConnect(session, host, parts.nPort, 0);
    if (connection)
        request = WinHttpOpenRequest(connection, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (request) {
        _snwprintf(headers, sizeof headers / sizeof headers[0], L"Accept: %hs", accept);
        headers[sizeof headers / sizeof headers[0] - 1] = 0;
        WinHttpAddRequestHeaders(request, headers, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    }
    if (!request || !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, NULL)) {
        say(message, message_size, unreachable);
    } else if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
        say(message, message_size, unreachable);
    } else if (status == 403 || status == 429) {
        say(message, message_size, "GitHub is limiting requests from this connection. Try again in a while.");
    } else if (status == 404) {
        say(message, message_size, "No Setup release was found on GitHub.");
    } else if (status != 200) {
        snprintf(message, message_size, "GitHub answered with error %lu.", (unsigned long)status);
    } else {
        DWORD announced = 0;
        length_size = sizeof announced;
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &announced, &length_size, WINHTTP_NO_HEADER_INDEX))
            total = announced;
        ok = 1;
        for (;;) {
            if (!WinHttpReadData(request, chunk, sizeof chunk, &got)) {
                say(message, message_size, "The connection to GitHub was lost. Try again.");
                ok = 0; break;
            }
            if (!got) break;
            if (!sink(context, chunk, got, total)) { *stopped = 1; ok = 0; break; }
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok;
}

int mm3_update_check(const char *api_url, const char *current, const char *trusted_prefix,
                     MM3Update *update, char *message, size_t message_size)
{
    Buffer buffer = {0};
    int stopped, result;
    buffer.limit = RELEASE_LIMIT;
    buffer.data = (char *)malloc(buffer.limit + 1);
    memset(update, 0, sizeof *update);
    if (!buffer.data) { say(message, message_size, "Not enough memory to check for updates."); return -1; }
    buffer.data[0] = 0;
    if (!http_get(api_url, "application/vnd.github+json", buffer_sink, &buffer, message, message_size, &stopped)) {
        if (stopped && buffer.overflow) say(message, message_size, "GitHub's answer could not be understood.");
        free(buffer.data);
        return -1;
    }
    result = mm3_update_parse_release(buffer.data, current, trusted_prefix, update, message, message_size);
    free(buffer.data);
    return result;
}

/* ------------------------------------------------------------------ download */

typedef struct Sha256 {
    BCRYPT_ALG_HANDLE algorithm;
    BCRYPT_HASH_HANDLE hash;
    PUCHAR object;
} Sha256;

static int sha_open(Sha256 *sha)
{
    DWORD size = 0, got = 0;
    memset(sha, 0, sizeof *sha);
    if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&sha->algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0))) return 0;
    if (!NT_SUCCESS(BCryptGetProperty(sha->algorithm, BCRYPT_OBJECT_LENGTH, (PUCHAR)&size, sizeof size, &got, 0)) ||
        !(sha->object = (PUCHAR)malloc(size)) ||
        !NT_SUCCESS(BCryptCreateHash(sha->algorithm, &sha->hash, sha->object, size, NULL, 0, 0))) {
        if (sha->object) free(sha->object);
        BCryptCloseAlgorithmProvider(sha->algorithm, 0);
        memset(sha, 0, sizeof *sha);
        return 0;
    }
    return 1;
}

static void sha_close(Sha256 *sha)
{
    if (sha->hash) BCryptDestroyHash(sha->hash);
    if (sha->algorithm) BCryptCloseAlgorithmProvider(sha->algorithm, 0);
    free(sha->object);
    memset(sha, 0, sizeof *sha);
}

typedef struct Download {
    HANDLE file;
    Sha256 sha;
    unsigned long long done;
    MM3UpdateProgress progress;
    void *context;
    int failed;
} Download;

static int download_sink(void *context, const unsigned char *data, DWORD size, unsigned long long total)
{
    Download *download = (Download *)context;
    DWORD written;
    if (!WriteFile(download->file, data, size, &written, NULL) || written != size) { download->failed = 1; return 0; }
    if (!NT_SUCCESS(BCryptHashData(download->sha.hash, (PUCHAR)data, size, 0))) { download->failed = 1; return 0; }
    download->done += size;
    return download->progress ? download->progress(download->context, download->done, total) : 1;
}

int mm3_update_download(const MM3Update *update, const wchar_t *target, MM3UpdateProgress progress,
                        void *context, char *message, size_t message_size)
{
    Buffer sum = {0};
    char text[4096 + 1], expected[65], actual[65];
    wchar_t part[1100], folder[1100];
    unsigned char digest[32], header[2];
    Download download;
    int stopped, ok = 0;
    DWORD got;
    wchar_t *slash;
    sum.data = text; sum.limit = 4096;
    text[0] = 0;
    if (!http_get(update->sha256_url, "*/*", buffer_sink, &sum, message, message_size, &stopped)) return 0;
    {
        size_t n = 0;
        while (text[n] && isxdigit((unsigned char)text[n]) && n < 64) { expected[n] = (char)tolower((unsigned char)text[n]); ++n; }
        expected[n] = 0;
        if (n != 64 || (text[n] && !isspace((unsigned char)text[n]))) {
            say(message, message_size, "The release's checksum file is not valid, so the update was not used.");
            return 0;
        }
    }
    if (wcslen(target) + 6 >= sizeof part / sizeof part[0]) { say(message, message_size, "The update folder's name is too long."); return 0; }
    _snwprintf(part, sizeof part / sizeof part[0], L"%ls.part", target);
    part[sizeof part / sizeof part[0] - 1] = 0;
    wcsncpy(folder, target, sizeof folder / sizeof folder[0] - 1);
    folder[sizeof folder / sizeof folder[0] - 1] = 0;
    slash = wcsrchr(folder, L'\\');
    if (!slash) slash = wcsrchr(folder, L'/');
    if (slash) { *slash = 0; CreateDirectoryW(folder, NULL); }
    memset(&download, 0, sizeof download);
    download.progress = progress; download.context = context;
    download.file = CreateFileW(part, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (download.file == INVALID_HANDLE_VALUE) {
        say(message, message_size, "Could not save the update next to the game. Check that its folder can be written to.");
        return 0;
    }
    if (!sha_open(&download.sha)) {
        CloseHandle(download.file); DeleteFileW(part);
        say(message, message_size, "Windows could not start its checksum service.");
        return 0;
    }
    if (http_get(update->setup_url, "*/*", download_sink, &download, message, message_size, &stopped)) {
        ok = 1;
    } else if (download.failed) {
        say(message, message_size, "Could not save the update (disk full?).");
    } else if (stopped) {
        say(message, message_size, "The update was cancelled.");
    }
    if (ok) {
        static const char hex[] = "0123456789abcdef";
        if (!NT_SUCCESS(BCryptFinishHash(download.sha.hash, digest, sizeof digest, 0))) ok = 0;
        for (int i = 0; ok && i < 32; ++i) { actual[2 * i] = hex[digest[i] >> 4]; actual[2 * i + 1] = hex[digest[i] & 15]; }
        actual[64] = 0;
        if (ok && strcmp(actual, expected)) {
            say(message, message_size, "The download does not match the release's checksum, so it was discarded.");
            ok = 0;
        }
        if (ok) {
            CloseHandle(download.file);
            download.file = CreateFileW(part, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (download.file == INVALID_HANDLE_VALUE || !ReadFile(download.file, header, 2, &got, NULL) ||
                got != 2 || header[0] != 'M' || header[1] != 'Z') {
                say(message, message_size, "The download is not a Windows program, so it was discarded.");
                ok = 0;
            }
        }
    }
    sha_close(&download.sha);
    if (download.file != INVALID_HANDLE_VALUE) CloseHandle(download.file);
    if (ok && !MoveFileExW(part, target, MOVEFILE_REPLACE_EXISTING)) {
        say(message, message_size, "Could not put the new Setup in place. Is an earlier downloaded Setup still open?");
        ok = 0;
    }
    if (!ok) DeleteFileW(part);
    return ok;
}
