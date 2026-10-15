/* Directional route scenarios use explicit native street identities and
 * connectors, without relying on GPU submission or the current race state. */
static void routing_fixture(const RoadPoint *positions, unsigned count)
{
    unsigned i;
    road_graph_clear(); assert(road_graph_allocate(count));
    for (i = 0; i < count; ++i) s_roads.nodes[i].position = positions[i];
}

static void routing_street(unsigned a, unsigned b, uint32_t road)
{
    assert(road_graph_edge_roads(a, b, road, road));
}

static void race_direction_routing_contract(void)
{
    const RoadPoint east = {1, 0, 0};
    RoadPoint route[32];
    RoadSnap snap;
    unsigned count, i;
    const RoadPoint loop[] = {{-50,0,0}, {50,0,0}, {50,0,80}, {-50,0,80}, {-50,0,-40}};
    check = "directional route forward around block";
    routing_fixture(loop, 5);
    routing_street(0,1,100); routing_street(1,2,200); routing_street(2,3,300);
    routing_street(3,0,400); routing_street(0,4,500);
    /* The shortest path turns around immediately to a different street;
     * the racing route proceeds to the next junction and around the block. */
    count = road_path_direction((RoadPoint){0,0,0}, (RoadPoint){-50,0,-20}, east, route, 32);
    assert(count == 6); near(route[1].x, 50); near(route[2].z, 80);
    near(route[count-1].x, -50); near(route[count-1].z, -20);
    assert(road_path_direction((RoadPoint){0,0,0}, (RoadPoint){-50,0,-20}, east, route, 3) == 0);
    count = road_path_direction((RoadPoint){0,0,0}, (RoadPoint){-50,0,-20},
                                (RoadPoint){-1,0,0}, route, 32);
    assert(count == 3); near(route[1].x, -50);
    /* Unknown headings retain the existing connected shortest-path result. */
    count = road_path_direction((RoadPoint){0,0,0}, (RoadPoint){-50,0,-20},
                                (RoadPoint){0,0,0}, route, 32);
    assert(count == 3); near(route[1].x, -50);

    {
        const RoadPoint sampled[] = {{-100,0,0}, {0,0,0}, {100,0,0}, {200,0,0}};
        check = "directional route same native road behind";
        routing_fixture(sampled, 4);
        for (i = 0; i < 3; ++i) routing_street(i,i+1,1234);
        /* The next checkpoint is behind on a different sampled edge of the
         * same native road: a reversal is legitimate without a giant loop. */
        count = road_path_direction((RoadPoint){150,0,0}, (RoadPoint){-50,0,0}, east, route, 32);
        assert(count == 4); near(route[1].x, 100); near(route[count-1].x, -50);
        assert(road_path_direction((RoadPoint){150,0,0}, (RoadPoint){160,0,0}, east, route, 2) == 2);
        assert(road_path_direction((RoadPoint){150,0,0}, (RoadPoint){140,0,0}, east, route, 2) == 2);
    }
    {
        const RoadPoint dead_end[] = {{-100,0,0}, {100,0,0}, {-100,0,60}};
        check = "directional route dead end recovery";
        routing_fixture(dead_end, 3); routing_street(0,1,100); routing_street(0,2,200);
        /* No connected forward exit exists. Recovery turns around now,
         * rather than losing guidance or visiting the dead end first. */
        count = road_path_direction((RoadPoint){0,0,0}, (RoadPoint){-100,0,30}, east, route, 32);
        assert(count == 3); near(route[1].x, -100); near(route[count-1].z, 30);
    }
    {
        const RoadPoint joined[] = {{-100,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {-80,0,-10},
                                    {0,0,0}, {0,0,80}, {-80,0,80}};
        check = "directional route zero length junction";
        routing_fixture(joined, 8);
        routing_street(0,1,100); routing_street(1,2,100); routing_street(2,3,200);
        routing_street(3,4,200); routing_street(2,5,300); routing_street(5,6,300);
        routing_street(6,7,400); routing_street(7,4,500);
        /* A zero-length native junction must preserve the incoming street's
         * direction and reject the nearby hairpin onto a different road. */
        count = road_path_direction((RoadPoint){-20,0,0}, joined[4], east, route, 32);
        assert(count == 5); near(route[1].x, 0); near(route[2].z, 80);
        near(route[count-1].x, -80); near(route[count-1].z, -10);
    }
    {
        const RoadPoint arrivals[] = {{-100,0,0}, {0,0,0}, {0,0,100}, {100,0,100},
                                     {100,0,0}, {-100,0,-10}};
        check = "directional route distinct junction arrivals";
        routing_fixture(arrivals, 6);
        routing_street(0,1,100); routing_street(1,2,200); routing_street(2,3,300);
        routing_street(3,4,400); routing_street(4,1,500); routing_street(1,5,600);
        /* The same junction must be revisitable from a different direction.
         * A node-only cost would discard this legal approach after seeing
         * the cheaper arrival that requires a U-turn onto the final road. */
        count = road_path_direction((RoadPoint){-20,0,0}, arrivals[5], east, route, 32);
        assert(count == 7); near(route[1].x, 0); near(route[1].z, 0);
        /* Either orientation around the block is a legal forward approach;
         * taking the east exit also avoids the initial right-angle turn. */
        assert(route[2].x == 100 || route[2].z == 100);
        near(route[5].x, 0); near(route[5].z, 0); near(route[count-1].x, -100);
    }
    {
        const RoadPoint hairpin[] = {{0,0,0}, {50,0,0}, {10,0,10}};
        check = "directional route true spline hairpin";
        routing_fixture(hairpin, 3); routing_street(0,1,100); routing_street(1,2,100);
        /* A sharp geometric bend in one native spline remains traversable. */
        count = road_path_direction((RoadPoint){10,0,0}, hairpin[2], east, route, 32);
        assert(count == 3); near(route[1].x, 50);
    }
    {
        const RoadPoint crossing[] = {{-50,0,0}, {50,0,0}, {0,0,-50}, {0,0,50},
                                      {-50,12,0}, {50,12,0}};
        check = "directional route crossing and bridge isolation";
        routing_fixture(crossing, 6); routing_street(0,1,100);
        routing_street(2,3,200); routing_street(4,5,300);
        /* Crossing/parallel roads do not acquire a graph connection, and
         * height still separates bridges under directional snapping. */
        assert(road_snap_direction((RoadPoint){0,0,0}, east, &snap)); assert(snap.road == 100);
        assert(road_snap_direction((RoadPoint){0,0,0}, (RoadPoint){0,0,1}, &snap)); assert(snap.road == 200);
        assert(road_snap_direction((RoadPoint){0,12,0}, east, &snap)); near(snap.position.y, 12);
        assert(!road_path_direction((RoadPoint){-20,0,0}, (RoadPoint){0,0,40}, east, route, 32));
        assert(!road_path_direction((RoadPoint){-20,0,0}, (RoadPoint){20,12,0}, east, route, 32));
        assert(!road_path_direction((RoadPoint){-20,0,0}, (RoadPoint){1000,0,1000}, east, route, 32));
    }
    road_graph_clear();
}
