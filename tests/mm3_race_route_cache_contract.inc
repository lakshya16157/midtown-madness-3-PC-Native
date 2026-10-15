/* Work-count gates are deterministic; retail-asset timings are reported
 * separately because scheduler noise and compiler optimization affect them. */
static void route_cache_course(unsigned count, const RoadPoint *positions, RoadPoint origin_point)
{
    unsigned i;
    memset(&s_race[0], 0, sizeof s_race[0]);
    memset(&s_paths[0], 0, sizeof s_paths[0]);
    s_race[0].count = count; s_race[0].current_object = 1000;
    s_race[0].origin = origin_point;
    for (i = 0; i < count; ++i) {
        s_race[0].checkpoints[i].object = 1000 + i;
        s_race[0].checkpoints[i].position = positions[i];
    }
}

static void route_cache_advance(void)
{
    --s_race[0].count;
    memmove(s_race[0].checkpoints, s_race[0].checkpoints + 1,
            s_race[0].count * sizeof(RaceCheckpoint));
    s_race[0].current_object = s_race[0].checkpoints[0].object;
}

static void route_cache_contract(void)
{
    const RoadPoint east = {1, 0, 0}, west = {-1, 0, 0};
    const RoadPoint street[] = {{-100,0,0}, {0,0,0}, {100,0,0}, {200,0,0}, {300,0,0}};
    unsigned i;
    check = "course cache unchanged and pending-prefix removal";
    routing_fixture(street, 5);
    for (i = 0; i < 4; ++i) routing_street(i, i + 1, 100);
    route_cache_course(4, street + 1, (RoadPoint){-50,0,0});
    race_paths_update(0, east);
    assert(s_paths[0].course_rebuilt == 3 && !s_paths[0].course_reused);
    race_paths_update(0, east);
    assert(!s_paths[0].course_rebuilt && !s_paths[0].course_reused);
    for (i = 3; i > 0; --i) {
        route_cache_advance(); race_paths_update(0, east);
        assert(!s_paths[0].course_rebuilt && s_paths[0].course_reused == i - 1);
        assert(s_paths[0].active_count >= 2);
        assert(s_paths[0].course_count == 2 * (i - 1));
    }
    {
        const RoadPoint blocks[] = {{0,0,0}, {-100,0,0}, {100,0,0}, {-100,0,100},
                                    {100,0,100}, {0,0,100}, {0,0,150}, {0,0,200}};
        const RoadPoint gates[] = {{0,0,0}, {0,0,100}, {0,0,150}, {0,0,180}};
        check = "course cache directional cascade and convergence";
        routing_fixture(blocks, 8);
        routing_street(0,1,100); routing_street(0,2,100);
        routing_street(1,3,200); routing_street(2,4,300);
        routing_street(3,5,400); routing_street(5,4,400);
        routing_street(5,6,500); routing_street(6,7,500);
        route_cache_course(4, gates, (RoadPoint){-20,0,0});
        race_paths_update(0, east);
        assert(s_paths[0].course_rebuilt == 3 && !s_paths[0].course_reused);
        assert(s_paths[0].incoming[1].x < -.99f);
        s_race[0].origin.x = 20; race_paths_update(0, west);
        assert(s_paths[0].course_rebuilt == 2 && s_paths[0].course_reused == 1);
        assert(s_paths[0].incoming[1].x > .99f);
        near(s_paths[0].course[1].x, -100);
        check = "course cache moved gate and graph generation";
        s_race[0].checkpoints[2].position.z = 160;
        race_paths_update(0, west);
        assert(s_paths[0].course_rebuilt == 2 && s_paths[0].course_reused == 1);
        near(s_paths[0].course[s_paths[0].starts[1] + s_paths[0].lengths[1] - 1].z, 160);
        ++s_roads.generation; race_paths_update(0, west);
        assert(s_paths[0].course_rebuilt == 3 && !s_paths[0].course_reused);
    }
    {
        RoadPoint *long_street = (RoadPoint *)calloc(8501, sizeof(RoadPoint));
        const RoadPoint gates[] = {{0,0,0}, {8500,0,0}, {0,0,0}};
        check = "course cache retries capacity-limited failures";
        assert(long_street);
        for (i = 0; i < 8501; ++i) long_street[i].x = (float)i;
        routing_fixture(long_street, 8501); free(long_street);
        for (i = 0; i < 8500; ++i) routing_street(i, i + 1, 100);
        route_cache_course(3, gates, gates[0]);
        race_paths_update(0, east);
        assert(s_paths[0].lengths[0] > 8000 && !s_paths[0].lengths[1]);
        route_cache_advance(); race_paths_update(0, east);
        assert(s_paths[0].course_rebuilt == 1 && !s_paths[0].course_reused);
        assert(s_paths[0].lengths[0] > 8000);
    }
    road_graph_clear(); mm3_race_guidance_frame_begin();
    puts("PASS course cache: unchanged updates, 4->3->2->1 suffix reuse, arrival-direction cascade/convergence, moved gates, graph invalidation and retry after capacity grows");
}
