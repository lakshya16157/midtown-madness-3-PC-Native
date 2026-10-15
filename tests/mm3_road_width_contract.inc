/* Physical roadway widths must survive graph caching and screen-space
 * simplification. The native submission capture measures the resulting quads. */
static float road_width_draw_half(const Draw *draw)
{
    return .5f * hypotf(draw->xy[0][0] - draw->xy[3][0],
                       draw->xy[0][1] - draw->xy[3][1]);
}

static void road_width_profile(uint32_t profile, uint32_t records, uint32_t controls,
                               const uint16_t *lateral, unsigned count)
{
    unsigned i;
    memset((void *)XBOX_PTR(profile),0,0x3C);
    MEM32(profile + 4) = records; MEM32(profile + 8) = records + (count + 2) * 7;
    MEMF(profile + 0x20) = -10; MEMF(profile + 0x38) = 6553.5f;
    for (i = 0; i < count + 2; ++i) {
        uint32_t record = records + i * 7, points = controls + i * 12;
        MEM16(record) = 2; MEM32(record + 2) = points;
        MEM8(record + 6) = i < count ? 0xA1 : 0;
        /* All first endpoints coincide. Only the last endpoint carries
         * the lane offset used by native 1545E7. Far sidewalk endpoints
         * must have no influence on the inferred carriageway width. */
        MEM16(points) = 300; MEM16(points + 2) = MEM16(points + 4) = 0;
        MEM16(points + 6) = i < count ? lateral[i] : i == count ? 0 : 6000;
        MEM16(points + 8) = MEM16(points + 10) = 0;
    }
}

static void road_width_native_profile_contract(void)
{
    const uint16_t single[] = {100};
    const uint16_t four[] = {220,100,180,140};
    const uint16_t median[] = {380,100,340,140};
    const uint32_t profiles[3] = {0x90000,0x90100,0x90200};
    const uint32_t road = 0x94000, positions = 0x94100, sections = 0x94200;
    RoadCurve curve = {0};
    road_width_profile(profiles[0],0x91000,0x92000,single,1);
    road_width_profile(profiles[1],0x91100,0x92100,four,4);
    road_width_profile(profiles[2],0x91200,0x92200,median,4);
    near(road_template_width(profiles[0]),3.5f);
    near(road_template_width(profiles[1]),16);
    near(road_template_width(profiles[2]),32);
    near(road_template_width(0),6);
    MEM32(profiles[2] + 8) = MEM32(profiles[2] + 4) + 1;
    near(road_template_width(profiles[2]),6);

    memset((void *)XBOX_PTR(road),0,0xDC); curve.object = road;
    MEM32(road + 0x80) = positions; MEM32(road + 0x84) = positions + 8;
    MEM32(road + 0x90) = sections; MEM32(road + 0x94) = sections + 8;
    MEMF(road + 0xCC) = 200;
    MEMF(positions) = 0; MEMF(positions + 4) = 100;
    MEM32(sections) = profiles[0]; MEM32(sections + 4) = profiles[1];
    near(road_curve_width(&curve,0),3.5f);
    near(road_curve_width(&curve,.499f),3.5f);
    near(road_curve_width(&curve,.5f),16);
    near(road_curve_width(&curve,1),16);
    near(road_curve_width(&curve,-1),3.5f);
    near(road_curve_width(&curve,2),16);
    MEM32(road + 0x84) = positions + 4;
    near(road_curve_width(&curve,.75f),6);
}

static void road_width_fixture(const RoadPoint *positions, unsigned count)
{
    unsigned i;
    road_graph_clear(); assert(road_graph_allocate(count));
    for (i = 0; i < count; ++i) s_roads.nodes[i].position = positions[i];
}

static void road_width_cache_coverage(void)
{
    unsigned char seen[16] = {0};
    unsigned i, j, edge, segments = 0;
    assert(s_roads.edge_count / 2 <= sizeof seen);
    assert(road_lines_build());
    for (i = 0; i < s_road_lines.count; ++i) {
        const RoadLine *line = s_road_lines.lines + i;
        assert(isfinite(line->width) && line->width > 0);
        for (j = 1; j < line->count; ++j) {
            RoadPoint a = s_road_lines.points[line->first + j - 1];
            RoadPoint b = s_road_lines.points[line->first + j];
            unsigned matches = 0;
            for (edge = 0; edge < s_roads.edge_count; edge += 2) {
                RoadPoint p = s_roads.nodes[s_roads.edges[edge ^ 1].to].position;
                RoadPoint q = s_roads.nodes[s_roads.edges[edge].to].position;
                if ((road_distance(a,p) < .001f && road_distance(b,q) < .001f) ||
                    (road_distance(a,q) < .001f && road_distance(b,p) < .001f)) {
                    assert(!seen[edge / 2]); seen[edge / 2] = 1; ++matches;
                    near(line->width, s_roads.edges[edge].width);
                    near(line->width, s_roads.edges[edge ^ 1].width);
                }
            }
            assert(matches == 1); ++segments;
        }
    }
    assert(segments == s_roads.edge_count / 2);
    for (edge = 0; edge < segments; ++edge) assert(seen[edge]);
}

