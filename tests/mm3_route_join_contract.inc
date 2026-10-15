/* Pixel coverage at an elbow catches gaps that centreline tests miss. */
static void route_join_contract(void)
{
    const RoadPoint ring[] = {{20,0,0}, {0,0,0}, {0,0,20}, {20,0,20}, {20,0,0}};
    const RoadPoint duplicate[] = {{20,0,0}, {0,0,0}, {0,0,0}, {0,0,20}};
    const RoadPoint shifted[] = {{140,0,100}, {100,0,100}, {100,0,140}};
    const RoadPoint outside[] = {{-2000,0,0}, {-2020,0,0}, {-2020,0,20}};
    const float fractions[] = {.13f, .37f, .63f, .87f};
    int saved_improved = improved;
    unsigned turn, sample, i, vertex;
    improved = 1; s_road_layer = 0;
    check = "blue left/right 45/90/135 degree elbows fill exterior gap";
    for (turn = 0; turn < 6; ++turn) {
        float angle = (float)(turn % 3 + 1) * .7853981634f * (turn < 3 ? 1 : -1);
        RoadPoint corner[] = {{20,0,0}, {0,0,0}, {-20*cosf(angle),0,20*sinf(angle)}};
        draw_test_polyline(corner, 3, 56);
        assert(route_segment_draw_count() == 2 && draw_count > 2 && draw_count <= 6);
        for (sample = 0; sample < sizeof fractions / sizeof fractions[0]; ++sample) {
            float arc = (angle > 0 ? -1.5707963268f : 1.5707963268f) + angle * fractions[sample];
            /* These points belong to neither butt-ended segment. They must
             * be blue once, including beyond the old straight bevel chord. */
            assert(road_join_covers(1.25f*.9f*cosf(arc),1.25f*.9f*sinf(arc)) == 1);
        }
        for (i = 0; i < draw_count; ++i) {
            Draw *draw = draws + i;
            near(draw->color[0], .12f); near(draw->color[1], .58f);
            near(draw->color[2], 1); near(draw->color[3], .8f);
            if (draw->xy[0][0] != draw->xy[3][0] || draw->xy[0][1] != draw->xy[3][1]) continue;
            for (vertex = 0; vertex < 4; ++vertex)
                assert(hypotf(draw->xy[vertex][0],draw->xy[vertex][1]) <= 1.2501f);
            assert((draw->xy[1][0] - draw->xy[0][0]) * (draw->xy[2][1] - draw->xy[0][1]) -
                   (draw->xy[1][1] - draw->xy[0][1]) * (draw->xy[2][0] - draw->xy[0][0]) < 0);
        }
    }
    check = "blue repeated junction and exact loop seam remain filled";
    draw_test_polyline(duplicate,4,56);
    assert(route_segment_draw_count() == 2 && road_join_covers(.7f,-.6f) == 1);
    draw_test_polyline(ring,5,56);
    assert(route_segment_draw_count() == 4 && draw_count == 12);
    assert(road_join_covers(-20.7f,-.6f) == 1 && !road_join_covers(-10,10));
    check = "blue elbow shares translated rotated rectangular map camera";
    for (turn = 0; turn < 8; ++turn) {
        float angle = (float)turn * .7853981634f;
        float cosine = cosf(angle), sine = sinf(angle);
        abi_start(0);
        route_polyline(0x25000,0,shifted,3,(RoadPoint){100,0,100},.5f,
                       cosine,sine,16,32,128,96,.8f);
        assert(route_segment_draw_count() == 2 && draw_count == 4);
        assert(road_join_covers(128 + .7f*cosine - .6f*sine,96 - .6f*cosine - .7f*sine) == 1);
        assert(g_esp == stack && g_fp_top == 0);
    }
    check = "blue join triangles survive native packing and route tags";
    native_submission_begin(); native_submission = 1;
    draw_test_polyline(duplicate,4,56);
    native_submission = 0;
    assert(draw_count == 4 && MEM32(0x3C568C) == 16);
    for (i = 0; i < MEM32(0x3C568C); ++i) {
        uint32_t packed = packed_vertices + i * 16;
        assert((int16_t)MEM16(packed + 8) == ROUTE_TAG_U);
        assert((int16_t)MEM16(packed + 0xA) == ROUTE_TAG_V);
    }
    check = "blue off-map elbows are rejected";
    draw_test_polyline(outside,3,56); assert(!draw_count);
    improved = saved_improved;
    puts("PASS blue route joins: both turn directions, 45/90/135 degrees, exterior pixel coverage, bounded round arc/winding/color/alpha, duplicate samples, closed seam, rotated/rectangular camera, native packing/tags, clipping and guest ABI");
}
