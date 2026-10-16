/* Actual retail course and native pending-list regression coverage. */
static void retail_big_one_contract(void)
{
    unsigned i;
    RoadPoint route[ACTIVE_POINTS];
    RoadPoint incoming = {0, 0, 0}, prior_end = {0, 0, 0};
    unsigned gate, pixel_samples = 0;
    int previous_improved = improved;
    improved = 1;
    check = "retail Big One every gate leg and rendered blue centreline";
    for (gate = 0; gate + 1 < sizeof retail_big_one_gates / sizeof *retail_big_one_gates; ++gate) {
        unsigned point, route_count = road_path_direction(retail_big_one_gates[gate], retail_big_one_gates[gate + 1],
                                                          incoming, route, ACTIVE_POINTS);
        assert(route_count >= 2);
        assert(road_distance(route[0], retail_big_one_gates[gate]) < 2);
        assert(road_distance(route[route_count-1], retail_big_one_gates[gate+1]) < 2);
        if (gate) assert(road_distance(prior_end, route[0]) < .01f);
        incoming = race_path_tangent(route, route_count); prior_end = route[route_count-1];
        abi_start(0);
        route_polyline(0x25000,0,route,route_count,retail_big_one_gates[gate],.5f,1,0,4096,4096,0,0,.8f);
        assert(route_segment_draw_count() > 0);
        for (point = 1; point < route_count; ++point) {
            unsigned sample;
            for (sample = 1; sample < 4; ++sample) {
                float t = (float)sample / 4;
                float x = route[point-1].x + (route[point].x-route[point-1].x)*t;
                float z = route[point-1].z + (route[point].z-route[point-1].z)*t;
                assert(road_join_covers((retail_big_one_gates[gate].x-x)*.5f,
                                       (z-retail_big_one_gates[gate].z)*.5f));
                ++pixel_samples;
            }
        }
        assert(g_esp == stack && g_fp_top == 0);
        if (gate == 28) {
            unsigned turn, zoom;
            for (turn = 0; turn < 4; ++turn) for (zoom = 0; zoom < 2; ++zoom)
            for (point = 1; point < route_count; ++point) {
                float angle = (float)turn * .3926990817f;
                RoadPoint camera = {(route[point-1].x + route[point].x)*.5f,0,
                                    (route[point-1].z + route[point].z)*.5f};
                abi_start(0);
                route_polyline(0x25000,0,route,route_count,camera,zoom ? 1.10f : .5f,
                               cosf(angle),sinf(angle),56,40,0,0,.8f);
                assert(road_join_covers(0,0));
                assert(g_esp == stack && g_fp_top == 0);
            }
        }
    }
    for (gate = 0; gate < 31; ++gate) {
        memset(&s_race[0], 0, sizeof s_race[0]);
        for (i = gate; i < 31; ++i) {
            s_race[0].checkpoints[i-gate].object = 1000+i;
            s_race[0].checkpoints[i-gate].position = retail_big_one_gates[i];
        }
        s_race[0].valid = 1; s_race[0].count = 31-gate;
        s_race[0].current = 0; s_race[0].current_object = 1000+gate;
        s_race[0].origin = retail_big_one_gates[gate ? gate-1 : 0];
        race_paths_update(0, (RoadPoint){0,0,-1});
        for (i = 0; i+1 < s_paths[0].count; ++i)
            assert(s_paths[0].lengths[i] >= 2);
    }
    improved = previous_improved;
    printf("PASS retail Big One: all 30 legs including CP-28->CP-29; %u blue centreline pixel samples, CP-28->29 close/rotated clipping, continuous endpoints and complete 31->1 pending-list lifecycle\n", pixel_samples);
}