static void road_width_draw(const RoadPoint *points, unsigned count,
                            float width, float factor)
{
    abi_start(0);
    route_polyline_width(0x25000, 0, points, count, (RoadPoint){0},
                         factor, 1, 0, 1000, 1000, 0, 0, .82f,
                         road_draw_half_width(width, factor));
    assert(g_esp == stack && g_fp_top == 0);
    assert(g_ebx == 0x11223344 && g_esi == 0x22334455 && g_edi == 0x33445566);
}

static void road_width_live_map_contract(void)
{
    const RoadPoint streets[4] = {{-20,0,-10}, {20,0,-10}, {-20,0,10}, {20,0,10}};
    const unsigned offsets[7] = {0x50,0x54,0x58,0x4C,0x94,0x98,0xB4};
    const float factors[2] = {.5f,.25f};
    const float expected[2][2] = {{1,3},{1,1.5f}};
    unsigned char previous_race[sizeof s_race];
    float previous_fields[9], previous_origin[3];
    int previous_origin_valid = cruise_origin_valid;
    unsigned i, pass;
    memcpy(previous_race,s_race,sizeof s_race);
    memcpy(previous_origin,cruise_origin,sizeof cruise_origin);
    for (i = 0; i < 7; ++i) previous_fields[i] = MEMF(node + offsets[i]);
    previous_fields[7] = MEMF(layout + 8); previous_fields[8] = MEMF(layout + 12);
    road_width_fixture(streets,4);
    assert(road_graph_edge_width(0,1,600,600,4));
    assert(road_graph_edge_width(2,3,700,700,12));
    memset(s_race,0,sizeof s_race); memset(cruise_origin,0,sizeof cruise_origin);
    cruise_origin_valid = 1;
    MEMF(node + 0x54) = 0; MEMF(node + 0x58) = 1024; MEMF(node + 0x4C) = 0;
    MEMF(node + 0x94) = 114; MEMF(node + 0x98) = 384; MEMF(node + 0xB4) = 1;
    MEMF(layout + 8) = MEMF(layout + 12) = 128;
    for (pass = 0; pass < 2; ++pass) {
        unsigned seen = 0;
        MEMF(node + 0x50) = factors[pass];
        abi_start(0); mm3_race_map_route(node,0x407F78,0x25000,0,layout);
        assert(draw_count == 2 && g_esp == stack && g_fp_top == 0);
        for (i = 0; i < draw_count; ++i) {
            unsigned road = centre(draws + i,1) < 384 ? 0 : 1;
            assert(!(seen & (1u << road))); seen |= 1u << road;
            near(road_width_draw_half(draws + i),expected[pass][road]);
            near(edge(draws + i),40*factors[pass]);
            near(draws[i].color[0],.62f);
        }
        assert(seen == 3 && !s_road_layer);
    }
    road_graph_clear();
    memcpy(s_race,previous_race,sizeof s_race);
    memcpy(cruise_origin,previous_origin,sizeof cruise_origin);
    cruise_origin_valid = previous_origin_valid;
    for (i = 0; i < 7; ++i) MEMF(node + offsets[i]) = previous_fields[i];
    MEMF(layout + 8) = previous_fields[7]; MEMF(layout + 12) = previous_fields[8];
}

