/* The launcher's "Check for updates": version order, release parsing, and the
 * real WinHTTP/SHA-256 paths against a local stand-in for GitHub. argv[1] is
 * the stand-in's port, argv[2] a scratch folder. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mm3_update.h"

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "FAIL update line %d: %s\n", __LINE__, #test); return 1; } } while (0)

static int s_progress_calls;
static unsigned long long s_last_done;

static int count_progress(void *context, unsigned long long done, unsigned long long total)
{
    (void)context; (void)total;
    ++s_progress_calls; s_last_done = done;
    return 1;
}

static int stop_progress(void *context, unsigned long long done, unsigned long long total)
{
    (void)context; (void)done; (void)total;
    ++s_progress_calls;
    return 0;
}

static void api(char *url, size_t size, const char *port, const char *scenario)
{
    snprintf(url, size, "http://127.0.0.1:%s/%s/latest", port, scenario);
}

static int versions(void)
{
    static const char *const order[] = {"2.0 beta 0", "v2.0-beta.1", "2.0 beta 2", "2.0", "v2.0.1", "2.1", "2.10", "3"};
    for (int i = 0; i < (int)(sizeof order / sizeof order[0]); ++i) {
        CHECK(mm3_update_version_valid(order[i]));
        for (int j = 0; j < (int)(sizeof order / sizeof order[0]); ++j)
            CHECK(mm3_update_compare(order[i], order[j]) == (i < j ? -1 : i > j ? 1 : 0));
    }
    CHECK(mm3_update_compare("v2.0", "2.0.0") == 0);
    CHECK(!mm3_update_version_valid("latest") && !mm3_update_version_valid("") && !mm3_update_version_valid(NULL));
    CHECK(mm3_update_compare("latest", "2.0") == 0);
    return 0;
}

static int parsing(void)
{
    static const char *const good =
        "{\"url\":\"x\",\"tag_name\" : \"v2.1\",\"body\":\"says \\\"tag_name\\\": \\\"v9\\\"\",\"assets\":["
        "{\"name\":\"MM3-Setup.exe.sha256\",\"browser_download_url\":\"https://github.com/o/r/releases/download/v2.1/MM3-Setup.exe.sha256\"},"
        "{\"name\":\"MM3-Setup.exe\",\"size\":5,\"browser_download_url\":\"https://github.com/o/r/releases/download/v2.1/MM3-Setup.exe\"},"
        "{\"name\":\"notes.txt\",\"browser_download_url\":\"https://elsewhere.example/notes.txt\"}]}";
    MM3Update update;
    char message[256] = "";
    CHECK(mm3_update_parse_release(good, "2.0", "https://github.com/o/r/releases/download/", &update, message, sizeof message) == 1);
    CHECK(!strcmp(update.version, "2.1"));
    CHECK(!strcmp(update.setup_url, "https://github.com/o/r/releases/download/v2.1/MM3-Setup.exe"));
    CHECK(!strcmp(update.sha256_url, "https://github.com/o/r/releases/download/v2.1/MM3-Setup.exe.sha256"));
    CHECK(mm3_update_parse_release(good, "2.1", "", &update, message, sizeof message) == 0);
    CHECK(mm3_update_parse_release(good, "2.2", "", &update, message, sizeof message) == 0);
    /* Downloads outside the project, a missing checksum, junk. */
    CHECK(mm3_update_parse_release(good, "2.0", "https://github.com/someone/else/", &update, message, sizeof message) == -1 && *message);
    CHECK(mm3_update_parse_release("{\"tag_name\":\"v2.1\",\"assets\":[]}", "2.0", "", &update, message, sizeof message) == -1);
    CHECK(mm3_update_parse_release("not json", "2.0", "", &update, message, sizeof message) == -1);
    CHECK(mm3_update_parse_release("{\"tag_name\":\"nightly\",\"assets\":[]}", "2.0", "", &update, message, sizeof message) == -1);
    CHECK(mm3_update_parse_release(good, "weird", "", &update, message, sizeof message) == -1);
    /* A NULL prefix means this project's own releases. */
    CHECK(mm3_update_parse_release(good, "2.0", NULL, &update, message, sizeof message) == -1);
    return 0;
}