static unsigned retail_big_one_publish(uint32_t pending, unsigned target)
{
    unsigned gate, count = 0;
    uint32_t previous = event_head;
    MEM32(0x407C1C) = 3; MEM32(0x3C569C) = players;
    MEM32(players + 4) = players + 0x40; MEM32(players + 0x40) = player_thing;
    MEM32(0x3C5CDC) = players + 0x80; MEM32(players + 0x80 + 0x54) = player_thing;
    MEM8(player_thing + 8) = 1;
    MEM32(event) = event_vtable;
    MEM32(event_vtable + 0x28) = 0x101; MEM32(event_vtable + 0x30) = 0x102;
    MEM32(event_vtable + 0x34) = MEM32(event_vtable + 0x9C) = 0x103;
    MEM32(event_vtable + 0xE8) = 0x104;
    MEM32(event_vtable + 0xEC) = MEM32(event_vtable + 0xF0) = 0x105;
    MEM32(event_list + 4) = event_head;
    MEM32(empty_list + 4) = empty_head; MEM32(empty_head) = empty_head;
    MEM32(0x407D20 + 0x168) = 0;
    MEM32(event_vtable + 0x200 + 0x48) = 0x106;
    MEM32(event_vtable + 0x200 + 0x24) = 0x107;
    MEM32(event_head) = event_head;
    for (gate = 0; gate < 31; ++gate) {
        uint32_t object = 0x30018 + gate * 256, link = list_nodes + gate * 16;
        MEM32(object) = event_vtable + 0x200;
        MEMF(object + 0x48) = retail_big_one_gates[gate].x;
        MEMF(object + 0x4C) = retail_big_one_gates[gate].y;
        MEMF(object + 0x50) = retail_big_one_gates[gate].z;
        /* Visibility is independent of membership: pending but hidden gates
         * must retain guidance; visited gates are absent even if visible. */
        MEM8(object + 0x70) = (uint8_t)(gate & 1);
        if (!(pending & (1u << gate))) continue;
        MEM32(previous) = link; MEM32(link) = event_head;
        MEM32(link + 8) = object - 0x18; previous = link; ++count;
    }
    current_target = 0x30018 + target * 256;
    MEMF(player_thing + 0x34) = retail_big_one_gates[27].x;
    MEMF(player_thing + 0x38) = retail_big_one_gates[27].y;
    MEMF(player_thing + 0x3C) = retail_big_one_gates[27].z;
    hud_players = 1; active_player = 0; native_marker_count = 0;
    abi_start(0); sub_00120091();
    assert(g_esp == stack + 8 && g_fp_top == 0 && s_race[0].count == count);
    for (gate = 0, count = 0; gate < 31; ++gate) if (pending & (1u << gate)) {
        assert(s_race[0].checkpoints[count].object == 0x30018 + gate * 256);
        assert(road_distance(s_race[0].checkpoints[count].position, retail_big_one_gates[gate]) < .01f);
        if (gate == target) assert(s_race[0].current == count);
        ++count;
    }
    return count;
}

static void retail_big_one_free_order_contract(void)
{
    const uint32_t cases[] = {0x7FFFFFFFu,0x55555555u,0x2AAAAAAAu,
        0x70000000u,0x30000001u,0x70002010u,0x37FFFFFDu,0x7F0FF0FFu};
    uint32_t random = 0x31415926u;
    unsigned scenario, count, gate, turn, retained = 0;
    int previous_improved = improved;
    improved = 1;
    check = "retail Big One native free-order pending publication and route capacity";
    for (scenario = 0; scenario < 32; ++scenario) {
        uint32_t pending;
        unsigned target = 0;
        random = random * 1664525u + 1013904223u;
        pending = scenario < sizeof cases / sizeof *cases ? cases[scenario] : random & 0x7FFFFFFFu;
        while (!(pending & (1u << target))) ++target;
        for (turn = 0; turn < 2; ++turn) {
            if (turn) { target = 30; while (!(pending & (1u << target))) --target; }
            count = retail_big_one_publish(pending,target);
            race_paths_update(0,(RoadPoint){turn ? 1.0f : -1.0f,0,0});
            assert(s_paths[0].count == count && s_paths[0].course_count < COURSE_POINTS);
            for (gate = 0; gate + 1 < count; ++gate) {
                assert(s_paths[0].lengths[gate] >= 2);
                ++retained;
            }
        }
    }
    /* Pack every actual gate and its complete blue course over dense gray DC
     * artwork through the real native 160,000-vertex pool. */
    count = retail_big_one_publish(0x7FFFFFFFu,0); assert(count == 31);
    s_race[0].origin = (RoadPoint){-700,0,-800};
    marker_count = count; MEM32(0x407F78 + 4) = markers;
    for (gate = 0; gate < count; ++gate) {
        MEMF(markers + gate*24) = retail_big_one_gates[gate].x - s_race[0].origin.x;
        MEMF(markers + gate*24 + 8) = retail_big_one_gates[gate].z - s_race[0].origin.z;
        MEM32(markers + gate*24 + 12) = 0;
    }
    MEMF(node + 0x50) = .06f; MEMF(node + 0x54) = 0; MEMF(node + 0x58) = 1024;
    MEMF(node + 0x4C) = MEMF(node + 0x94) = MEMF(node + 0x98) = MEMF(node + 0xAC) = 0;
    MEMF(node + 0xB4) = MEMF(node + 0xBC) = 3;
    MEMF(layout + 8) = MEMF(layout + 12) = 384;
    MEMF(4 + 0x50) = MEMF(4 + 0x54) = 1;
    native_submission_begin(); MEM32(0x3A0EC0) = 160000;
    MEM32(packed_vertices + 160000*16) = 0xC01DFACE;
    native_submission_dropped = native_submission_gray = native_submission_blue = native_submission_markers = 0;
    native_submission = 1;
    abi_start(0); MEM32(stack + 12) = 0; sub_0011CAE8(); abi_end();
    native_submission = 0;
    assert(!native_submission_dropped && native_submission_gray > 1250 && native_submission_blue);
    assert(native_submission_markers == 31 && MEM32(0x3C568C) < 160000);
    assert(MEM32(packed_vertices + 160000*16) == 0xC01DFACE);
    printf("PASS retail Big One free order: 64 native pending/target scenarios, %u routed legs; all31 markers with gray=%u blue=%u used=%u/160000 and no drops\n",
           retained,native_submission_gray,native_submission_blue,MEM32(0x3C568C));
    marker_count = hud_players = 0; MEM32(0x3C569C) = 0;
    improved = previous_improved;
}

