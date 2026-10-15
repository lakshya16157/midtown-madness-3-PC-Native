/* Exercise packed native car paths independently of retail asset counts. */
enum { TRAFFIC_AREA = 0x71000, TRAFFIC_RECORDS = 0x72000,
       TRAFFIC_CONTROLS = 0x73000, TRAFFIC_LINKS = 0x74000,
       TRAFFIC_ROADS = 0x75000 };

static void traffic_test_record(unsigned index, unsigned flags,
                                const uint16_t *points, unsigned count)
{
    uint32_t record = TRAFFIC_RECORDS + index * 7;
    uint32_t controls = TRAFFIC_CONTROLS + index * 0x100;
    MEM16(record) = (uint16_t)count;
    MEM32(record + 2) = controls;
    MEM8(record + 6) = (uint8_t)flags;
    memcpy((void *)XBOX_PTR(controls), points, count * 6);
}

static int traffic_test_edge(unsigned from, unsigned to)
{
    unsigned edge;
    for (edge = s_roads.nodes[from].first; edge != NO_NODE;
         edge = s_roads.edges[edge].next)
        if (s_roads.edges[edge].to == to) return 1;
    return 0;
}

static unsigned traffic_test_reachable(unsigned start)
{
    unsigned pending[32], count = 1, visited = 0;
    unsigned char seen[32] = {0};
    assert(s_roads.node_count <= 32);
    pending[0] = start; seen[start] = 1;
    while (count) {
        unsigned current = pending[--count], edge;
        ++visited;
        for (edge = s_roads.nodes[current].first; edge != NO_NODE;
             edge = s_roads.edges[edge].next) {
            unsigned next = s_roads.edges[edge].to;
            if (!seen[next]) { seen[next] = 1; pending[count++] = next; }
        }
    }
    return visited;
}

static void traffic_test_graph(const RoadPoint *positions, unsigned count)
{
    unsigned i;
    road_graph_clear(); assert(road_graph_allocate(count));
    for (i = 0; i < count; ++i) s_roads.nodes[i].position = positions[i];
}

