/* Native park/plaza paths extend complete authored areas beyond the review
 * boxes. These paths belong to the gray display cache, never the route graph. */
static int map_areas_review_contains(RoadPoint point)
{
    float column = 8 * (point.x + 3429) / 5348;
    float row = 8 * (1386 - point.z) / 4729;
    return (column >= 0 && column <= 8 && row >= 4 && row <= 5) ||
        (column >= 2.5f && column <= 3 && row >= 5 && row <= 5.5f) ||
        (column >= 3 && column <= 5 && row >= 5 && row <= 5.5f) ||
        (column >= 1 && column <= 3 && row >= 3.5f && row <= 4);
}

static void map_areas_empty(void)
{
    assert(!s_map_areas.points && !s_map_areas.lines);
    assert(!s_map_areas.count && !s_map_areas.point_count);
}

static void retail_map_areas_contract(uint32_t city)
{
    static const unsigned expected_areas[] = {
        59,60,61,62,64,65,66,69,71,72,118,133,135,136,139,140,141,142,
        143,153,154,155,156,157,161,166,167,168,169,170,172,190,191,
        192,193,194
    };
    RoadNode *nodes = s_roads.nodes;
    RoadEdge *edges = s_roads.edges;
    unsigned node_count = s_roads.node_count, edge_count = s_roads.edge_count;
    unsigned generation = s_roads.generation;
    RoadNode *node_copy = (RoadNode *)malloc(node_count * sizeof *nodes);
    RoadEdge *edge_copy = (RoadEdge *)malloc(edge_count * sizeof *edges);
    RoadPoint *point_copy = NULL;
    RoadLine *line_copy = NULL;
    unsigned i, line_index = 0, point_index = 0, outside = 0, endpoints_outside = 0;
    unsigned complete_paths_outside = 0, expanded_straights = 0;
    uint32_t identity[6];
    check = "complete native map areas preserve routing and unclipped geometry";
    assert(node_copy && edge_copy);
    memcpy(node_copy,nodes,node_count * sizeof *nodes);
    memcpy(edge_copy,edges,edge_count * sizeof *edges);
    if (MEM32(city + 0xAC) != 198) {
        assert(!road_map_areas_build(city));
        map_areas_empty();
        assert(s_roads.nodes == nodes && s_roads.edges == edges);
        assert(s_roads.node_count == node_count && s_roads.edge_count == edge_count);
        assert(s_roads.generation == generation);
        assert(!memcmp(node_copy,nodes,node_count * sizeof *nodes));
        assert(!memcmp(edge_copy,edges,edge_count * sizeof *edges));
        free(node_copy); free(edge_copy);
        puts("PASS retail map areas: Paris retains its original coverage and route graph");
        return;
    }
    assert(node_count == 14391 && edge_count == 29338);
    assert(sizeof s_map_area_indices == sizeof expected_areas);
    assert(!memcmp(s_map_area_indices,expected_areas,sizeof expected_areas));
    assert(road_map_areas_build(city));
    assert(s_map_areas.count == 980 && s_map_areas.point_count == 14837);
    point_copy = (RoadPoint *)malloc(s_map_areas.point_count * sizeof *point_copy);
    line_copy = (RoadLine *)malloc(s_map_areas.count * sizeof *line_copy);
    assert(point_copy && line_copy);
    memcpy(point_copy,s_map_areas.points,s_map_areas.point_count * sizeof *point_copy);
    memcpy(line_copy,s_map_areas.lines,s_map_areas.count * sizeof *line_copy);
    for (i = 0; i < sizeof expected_areas / sizeof expected_areas[0]; ++i) {
        uint32_t area = MEM32(city + 0x4C) + expected_areas[i] * 0xE8;
        uint32_t geometry;
        for (geometry = MEM32(area + 0x78); geometry < MEM32(area + 0x7C); geometry += 7) {
            RoadLine *line;
            float min_x = FLT_MAX, max_x = -FLT_MAX, min_z = FLT_MAX, max_z = -FLT_MAX;
            unsigned count, sample, path_inside = 0;
            if ((MEM8(geometry + 6) & 15) != 0) continue;
            count = MEM16(geometry);
            if (count == 2) { count = 6; ++expanded_straights; }
            assert(line_index < s_map_areas.count);
            line = s_map_areas.lines + line_index++;
            assert(line->first == point_index && line->count == count);
            near(line->width,3.5f);
            for (sample = 0; sample < count; ++sample) {
                RoadPoint point = s_map_areas.points[point_index++];
                int inside = map_areas_review_contains(point);
                abi_start(0); g_eax = position + 0x200; g_ecx = area + 0xD4; g_edx = geometry;
                MEMF(stack + 4) = MEMF(area + 0xE0); MEM32(stack + 8) = sample;
                sub_0013532D();
                assert(g_esp == stack + 12 && g_fp_top == 0);
                assert(fabsf(MEMF(position + 0x200) - point.x) < .005f);
                assert(fabsf(MEMF(position + 0x204) - point.y) < .005f);
                assert(fabsf(MEMF(position + 0x208) - point.z) < .005f);
                min_x = fminf(min_x,point.x); max_x = fmaxf(max_x,point.x);
                min_z = fminf(min_z,point.z); max_z = fmaxf(max_z,point.z);
                if (inside) ++path_inside;
                else {
                    ++outside;
                    if (!sample || sample + 1 == count) ++endpoints_outside;
                }
            }
            near(line->min_x,min_x); near(line->max_x,max_x);
            near(line->min_z,min_z); near(line->max_z,max_z);
            if (!path_inside) ++complete_paths_outside;
        }
    }
    assert(line_index == 980 && point_index == 14837 && point_index - line_index == 13857);
    assert(expanded_straights == 457);
    assert(outside && endpoints_outside && complete_paths_outside);
    /* Every identity word matters: a similarly sized city or a partial title
     * match must not acquire Washington-specific optional coverage. */
    for (i = 0; i < 6; ++i) identity[i] = MEM32(city + 0x24 + i * 4);
    for (i = 0; i < 6; ++i) {
        MEM32(city + 0x24 + i * 4) ^= 1;
        assert(!road_map_areas_build(city)); map_areas_empty();
        MEM32(city + 0x24 + i * 4) = identity[i];
    }
    MEM32(city + 0xAC) = 197;
    assert(!road_map_areas_build(city)); map_areas_empty();
    MEM32(city + 0xAC) = 199;
    assert(!road_map_areas_build(city)); map_areas_empty();
    MEM32(city + 0xAC) = 198;
    assert(road_map_areas_build(city));
    assert(s_map_areas.count == 980 && s_map_areas.point_count == 14837);
    assert(!memcmp(point_copy,s_map_areas.points,14837 * sizeof *point_copy));
    assert(!memcmp(line_copy,s_map_areas.lines,980 * sizeof *line_copy));
    road_map_areas_clear(); map_areas_empty();
    road_map_areas_clear(); map_areas_empty();
    assert(road_map_areas_build(city));
    assert(s_roads.nodes == nodes && s_roads.edges == edges);
    assert(s_roads.node_count == node_count && s_roads.edge_count == edge_count);
    assert(s_roads.generation == generation);
    assert(!memcmp(node_copy,nodes,node_count * sizeof *nodes));
    assert(!memcmp(edge_copy,edges,edge_count * sizeof *edges));
    printf("PASS retail Washington map areas: 36 complete areas, 980 paths/14837 native samples/13857 segments; %u samples and %u endpoints outside review boxes, %u whole outside paths; native oracle, identity/count rejection, rebuild/clear, unchanged route graph\n",
           outside,endpoints_outside,complete_paths_outside);
    free(node_copy); free(edge_copy); free(point_copy); free(line_copy);
}

