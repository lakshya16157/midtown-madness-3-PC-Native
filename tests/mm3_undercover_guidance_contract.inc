/* Run the literal native HUD publisher with objective/zone collections.
 * Only native property/name lookup and draw submission remain boundaries. */
static void undercover_expect(unsigned count, const unsigned *expected, unsigned order)
{
    unsigned i;
    assert(s_race[0].valid && s_race[0].objective && s_race[0].count == count);
    assert(s_race[0].order == order);
    assert(s_race[0].current == 0 && s_race[0].current_object == objects[expected[0]]);
    for (i = 0; i < count; ++i) {
        assert(s_race[0].checkpoints[i].object == objects[expected[i]]);
        near(s_race[0].checkpoints[i].position.x, points[expected[i]][0]);
        near(s_race[0].checkpoints[i].position.z, points[expected[i]][1]);
    }
}

/* Draw both map sizes: the bright leg runs from the car to the selected stop
 * and the dimmer course continues through every later stop of the mission. */
static void undercover_draw_expect(unsigned count, const unsigned *expected)
{
    unsigned large, i, bright, dim;
    for (large = 0; large < 2; ++large) {
        MEMF(layout + 8) = MEMF(layout + 12) = large ? 512.0f : 128.0f;
        MEMF(node + 0x4C) = large ? 1.5707963268f : 0;
        abi_start(0); sub_0011CAE8(); abi_end();
        assert(route_segment_draw_count() > 0);
        assert(s_paths[0].count == count && s_paths[0].active_count >= 2);
        near(s_paths[0].active[s_paths[0].active_count - 1].x, points[expected[0]][0]);
        near(s_paths[0].active[s_paths[0].active_count - 1].z, points[expected[0]][1]);
        if (count == 1) assert(s_paths[0].course_count == 0);
        else {
            assert(s_paths[0].course_count >= 2 * (count - 1));
            for (i = 0; i + 1 < count; ++i) assert(s_paths[0].lengths[i] >= 2);
        }
        bright = dim = 0;
        for (i = 0; i < draw_count; ++i) {
            if (draws[i].color[3] == .98f) ++bright;
            else { near(draws[i].color[3], .8f); ++dim; }
        }
        assert(bright > 0 && (count == 1 ? !dim : dim > 0));
    }
}

