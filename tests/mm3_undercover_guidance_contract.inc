/* Run the literal native HUD publisher with objective/zone collections.
 * Only native property/name lookup and draw submission remain boundaries. */
static void native_undercover_guidance_contract(void)
{
    unsigned collection, i, target, large;
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
    MEM32(event_vtable + 0xE8) = 0x105;
    for (collection = 0; collection < 2; ++collection) {
        MEM32(event_vtable + 0xEC) = collection ? 0x105 : 0x104;
        MEM32(event_vtable + 0xF0) = collection ? 0x104 : 0x105;
        for (target = 0; target < 4; ++target) {
            unsigned from = (target + 1) % 4;
            current_target = objects[target];
            MEMF(player_thing + 0x34) = points[from][0];
            MEMF(player_thing + 0x3C) = points[from][1];
            native_marker_count = 0;
            abi_start(0); sub_00120091();
            assert(g_esp == stack + 8 && g_fp_top == 0);
            assert(g_ebx == 0x11223344 && g_esi == 0x22334455 && g_edi == 0x33445566);
            assert(native_marker_count == 4);
            assert(s_race[0].valid && s_race[0].objective && s_race[0].count == 1);
            assert(s_race[0].current == 0 && s_race[0].current_object == objects[target]);
            assert(s_race[0].checkpoints[0].object == objects[target]);
            near(s_race[0].checkpoints[0].position.x, points[target][0]);
            near(s_race[0].checkpoints[0].position.z, points[target][1]);
            for (large = 0; large < 2; ++large) {
                MEMF(layout + 8) = MEMF(layout + 12) = large ? 512.0f : 128.0f;
                MEMF(node + 0x4C) = large ? 1.5707963268f : 0;
                abi_start(0); sub_0011CAE8(); abi_end();
                assert(route_segment_draw_count() > 0);
                assert(s_paths[0].course_count == 0 && s_paths[0].active_count >= 2);
                near(s_paths[0].active[s_paths[0].active_count - 1].x, points[target][0]);
                near(s_paths[0].active[s_paths[0].active_count - 1].z, points[target][1]);
                for (i = 0; i < draw_count; ++i) near(draws[i].color[3], .98f);
            }
        }
        /* Pickup hides the old stop while its cinematic runs. A hidden,
         * missing, invalid, or unselected target never leaves a stale route. */
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
    puts("PASS Undercover guidance: both native objective lists, selected pickup/drop-off changes, getter positions, visibility/name/invalid-target filtering, both map sizes, race-course preservation and guest ABI");
}
