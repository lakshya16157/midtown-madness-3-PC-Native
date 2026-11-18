/* nv2a_texture_pack.h against a pack written by tools/hud/hd_hud.py
 * (tests/texture_pack_regression.py). argv: guest width, height, the guest
 * bytes' file, then the expected level width at scales 1, 2.5 and 8. */
#include <assert.h>
#include "video/nv2a_texture_pack.h"

static uint8_t guest[1 << 20];

int main(int argc, char **argv)
{
    uint32_t gw, gh, w = 0, h = 0;
    size_t pitch = 0, n;
    int bc3 = -1, i;
    float scales[3] = {1.0f, 2.5f, 8.0f};
    FILE *f;
    uint64_t hash;
    assert(argc == 7);
    gw = (uint32_t)atoi(argv[1]); gh = (uint32_t)atoi(argv[2]);
    f = fopen(argv[3], "rb");
    assert(f);
    n = fread(guest, 1, sizeof guest, f);
    fclose(f);
    assert(nv_pack_init() == 1);
    assert(nv_pack_size_known(gw, gh) && !nv_pack_size_known(gw + 4, gh));
    hash = nv_pack_fnv(guest, n);
    assert(!nv_pack_load(gw, gh, hash ^ 1, 1.0f, &w, &h, &pitch, &bc3));   /* other content */
    for (i = 0; i < 3; i++) {
        uint8_t *data = nv_pack_load(gw, gh, hash, scales[i], &w, &h, &pitch, &bc3);
        assert(data && bc3 == 1);
        assert(w == (uint32_t)atoi(argv[4 + i]) && h * gw == w * gh);
        assert(pitch == (size_t)(w / 4) * 16);
        free(data);
    }
    puts("texture pack contract passed");
    return 0;
}