static void road_width_contract(void)
{
    const RoadPoint chain[4] = {{0,0,0}, {10,0,0}, {20,0,0}, {30,0,0}};
    const RoadPoint loop[4] = {{0,0,0}, {30,0,0}, {30,0,30}, {0,0,30}};
    const RoadPoint straight[2] = {{-20,0,0}, {20,0,0}};
    const float invalid[4] = {0, -4, NAN, INFINITY};
    RoadPoint route[8];
    static RoadPoint dense[758];
    unsigned i, narrow = 0, wide = 0;
    int saved_improved = improved, saved_layer = s_road_layer;

    check = "road width native lane profiles and section changes";
    road_width_native_profile_contract();

    check = "road width graph identity and degree-two transition";
    road_width_fixture(chain, 4);
    assert(road_graph_edge_width(0,1,101,102,3));
    assert(road_graph_edge_width(1,2,201,201,12));
    assert(road_graph_edge_width(2,3,201,201,12));
    assert(s_roads.edges[0].road == 101 && s_roads.edges[1].road == 102);
    near(s_roads.edges[0].width,3); near(s_roads.edges[1].width,3);
    road_width_cache_coverage();
    assert(s_road_lines.count == 2);
    for (i = 0; i < s_road_lines.count; ++i) {
        const RoadLine *line = s_road_lines.lines + i;
        if (line->width == 3) {
            assert(line->count == 2); near(line->min_x,0); near(line->max_x,10); ++narrow;
        } else {
            near(line->width,12); assert(line->count == 3);
            near(line->min_x,10); near(line->max_x,30); ++wide;
        }
    }
    assert(narrow == 1 && wide == 1);
    assert(road_path(chain[0],chain[3],route,8) == 4);

    check = "road width mixed-width closed-loop coverage";
    road_width_fixture(loop,4);
    for (i = 0; i < 4; ++i)
        assert(road_graph_edge_width(i,(i+1)%4,300,300,i < 2 ? 4.0f : 10.0f));
    road_width_cache_coverage();
    assert(s_road_lines.count >= 2);

    check = "road width invalid metadata and legacy defaults";
    for (i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        road_width_fixture(straight,2);
        assert(road_graph_edge_width(0,1,400,400,invalid[i]));
        near(s_roads.edges[0].width,6); near(s_roads.edges[1].width,6);
    }
    road_width_fixture(chain,4);
    assert(road_graph_edge_roads(0,1,401,402)); assert(road_graph_edge(1,2));
    for (i = 0; i < s_roads.edge_count; ++i) near(s_roads.edges[i].width,6);
    road_graph_clear();
    assert(!s_roads.nodes && !s_road_lines.points && !s_road_lines.lines);

    check = "road width physical size, zoom, readability floor and captured quads";
    improved = 1; s_road_layer = 1;
    near(road_draw_half_width(4,.1f),1);
    near(road_draw_half_width(12,.25f),1.5f);
    near(road_draw_half_width(12,.5f),3);
    road_width_draw(straight,2,4,.5f);
    assert(draw_count == 1); near(road_width_draw_half(draws),1);
    near(edge(draws),20); near(draws[0].color[0],.62f);
    road_width_draw(straight,2,12,.5f);
    assert(draw_count == 1); near(road_width_draw_half(draws),3); near(edge(draws),20);
    road_width_draw(straight,2,12,.25f);
    assert(draw_count == 1); near(road_width_draw_half(draws),1.5f); near(edge(draws),10);
    road_width_draw(straight,2,4,.1f);
    assert(draw_count == 1); near(road_width_draw_half(draws),1);

    check = "road width real Cruise gray-layer wiring at two zoom levels";
    road_width_live_map_contract();
    s_road_layer = 1;

    check = "road width dense cached chain preserves one simplified thick draw";
    road_graph_clear(); assert(road_graph_allocate(758));
    for (i = 0; i < 758; ++i) {
        dense[i] = (RoadPoint){-200 + 400.0f*i/757,0,0};
        s_roads.nodes[i].position = dense[i];
        if (i) assert(road_graph_edge_width(i-1,i,500,500,12));
    }
    assert(road_lines_build() && s_road_lines.count == 1);
    near(s_road_lines.lines[0].width,12);
    road_width_draw(s_road_lines.points,s_road_lines.lines[0].count,12,.5f);
    assert(draw_count == 1); near(road_width_draw_half(draws),3);
    road_graph_clear();

    check = "road width does not leak into blue checkpoint guidance";
    s_road_layer = 0;
    abi_start(0);
    route_polyline(0x25000,0,straight,2,(RoadPoint){0},1,1,0,1000,1000,0,0,.98f);
    assert(draw_count == 1); near(road_width_draw_half(draws),1.25f);
    near(draws[0].color[0],.12f); near(draws[0].color[3],.98f);
    assert(g_esp == stack && g_fp_top == 0);
    improved = saved_improved; s_road_layer = saved_layer;
    puts("PASS road widths: physical-size graph metadata, width-transition/cycle cache coverage, real Cruise zoom/floor, dense RDP submission, fallback/teardown and unchanged guidance thickness");
}