static void retail_map_areas_capacity_contract(uint32_t city)
{
    const unsigned offsets[] = {0x50,0x54,0x58,0x4C,0x94,0x98,0xAC,0xB4,0xBC};
    const float factors[] = {1 / 2.2f,1 / (2.2f * 1.6f)};
    static unsigned char previous_paths[sizeof s_paths];
    unsigned char previous_race[sizeof s_race], previous_markers[48];
    float previous_fields[11], previous_origin[3], previous_position[3], previous_uv[2];
    unsigned previous_marker_count = marker_count, previous_player = active_player;
    uint32_t previous_table = MEM32(0x407F78 + 4);
    int previous_improved = improved;
    RoadPoint center;
    unsigned nearest = NO_NODE, second, pass, i;
    float best = FLT_MAX;
    if (MEM32(city + 0xAC) != 198) return;
    check = "dense expanded Washington map areas retain native HUD capacity";
    center = road_control(MEM32(city + 0x4C) + 167 * 0xE8 + 0x34,0);
    for (i = 0; i < s_roads.node_count; ++i) {
        float distance = road_distance(center,s_roads.nodes[i].position);
        if (s_roads.nodes[i].routable && s_roads.nodes[i].first != NO_NODE && distance < best) {
            nearest = i; best = distance;
        }
    }
    assert(nearest != NO_NODE && best < 200);
    second = s_roads.edges[s_roads.nodes[nearest].first].to;
    assert(s_roads.nodes[second].routable && road_distance(center,s_roads.nodes[second].position) < 250);
    memcpy(previous_race,s_race,sizeof s_race); memcpy(previous_paths,s_paths,sizeof s_paths);
    memcpy(previous_markers,(void *)XBOX_PTR(markers),sizeof previous_markers);
    for (i = 0; i < 9; ++i) previous_fields[i] = MEMF(node + offsets[i]);
    previous_fields[9] = MEMF(layout + 8); previous_fields[10] = MEMF(layout + 12);
    for (i = 0; i < 3; ++i) {
        previous_origin[i] = MEMF(origin + i * 4);
        previous_position[i] = MEMF(position + i * 4);
    }
    previous_uv[0] = MEMF(4 + 0x50); previous_uv[1] = MEMF(4 + 0x54);
    MEMF(origin) = center.x; MEMF(origin + 4) = center.y; MEMF(origin + 8) = center.z;
    mm3_race_guidance_begin(0);
    for (i = 0; i < 2; ++i) {
        RoadPoint point = s_roads.nodes[i ? second : nearest].position;
        uint32_t marker = markers + i * 24;
        MEMF(position) = point.x; MEMF(position + 4) = point.y; MEMF(position + 8) = point.z;
        mm3_race_guidance_checkpoint(0,0x33000 + i * 256,position,origin,0x33000);
        MEMF(marker) = point.x - center.x; MEMF(marker + 8) = point.z - center.z;
        MEM32(marker + 12) = i ? 0 : 0x80;
    }
    mm3_race_guidance_end(0);
    MEM32(0x407F78 + 4) = markers; marker_count = 2; active_player = 0; improved = 1;
    MEMF(node + 0x54) = 0; MEMF(node + 0x58) = 1024;
    MEMF(node + 0x4C) = MEMF(node + 0x94) = MEMF(node + 0x98) = MEMF(node + 0xAC) = 0;
    MEMF(node + 0xB4) = MEMF(node + 0xBC) = 3;
    MEMF(layout + 8) = MEMF(layout + 12) = 384;
    MEMF(4 + 0x50) = MEMF(4 + 0x54) = 1;
    for (pass = 0; pass < sizeof factors / sizeof factors[0]; ++pass) {
        unsigned used, baseline_gray = 0;
        MEMF(node + 0x50) = factors[pass];
        road_map_areas_clear();
        abi_start(0); mm3_race_map_route(node,0x407F78,0x25000,0,layout);
        for (i = 0; i < draw_count; ++i) if (draws[i].color[0] == .62f) ++baseline_gray;
        assert(road_map_areas_build(city));
        native_submission_begin(); MEM32(0x3A0EC0) = 5000; MEM8(0x3C5694) = 1;
        native_pool_allocations = native_pool_frees = 0;
        abi_start(0); PUSH32(g_esp,0x0011E7E0); sub_0011DCBF();
        assert(g_esp == stack && g_fp_top == 0);
        assert(native_pool_allocations == 1 && native_pool_frees == 1);
        assert(MEM32(0x3A0EC0) == 160000 && !MEM8(0x3C5694));
        MEM32(packed_vertices + 160000 * 16) = 0xC01DFACE;
        native_submission_dropped = native_submission_gray = native_submission_blue = native_submission_markers = 0;
        native_submission = 1;
        abi_start(0); MEM32(stack + 12) = 0; sub_0011CAE8(); abi_end();
        native_submission = 0; used = MEM32(0x3C568C);
        assert(!native_submission_dropped && native_submission_gray > baseline_gray);
        assert(native_submission_blue && native_submission_markers == 2);
        assert(submit_test_quad(0,0,0) == 4 && MEM32(0x3C568C) == used + 4);
        assert(MEM32(packed_vertices + 160000 * 16) == 0xC01DFACE);
        assert(used + 18 < 160000);
        printf("PASS dense Washington map areas: factor=%.6f gray_quads=%u (streets-only=%u), blue=%u checkpoints=%u used=%u/160000; later HUD and guard retained\n",
               factors[pass],native_submission_gray,baseline_gray,native_submission_blue,native_submission_markers,used);
        improved = 0;
        abi_start(0); mm3_race_map_route(node,0x407F78,0x25000,0,layout);
        for (i = 0; i < draw_count; ++i) assert(draws[i].color[0] != .62f);
        improved = 1;
    }
    memcpy(s_race,previous_race,sizeof s_race); memcpy(s_paths,previous_paths,sizeof s_paths);
    memcpy((void *)XBOX_PTR(markers),previous_markers,sizeof previous_markers);
    for (i = 0; i < 9; ++i) MEMF(node + offsets[i]) = previous_fields[i];
    MEMF(layout + 8) = previous_fields[9]; MEMF(layout + 12) = previous_fields[10];
    for (i = 0; i < 3; ++i) {
        MEMF(origin + i * 4) = previous_origin[i]; MEMF(position + i * 4) = previous_position[i];
    }
    MEMF(4 + 0x50) = previous_uv[0]; MEMF(4 + 0x54) = previous_uv[1];
    MEM32(0x407F78 + 4) = previous_table;
    marker_count = previous_marker_count; active_player = previous_player; improved = previous_improved;
}