/* The native current target becomes live when the countdown ends. Its car
 * reroute can use another street; the pending checkpoint course must remain
 * complete underneath that brighter guidance. */
static void retail_big_one_live_draw_contract(void)
{
    unsigned direction, phase, point, sample, covered = 0;
    int was_improved = improved, was_heading = player_heading_valid;
    float old_heading = player_heading;
    improved = player_heading_valid = 1;
    check = "Big One pending CP28->29 remains blue when native target becomes active";
    for (direction = 0; direction < 2; ++direction) {
        memset(&s_paths[0], 0, sizeof s_paths[0]);
        for (phase = 0; phase < 2; ++phase) {
            unsigned start, length;
            retail_big_one_publish(0x7FFFFFFFu, phase ? 29 : 31);
            s_race[0].origin = (RoadPoint){-2110.43f,13.85f,-58.74f};
            player_heading = direction ? 1.554479f : -1.587114f;
            MEMF(node + 0x50) = .5f; MEMF(node + 0x54) = 0; MEMF(node + 0x58) = 1024;
            MEMF(node + 0x4C) = MEMF(node + 0x94) = MEMF(node + 0x98) = 0;
            MEMF(node + 0xB4) = 1; MEMF(layout + 8) = MEMF(layout + 12) = 4096;
            abi_start(0); mm3_race_map_route(node,0x407F78,1,0,layout);
            assert(g_esp == stack && g_fp_top == 0);
            start = s_paths[0].starts[28]; length = s_paths[0].lengths[28];
            assert(length >= 2 && (!phase || s_paths[0].active_count >= 2));
            for (point = 1; point < length; ++point) for (sample = 1; sample < 4; ++sample) {
                unsigned draw, matches = 0;
                float t = (float)sample/4;
                RoadPoint a = s_paths[0].course[start+point-1], b = s_paths[0].course[start+point];
                float x = (s_race[0].origin.x - (a.x+(b.x-a.x)*t))*.5f;
                float y = ((a.z+(b.z-a.z)*t) - s_race[0].origin.z)*.5f;
                for (draw = 0; draw < draw_count; ++draw)
                    if (fabsf(draws[draw].color[0]-.12f) < .001f &&
                        distance_to_draw(x,y,draws+draw) <= 1.0f) ++matches;
                if (!matches) fprintf(stderr,"missing live CP28->29 blue direction=%u phase=%u point=%u/%u x=%g y=%g\n",direction,phase,point,length,x,y);
                assert(matches); ++covered;
            }
        }
    }
    player_heading = old_heading; player_heading_valid = was_heading; improved = was_improved;
    printf("PASS Big One live draw: countdown/current-target transition and both approaches retain CP28->29; %u blue samples\n",covered);
}