static void roundabout_traffic_contract(void)
{
    const uint16_t controls[] = {0,0,0, 65535,32768,16384};
    RoadTrafficPath paths[8];
    unsigned count, total;
    RoadPoint point;
    memset((void *)XBOX_PTR(TRAFFIC_AREA), 0, 0x5000);
    MEMF(TRAFFIC_AREA + 0xD4) = -10;
    MEMF(TRAFFIC_AREA + 0xD8) = 5;
    MEMF(TRAFFIC_AREA + 0xDC) = 20;
    MEMF(TRAFFIC_AREA + 0xE0) = 4096;
    MEM32(TRAFFIC_AREA + 0x78) = TRAFFIC_RECORDS;
    MEM32(TRAFFIC_AREA + 0x7C) = TRAFFIC_RECORDS + 14;
    traffic_test_record(0, 0, controls, 2);
    traffic_test_record(1, 0xA1, controls, 2);
    total = 7;
    assert(road_traffic_paths(TRAFFIC_AREA, 1, paths, &count, &total));
    assert(count == 1 && total == 13 && paths[0].first == 7 && paths[0].count == 6);
    assert(paths[0].geometry == TRAFFIC_RECORDS + 7);
    point = road_traffic_point(TRAFFIC_AREA, paths[0].geometry, 0);
    assert(fabsf(point.x + 10) < .001f && fabsf(point.y - 5) < .001f && fabsf(point.z - 20) < .001f);
    point = road_traffic_point(TRAFFIC_AREA, paths[0].geometry, 2);
    assert(fabsf(point.x - 1628.4f) < .001f && fabsf(point.y - 824.2125f) < .001f && fabsf(point.z - 429.60625f) < .001f);
    point = road_traffic_point(TRAFFIC_AREA, paths[0].geometry, 5);
    assert(fabsf(point.x - 4086) < .001f && fabsf(point.y - 2053.03125f) < .001f && fabsf(point.z - 1044.015625f) < .001f);
    /* Invalid packed-vector strides and capacity limits reject the snapshot. */
    MEM32(TRAFFIC_AREA + 0x7C) = TRAFFIC_RECORDS + 13;
    total = 0; assert(!road_traffic_paths(TRAFFIC_AREA, 1, paths, &count, &total));
    MEM32(TRAFFIC_AREA + 0x7C) = TRAFFIC_RECORDS + 14;
    total = ROAD_NODES - 5; assert(!road_traffic_paths(TRAFFIC_AREA, 1, paths, &count, &total));
    MEM16(TRAFFIC_RECORDS + 7) = 1;
    total = 0; assert(!road_traffic_paths(TRAFFIC_AREA, 1, paths, &count, &total));
    MEM16(TRAFFIC_RECORDS + 7) = 2;

    {
        const RoadPoint positions[] = {
            {-20,0,-20}, {20,0,-20}, {20,0,20}, {-20,0,20}, {-20,0,-20},
            {20,0,-20}, {40,0,-20}, {60,0,-20},
            {0,0,-40}, {0,0,0}, {0,0,40}
        };
        RoadSnap center;
        unsigned i;
        paths[0] = (RoadTrafficPath){TRAFFIC_AREA, TRAFFIC_RECORDS, 5, 0};
        paths[1] = (RoadTrafficPath){TRAFFIC_AREA, TRAFFIC_RECORDS + 7, 3, 5};
        paths[2] = (RoadTrafficPath){TRAFFIC_AREA, TRAFFIC_RECORDS + 14, 3, 8};
        traffic_test_graph(positions, 11);
        for (i = 1; i < 5; ++i) assert(road_graph_edge(i - 1, i));
        assert(road_graph_edge(5,6) && road_graph_edge(6,7));
        assert(road_graph_edge(8,9) && road_graph_edge(9,10));
        assert(road_traffic_joins(paths, 3, NULL, 0, 0, 0, 0, 0, 0));
        /* A single closed path remains a cycle; exact entry endpoints join. */
        assert(traffic_test_edge(0,4) && traffic_test_edge(1,5));
        assert(traffic_test_reachable(0) == 8 && traffic_test_reachable(8) == 3);
        /* An unrelated lane crossing the ring creates no graph intersection. */
        for (i = 0; i < 8; ++i) {
            assert(!traffic_test_edge(i,8) && !traffic_test_edge(i,9) && !traffic_test_edge(i,10));
        }
        /* With the crossing excluded, snapping retains the ring's open island. */
        s_roads.node_count = 8;
        assert(road_snap((RoadPoint){0,0,0}, &center));
        assert(fabsf(fabsf(center.position.x) - 20) < .001f ||
               fabsf(fabsf(center.position.z) - 20) < .001f);
        s_roads.node_count = 11;
        assert(road_lines_build());
        road_graph_clear();
        assert(!s_roads.nodes && !s_roads.edges && !s_road_lines.points && !s_road_lines.lines);
    }

    {
        const RoadPoint positions[] = {
            {0,0,4}, {0,0,12}, {0,0,20},
            {-20,0,0}, {0,0,0}, {20,0,0},
            {-20,0,4}, {0,0,4}, {20,0,4}
        };
        RoadCurve curves[2] = {{0}};
        unsigned reverse;
        paths[0] = (RoadTrafficPath){TRAFFIC_AREA, TRAFFIC_RECORDS, 3, 0};
        curves[0].object = TRAFFIC_ROADS; curves[0].first = 3; curves[0].last = 5;
        curves[1].object = TRAFFIC_ROADS + 0xDC; curves[1].first = 6; curves[1].last = 8;
        for (reverse = 0; reverse < 2; ++reverse) {
            traffic_test_graph(positions, 9);
            assert(road_graph_edge(0,1) && road_graph_edge(1,2));
            assert(road_graph_edge(3,4) && road_graph_edge(4,5));
            assert(road_graph_edge(6,7) && road_graph_edge(7,8));
            MEM32(TRAFFIC_LINKS + (reverse ? 4 : 0)) = TRAFFIC_AREA;
            MEM32(TRAFFIC_LINKS + (reverse ? 0 : 4)) = TRAFFIC_ROADS;
            assert(road_traffic_joins(paths, 1, curves, 2, TRAFFIC_LINKS, 1,
                                      TRAFFIC_ROADS, 0, 0));
            /* Native links may attach at street interiors, in either pair order. */
            assert(traffic_test_edge(0,4) && traffic_test_reachable(0) == 6);
            /* Closer unlinked streets and an endpoint farther than 8m stay apart. */
            assert(!traffic_test_edge(0,7) && !traffic_test_edge(2,4));
            assert(traffic_test_reachable(6) == 3);
        }
    }
    road_graph_clear();
    puts("PASS roundabout traffic: packed car-path filtering/quantization, two-control sampling, invalid data/capacity, closed rings/shared entries, crossing isolation, native street-interior links, distance bound and teardown");
}