static int network(const char *port, const char *folder)
{
    char url[256], message[256], bytes[64];
    wchar_t target[600];
    MM3Update update;
    FILE *file;
    size_t got;

    api(url, sizeof url, port, "same");
    CHECK(mm3_update_check(url, "2.1", "", &update, message, sizeof message) == 0);
    api(url, sizeof url, port, "newer");
    CHECK(mm3_update_check(url, "2.1", "", &update, message, sizeof message) == 1);
    CHECK(!strcmp(update.version, "99.0"));
    CHECK(mm3_update_check(url, "2.1", "https://github.com/", &update, message, sizeof message) == -1);
    api(url, sizeof url, port, "nosum");
    CHECK(mm3_update_check(url, "2.1", "", &update, message, sizeof message) == -1 && strstr(message, "not attached"));
    api(url, sizeof url, port, "missing");
    CHECK(mm3_update_check(url, "2.1", "", &update, message, sizeof message) == -1 && strstr(message, "No Setup release"));
    api(url, sizeof url, port, "junk");
    CHECK(mm3_update_check(url, "2.1", "", &update, message, sizeof message) == -1);
    snprintf(url, sizeof url, "http://127.0.0.1:1/newer/latest");   /* nothing listens there */
    CHECK(mm3_update_check(url, "2.1", "", &update, message, sizeof message) == -1 && strstr(message, "Could not reach"));

    /* A good download is verified and moved into place, replacing an older file. */
    swprintf(target, sizeof target / sizeof target[0], L"%hs\\updates\\MM3-Setup.exe", folder);
    api(url, sizeof url, port, "newer");
    CHECK(mm3_update_check(url, "2.1", "", &update, message, sizeof message) == 1);
    s_progress_calls = 0; s_last_done = 0;
    CHECK(mm3_update_download(&update, target, count_progress, NULL, message, sizeof message) == 1);
    CHECK(s_progress_calls >= 1 && s_last_done == 3000000);
    file = _wfopen(target, L"rb");
    CHECK(file);
    got = fread(bytes, 1, sizeof bytes, file);
    fclose(file);
    CHECK(got == sizeof bytes && bytes[0] == 'M' && bytes[1] == 'Z');
    {
        wchar_t part[620];
        swprintf(part, sizeof part / sizeof part[0], L"%ls.part", target);
        CHECK(GetFileAttributesW(part) == INVALID_FILE_ATTRIBUTES);
    }

    /* Everything wrong with a download leaves the good file alone and no .part behind. */
    static const char *const bad[] = {"badsum", "notexe", "badsumfile", "short"};
    for (int i = 0; i < (int)(sizeof bad / sizeof bad[0]); ++i) {
        wchar_t part[620], other[600];
        swprintf(other, sizeof other / sizeof other[0], L"%hs\\updates\\other-%d.exe", folder, i);
        api(url, sizeof url, port, bad[i]);
        CHECK(mm3_update_check(url, "2.1", "", &update, message, sizeof message) == 1);
        message[0] = 0;
        CHECK(mm3_update_download(&update, other, NULL, NULL, message, sizeof message) == 0 && *message);
        swprintf(part, sizeof part / sizeof part[0], L"%ls.part", other);
        CHECK(GetFileAttributesW(part) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW(other) == INVALID_FILE_ATTRIBUTES);
    }
    /* Cancelling from the progress callback stops it and cleans up. */
    {
        wchar_t part[620], other[600];
        swprintf(other, sizeof other / sizeof other[0], L"%hs\\updates\\cancelled.exe", folder);
        api(url, sizeof url, port, "newer");
        CHECK(mm3_update_check(url, "2.1", "", &update, message, sizeof message) == 1);
        s_progress_calls = 0;
        CHECK(mm3_update_download(&update, other, stop_progress, NULL, message, sizeof message) == 0 && strstr(message, "cancelled"));
        CHECK(s_progress_calls == 1);
        swprintf(part, sizeof part / sizeof part[0], L"%ls.part", other);
        CHECK(GetFileAttributesW(part) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW(other) == INVALID_FILE_ATTRIBUTES);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: update_contract <port> <folder>\n"); return 2; }
    if (versions() || parsing() || network(argv[1], argv[2])) return 1;
    puts("PASS update: version order, release parsing and trusted links, WinHTTP check and checked download "
         "(good, wrong checksum, not a program, bad checksum file, short, cancelled, offline)");
    return 0;
}