static void native_undercover_guidance_contract(void)
{
    unsigned collection, i, target;
    fixture_roads();
    hud_players = 1; active_player = 0; marker_count = 0;
    MEM32(0x3C569C) = players;
    MEM32(event_head) = list_nodes;
    for (i = 0; i < 4; ++i) {
        MEM32(list_nodes + i * 16) = i == 3 ? event_head : list_nodes + (i + 1) * 16;
        MEM8(objects[i] + 0x70) = 1;
        /* Undercover must copy the getter's result, not assume +48 like a
         * static checkpoint. A saved guest pointer is never used by drawing. */
        MEMF(objects[i] + 0x48) = NAN;
        MEMF(objects[i] + 0x80) = points[i][0];
        MEMF(objects[i] + 0x84) = 0;
        MEMF(objects[i] + 0x88) = points[i][1];
    }
    MEM32(event_vtable + 0x200 + 0x24) = 0x109;
    MEM32(event_vtable + 0x200 + 0x7C) = 0x10A;
    /* A second stop class: same native methods, different identity. */
    memcpy((void *)XBOX_PTR(event_vtable + 0x300), (void *)XBOX_PTR(event_vtable + 0x200), 0x100);
    MEM32(event_vtable + 0xE8) = 0x105;
    for (collection = 0; collection < 2; ++collection) {
        MEM32(event_vtable + 0xEC) = collection ? 0x105 : 0x104;
        MEM32(event_vtable + 0xF0) = collection ? 0x104 : 0x105;
        for (target = 0; target < 4; ++target) {
            unsigned from = (target + 1) % 4, count, order, expected[4];
            current_target = objects[target];
            MEMF(player_thing + 0x34) = points[from][0];
            MEMF(player_thing + 0x3C) = points[from][1];
            native_marker_count = 0;
            abi_start(0); sub_00120091();
            assert(g_esp == stack + 8 && g_fp_top == 0);
            assert(g_ebx == 0x11223344 && g_esi == 0x22334455 && g_edi == 0x33445566);
            assert(native_marker_count == 4);
            /* Every zone is visible and the mission's arrow always selects the
             * lowest unvisited one, so a target at either end of the native
             * collection shows its order; one in the middle shows nothing. */
            if (target == 0) { count = 4; order = ORDER_FORWARD; for (i = 0; i < 4; ++i) expected[i] = i; }
            else if (target == 3) { count = 4; order = ORDER_REVERSE; for (i = 0; i < 4; ++i) expected[i] = 3 - i; }
            else { count = 1; order = ORDER_SINGLE; expected[0] = target; }
            undercover_expect(count, expected, order);
            undercover_draw_expect(count, expected);
        }
        /* Hidden, invalid or differently classed stops never join the course. */
        current_target = objects[0];
        MEMF(player_thing + 0x34) = points[1][0]; MEMF(player_thing + 0x3C) = points[1][1];
        {
            static const unsigned without1[] = {0, 2, 3}, without2[] = {0, 1, 3};
            MEM8(objects[1] + 0x70) = 0;
            abi_start(0); sub_00120091();
            undercover_expect(3, without1, ORDER_FORWARD); undercover_draw_expect(3, without1);
            MEM8(objects[1] + 0x70) = 1;
            MEMF(objects[2] + 0x80) = NAN;
            abi_start(0); sub_00120091();
            undercover_expect(3, without2, ORDER_FORWARD); undercover_draw_expect(3, without2);
            MEMF(objects[2] + 0x80) = points[2][0];
            MEM32(objects[2]) = event_vtable + 0x300;
            abi_start(0); sub_00120091();
            undercover_expect(3, without2, ORDER_FORWARD); undercover_draw_expect(3, without2);
            MEM32(objects[2]) = event_vtable + 0x200;
        }
        /* A finished stop disappears and the course starts at the next one. */
        {
            static const unsigned after[] = {1, 2, 3};
            MEM8(objects[0] + 0x70) = 0; current_target = objects[1];
            MEMF(player_thing + 0x34) = points[0][0]; MEMF(player_thing + 0x3C) = points[0][1];
            abi_start(0); sub_00120091();
            undercover_expect(3, after, ORDER_FORWARD); undercover_draw_expect(3, after);
            MEM8(objects[0] + 0x70) = 1;
        }
        /* Pickup hides the old stop while its cinematic runs. A hidden, missing,
         * invalid, or unselected target never leaves a stale route, even with
         * other stops still showing. */
        current_target = objects[3];
        MEM8(objects[3] + 0x70) = 0;
        abi_start(0); sub_00120091(); assert(!s_race[0].valid);
        MEM8(objects[3] + 0x70) = 1;
        current_target = 0;
        abi_start(0); sub_00120091(); assert(!s_race[0].valid);
        current_target = objects[3]; MEMF(objects[3] + 0x80) = NAN;
        abi_start(0); sub_00120091(); assert(!s_race[0].valid);
        MEMF(objects[3] + 0x80) = points[3][0];
    }
    /* Preserve the native third collection's name/visibility filter. */
    auxiliary_name_matches = 1;
    abi_start(0); sub_00120091(); assert(!s_race[0].valid);
    auxiliary_name_matches = 0;
    abi_start(0); sub_00120091(); assert(s_race[0].valid);
    /* The same stop listed by both collections is one stop, not two. */
    MEM32(event_vtable + 0xEC) = MEM32(event_vtable + 0xF0) = 0x104;
    current_target = objects[0];
    abi_start(0); sub_00120091();
    assert(s_race[0].objective_count == 4 && s_race[0].count == 4 && s_race[0].order == ORDER_FORWARD);
    /* Stops of different collections are separate courses: objectives 0/1 in
     * +EC and 2/3 in +F0 never join into one order. */
    MEM32(list_nodes + 16) = event_head;
    MEM32(second_list + 4) = second_head; MEM32(second_head) = second_nodes;
    MEM32(second_nodes) = second_nodes + 16; MEM32(second_nodes + 8) = objects[2] - 0x18;
    MEM32(second_nodes + 16) = second_head; MEM32(second_nodes + 16 + 8) = objects[3] - 0x18;
    MEM32(event_vtable + 0xEC) = 0x104; MEM32(event_vtable + 0xF0) = 0x10B;
    {
        static const unsigned first[] = {0, 1}, reversed[] = {1, 0}, second[] = {2, 3};
        /* Park the car away from every selected stop so each has a bright leg. */
        MEMF(player_thing + 0x34) = (points[0][0] + points[3][0]) * .5f;
        MEMF(player_thing + 0x3C) = (points[0][1] + points[3][1]) * .5f + 4.0f;
        current_target = objects[0];
        abi_start(0); sub_00120091();
        undercover_expect(2, first, ORDER_FORWARD); undercover_draw_expect(2, first);
        current_target = objects[1];
        abi_start(0); sub_00120091();
        undercover_expect(2, reversed, ORDER_REVERSE); undercover_draw_expect(2, reversed);
        current_target = objects[2];
        abi_start(0); sub_00120091();
        undercover_expect(2, second, ORDER_FORWARD); undercover_draw_expect(2, second);
    }
    MEM32(list_nodes + 16) = list_nodes + 32;
    current_target = objects[3];
    /* Objectives already present in an ordered race must not duplicate or
     * replace its course, even when listed in another marker collection. */
    MEM32(event_vtable + 0xE8) = 0x104;
    MEM32(event_vtable + 0xEC) = 0x104; MEM32(event_vtable + 0xF0) = 0x105;
    for (i = 0; i < 4; ++i) MEMF(objects[i] + 0x48) = points[i][0];
    abi_start(0); sub_00120091();
    assert(s_race[0].valid && !s_race[0].objective && s_race[0].count == 4 && s_race[0].current == 3);
    MEM32(event_vtable + 0x200 + 0x24) = 0x107;
    MEM32(event_vtable + 0xEC) = MEM32(event_vtable + 0xF0) = 0x105;
    hud_players = 0; MEM32(0x3C569C) = 0;
    mm3_race_guidance_frame_begin();
    puts("PASS Undercover guidance: both native objective lists, selected pickup/drop-off changes, getter positions, visibility/name/invalid-target filtering, mission-order courses through every visible zone (forward, reversed, single), per-collection/class grouping, both map sizes, race-course preservation and guest ABI");
}
