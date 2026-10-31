#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <wincodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mm3_photo.h"

/* A picture of what the window shows, saved as a PNG: the key thread
 * (src/mm3_cheat_keys.c) or the free camera's controller button asks, the
 * renderer hands over the next image it presents (nv2a_d3d_request_screenshot,
 * on its own thread, inside the frame), and a thread of ours compresses and
 * writes it, so the game never waits for the disk.
 *
 * The file goes to Pictures\Midtown Madness 3\MM3-yyyymmdd-hhmmss-mmm.png, or
 * the folder in MM3_PHOTO_DIR. What the window shows is what is saved, at the
 * window's size and with the HUD and any menu on it. */
typedef void (*nv2a_screenshot_fn)(const void *bgra, uint32_t width, uint32_t height, uint32_t pitch);
extern void nv2a_d3d_request_screenshot(nv2a_screenshot_fn take);

typedef struct Job {
    wchar_t path[MAX_PATH * 2];
    uint32_t width, height;
    uint8_t *pixels;                 /* the renderer's BGRA image, copied */
    uint32_t stride;
} Job;

static wchar_t s_path[MAX_PATH * 2];         /* the picture asked for, until served */
static volatile LONG s_busy;                 /* 1 from the request to the callback */
static DWORD s_last_tick;

int mm3_photo_save_png(const wchar_t *path, const void *bgra, uint32_t width, uint32_t height, uint32_t pitch)
{
    HRESULT init, hr = E_FAIL;
    IWICImagingFactory *factory = NULL;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    IWICStream *stream = NULL;
    IPropertyBag2 *options = NULL;
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    uint32_t stride = (width * 3 + 3) & ~3u, x, y;
    uint8_t *rgb;
    int ok = 0;
    if (!path || !bgra || !width || !height || pitch < width * 4) return 0;
    rgb = (uint8_t *)malloc((size_t)stride * height);
    if (!rgb) return 0;
    for (y = 0; y < height; ++y) {
        const uint8_t *in = (const uint8_t *)bgra + (size_t)y * pitch;
        uint8_t *out = rgb + (size_t)y * stride;
        for (x = 0; x < width; ++x) {
            out[x * 3] = in[x * 4];
            out[x * 3 + 1] = in[x * 4 + 1];
            out[x * 3 + 2] = in[x * 4 + 2];
        }
    }
    init = CoInitializeEx(NULL, COINIT_MULTITHREADED);   /* S_FALSE: already, RPC_E_CHANGED_MODE: apartment, both usable */
    if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
                                (void **)&factory))) goto done;
    if (FAILED(IWICImagingFactory_CreateStream(factory, &stream))) goto done;
    if (FAILED(IWICStream_InitializeFromFilename(stream, path, GENERIC_WRITE))) goto done;
    if (FAILED(IWICImagingFactory_CreateEncoder(factory, &GUID_ContainerFormatPng, NULL, &encoder))) goto done;
    if (FAILED(IWICBitmapEncoder_Initialize(encoder, (IStream *)stream, WICBitmapEncoderNoCache))) goto done;
    if (FAILED(IWICBitmapEncoder_CreateNewFrame(encoder, &frame, &options))) goto done;
    if (FAILED(IWICBitmapFrameEncode_Initialize(frame, options))) goto done;
    if (FAILED(IWICBitmapFrameEncode_SetSize(frame, width, height))) goto done;
    if (FAILED(IWICBitmapFrameEncode_SetPixelFormat(frame, &format))) goto done;
    if (!IsEqualGUID(&format, &GUID_WICPixelFormat24bppBGR)) goto done;
    hr = IWICBitmapFrameEncode_WritePixels(frame, height, stride, stride * height, rgb);
    if (FAILED(hr)) goto done;
    if (FAILED(IWICBitmapFrameEncode_Commit(frame))) goto done;
    if (FAILED(IWICBitmapEncoder_Commit(encoder))) goto done;
    ok = 1;
done:
    if (options) IPropertyBag2_Release(options);
    if (frame) IWICBitmapFrameEncode_Release(frame);
    if (encoder) IWICBitmapEncoder_Release(encoder);
    if (stream) IWICStream_Release(stream);
    if (factory) IWICImagingFactory_Release(factory);
    if (SUCCEEDED(init)) CoUninitialize();
    free(rgb);
    if (!ok) DeleteFileW(path);
    return ok;
}

