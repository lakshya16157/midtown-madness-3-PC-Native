#ifndef MM3_UPDATE_H
#define MM3_UPDATE_H

#include <stddef.h>
#include <wchar.h>

/* "Check for updates": ask GitHub for the newest release of this project and,
 * if it is newer than this build, fetch the MM3-Setup.exe attached to it.
 * Setup.exe's own button (installer/installer.py) follows the same rules:
 * pre-releases are skipped, the files must come from this repository's
 * release downloads, and the download must match the release's published
 * SHA-256 (MM3-Setup.exe.sha256: "<hex>  MM3-Setup.exe"). */
#define MM3_UPDATE_API "https://api.github.com/repos/lakshya16157/midtown-madness-3-PC-Native/releases/latest"
#define MM3_UPDATE_TRUSTED_PREFIX "https://github.com/lakshya16157/midtown-madness-3-PC-Native/releases/download/"
#define MM3_UPDATE_PAGE "https://github.com/lakshya16157/midtown-madness-3-PC-Native/releases/latest"

typedef struct MM3Update {
    char version[32];       /* "2.1", the release tag without its v */
    char setup_url[512];
    char sha256_url[512];
} MM3Update;

/* Version names order as 2.0 < 2.0.1 < 2.1, and a beta before its final
 * ("2.0 beta 2", "v2.0-beta.1" < "2.0"). Returns -1, 0 or 1; 0 when either
 * text holds no number (check mm3_update_version_valid first). */
int mm3_update_version_valid(const char *text);
int mm3_update_compare(const char *a, const char *b);

/* The release JSON GitHub's "latest" call answers. Returns 1 and fills
 * `update` when it names a version newer than `current`, 0 when it does not,
 * -1 (with a message for the player) when it cannot be used. Download links
 * must begin with trusted_prefix. */
int mm3_update_parse_release(const char *json, const char *current, const char *trusted_prefix,
                             MM3Update *update, char *message, size_t message_size);

/* parse_release over a download of api_url. Same results. Blocks for up to a
 * few seconds; call it from a worker thread. */
int mm3_update_check(const char *api_url, const char *current, const char *trusted_prefix,
                     MM3Update *update, char *message, size_t message_size);

/* progress(context, done, total) runs for each piece of the download (total is
 * 0 when unknown); returning 0 stops it. The file is written as target.part,
 * checked against the published SHA-256 and the Windows program header, and
 * only then moved to `target` (replacing it). Returns 1 on success; 0 with a
 * message otherwise, leaving no partial file. */
typedef int (*MM3UpdateProgress)(void *context, unsigned long long done, unsigned long long total);
int mm3_update_download(const MM3Update *update, const wchar_t *target,
                        MM3UpdateProgress progress, void *context, char *message, size_t message_size);

#endif
