#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nv2a_map_mask.h"
#include "native_mask.inc"

typedef unsigned UINT;
typedef long HRESULT;
typedef struct { unsigned refs; uint32_t pixels[128 * 128]; } ID3D11Texture2D;
typedef struct { ID3D11Texture2D *texture; } ID3D11ShaderResourceView;
typedef ID3D11Texture2D ID3D11Resource;
typedef struct { unsigned cube, color, width, height, levels, addr; } NvD3DTexture;
#define DXGI_FORMAT_B8G8R8A8_UNORM 1
#define D3D11_BIND_SHADER_RESOURCE 2
#define FAILED(hr) ((hr) < 0)
static void *s_dev, *s_ctx;
static ID3D11Texture2D host_texture;
static ID3D11ShaderResourceView host_view;
static unsigned allocation_calls, view_calls, upload_calls, release_calls;
static int readable = 1, fail_allocation, fail_view;
static unsigned char guest_mask[16384];

static int guest_ok(unsigned address, unsigned length) {
    return readable && address == 0x10000 && length == sizeof guest_mask;
}
static const unsigned char *guest(unsigned address) {
    assert(address == 0x10000); return guest_mask;
}
static ID3D11Texture2D *make_texture(unsigned w, unsigned h, unsigned mips, unsigned samples,
                                    unsigned format, unsigned bind, unsigned flags) {
    assert(w == 128 && h == 128 && mips == 1 && samples == 1);
    assert(format == 1 && bind == 2 && !flags);
    ++allocation_calls;
    if (fail_allocation) return NULL;
    host_texture.refs = 1; return &host_texture;
}
static void ID3D11DeviceContext_UpdateSubresource(void *ctx, ID3D11Resource *texture,
                  unsigned sub, const void *box, const void *data, unsigned pitch, unsigned depth_pitch) {
    (void)ctx; assert(texture == &host_texture && !sub && !box && pitch == 512 && !depth_pitch);
    memcpy(texture->pixels, data, sizeof texture->pixels); ++upload_calls;
}
static HRESULT ID3D11Device_CreateShaderResourceView(void *dev, ID3D11Resource *texture,
                               const void *descriptor, ID3D11ShaderResourceView **view) {
    (void)dev; assert(texture == &host_texture && !descriptor); ++view_calls;
    if (fail_view) return -1;
    ++texture->refs; host_view.texture = texture; *view = &host_view; return 0;
}
static void ID3D11Texture2D_Release(ID3D11Texture2D *texture) {
    assert(texture == &host_texture && texture->refs); --texture->refs; ++release_calls;
}
#include "production_mask.inc"

int main(int argc, char **argv) {
    NvD3DTexture texture = {0, 0x0F, 128, 128, 1, 0x10000}, changed;
    ID3D11ShaderResourceView *view;
    unsigned x, y;
    assert(argc == 2); fail_allocation = !strcmp(argv[1], "allocation"); fail_view = !strcmp(argv[1], "view");
    memcpy(guest_mask, native_mask, sizeof guest_mask);
    assert(nv2a_map_mask_matches(guest_mask, sizeof guest_mask));
    assert(!nv2a_map_mask_matches(guest_mask, sizeof guest_mask - 1));
    assert(!nv2a_map_mask_matches(NULL, sizeof guest_mask));
    for (x = 0; x < sizeof guest_mask; x += 127) {
        guest_mask[x] ^= 1; assert(!map_square_mask(&texture)); guest_mask[x] ^= 1;
    }
    readable = 0; assert(!map_square_mask(&texture)); readable = 1;
    changed = texture; changed.cube = 1; assert(!map_square_mask(&changed));
    changed = texture; changed.color = 0x0C; assert(!map_square_mask(&changed));
    changed = texture; changed.width = 256; assert(!map_square_mask(&changed));
    changed = texture; changed.height = 64; assert(!map_square_mask(&changed));
    changed = texture; changed.levels = 2; assert(!map_square_mask(&changed));
    assert(!allocation_calls && !upload_calls && !view_calls);
    view = map_square_mask(&texture);
    if (fail_allocation || fail_view) {
        assert(!view && !map_square_mask(&texture) && allocation_calls == 1);
        assert(host_texture.refs == 0);
        assert(release_calls == (unsigned)!fail_allocation);
        puts("PASS square map mask: failed host allocation/view keeps native texture fallback, no repeated allocations");
        return 0;
    }
    assert(view == &host_view && map_square_mask(&texture) == view);
    assert(allocation_calls == 1 && upload_calls == 1 && view_calls == 1 && release_calls == 1 && host_texture.refs == 1);
    for (y = 0; y < 128; ++y) for (x = 0; x < 128; ++x) {
        uint32_t pixel = host_texture.pixels[y * 128 + x];
        assert(!(pixel & 0xFFFFFF));
        assert(pixel == host_texture.pixels[x * 128 + y]);
        assert(pixel == host_texture.pixels[(127 - y) * 128 + 127 - x]);
        if (x >= 8 && x < 120 && y >= 8 && y < 120) assert(pixel == 0xFF000000u);
        if (!x || !y || x == 127 || y == 127) assert(!pixel);
    }
    for (x = 0; x < 8; ++x)
        assert(host_texture.pixels[64 * 128 + x] < host_texture.pixels[64 * 128 + x + 1]);
    assert(host_texture.pixels[12 * 128 + 12] == 0xFF000000u); /* Newly useful corners. */
    assert(!nv2a_map_mask_pixel(128, 64));
    puts("PASS square map mask: retail identity, texture isolation, symmetric opaque corners, smooth transparent edges, cached SRV ownership");
    return 0;
}