static void narrow(const wchar_t *w, char *out, size_t size)
{
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)size, NULL, NULL)) out[0] = 0;
}

static DWORD WINAPI save_thread(void *argument)
{
    Job *job = (Job *)argument;
    char shown[MAX_PATH * 3];
    narrow(job->path, shown, sizeof shown);
    if (mm3_photo_save_png(job->path, job->pixels, job->width, job->height, job->stride)) {
        fprintf(stderr, "[MM3_PHOTO] saved %s (%ux%u)\n", shown, job->width, job->height);
    } else {
        fprintf(stderr, "[MM3_PHOTO] could not write %s\n", shown);
        MessageBeep(MB_ICONHAND);
    }
    fflush(stderr);
    free(job->pixels);
    free(job);
    return 0;
}

/* The renderer's thread, inside the frame: keep a copy and go. */
static void serve(const void *bgra, uint32_t width, uint32_t height, uint32_t pitch)
{
    Job *job = NULL;
    if (bgra && width && height && pitch >= width * 4 && width <= 16384 && height <= 16384)
        job = (Job *)calloc(1, sizeof *job);
    if (job) {
        uint32_t y;
        job->width = width;
        job->height = height;
        job->stride = width * 4;
        job->pixels = (uint8_t *)malloc((size_t)job->stride * height);
        if (job->pixels) {
            for (y = 0; y < height; ++y)
                memcpy(job->pixels + (size_t)y * job->stride, (const uint8_t *)bgra + (size_t)y * pitch, job->stride);
            wcscpy_s(job->path, sizeof job->path / sizeof *job->path, s_path);
        } else {
            free(job);
            job = NULL;
        }
    }
    InterlockedExchange(&s_busy, 0);
    if (job) {
        HANDLE thread = CreateThread(NULL, 0, save_thread, job, 0, NULL);
        if (thread) {
            SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL);
            CloseHandle(thread);
            return;
        }
        free(job->pixels);
        free(job);
    }
    fprintf(stderr, "[MM3_PHOTO] the picture could not be taken\n");
    fflush(stderr);
    MessageBeep(MB_ICONHAND);
}

static int folder(wchar_t *out, size_t count)
{
    const wchar_t *custom = _wgetenv(L"MM3_PHOTO_DIR");
    wchar_t pictures[MAX_PATH];
    if (custom && *custom) {
        if (wcscpy_s(out, count, custom)) return 0;
    } else {
        if (FAILED(SHGetFolderPathW(NULL, CSIDL_MYPICTURES | CSIDL_FLAG_CREATE, NULL, SHGFP_TYPE_CURRENT, pictures)) ||
            swprintf_s(out, count, L"%ls\\Midtown Madness 3", pictures) < 0) return 0;
    }
    CreateDirectoryW(out, NULL);       /* exists already: fine */
    return GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES;
}

void mm3_photo_take(void)
{
    SYSTEMTIME t;
    wchar_t dir[MAX_PATH * 2];
    DWORD now = GetTickCount();
    if (s_last_tick && now - s_last_tick < 500u) return;       /* a double press is one picture */
    /* One waiting for the renderer already; after five seconds the window is
     * showing nothing to serve it with, and a new request takes its place. */
    if (InterlockedCompareExchange(&s_busy, 1, 0) && now - s_last_tick < 5000u) return;
    s_last_tick = now;
    if (!folder(dir, sizeof dir / sizeof *dir)) {
        fprintf(stderr, "[MM3_PHOTO] no folder to save the picture in\n");
        fflush(stderr);
        MessageBeep(MB_ICONHAND);
        InterlockedExchange(&s_busy, 0);
        return;
    }
    GetLocalTime(&t);
    swprintf_s(s_path, sizeof s_path / sizeof *s_path, L"%ls\\MM3-%04u%02u%02u-%02u%02u%02u-%03u.png", dir,
               t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    MessageBeep(MB_ICONASTERISK);       /* the game shows nothing: a sound says it was taken */
    nv2a_d3d_request_screenshot(serve);
}
