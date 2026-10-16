# Retail cars retain both original HUD layouts unless explicitly overridden.
# Added cars keep their hidden numeric-only readout; HUD style selects the
# native dial family and needle arc. Explicit added-car maxima render custom
# dials with matching needles, retaining native artwork for a unit whose maximum
# matches its native printed maximum. Original HUD overrides remain independent.
function(mm3_add_speedometer_readout source_variable)
    set(content "${${source_variable}}")
    set(original_prologue [=[void sub_0023F09B(void)
{
    uint32_t ebp = 0;]=])
    set(replacement_prologue [=[void sub_0023F09B(void)
{
    extern int mm3_speedometer_draw(uint32_t, uint32_t, double);
    extern int mm3_speedometer_hide_readout(void);
    uint32_t mm3_speedometer_node = 0;
    uint32_t ebp = 0;]=])
    set(original_base [=[loc_0023F148: ;
    MEM32(ebp + -48) = 0x43200000;
    MEM32(ebp + -44) = 0x43100000;]=])
    set(replacement_base [=[loc_0023F148: ;
    /* Every retail analog atlas ends the dial by column 146. The native
     * 160-wide base clip also exposes columns 158-159 of adjacent warning
     * icons as black slivers. Keep a transparent gutter before those icons;
     * their separate draws, the needle and numeric layout stay unchanged. */
    MEM32(ebp + -48) = 0x431C0000; /* 156, crop only the base sprite. */
    MEM32(ebp + -44) = 0x43100000;]=])
    set(original_selector [=[loc_0023F82D: ;
    _fa = (uint32_t)(MEM32(ebp + -344)) & 0xFFFFFFFFu; _fb = (uint32_t)(1) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp MEM32(ebp + -344), 1 (32-bit) */
    if (CMP_NE(_fa, _fb)) goto loc_0023F8F8; /* jne: not equal / not zero */]=])
    set(replacement_selector [=[loc_0023F82D: ;
    _fa = (uint32_t)(MEM32(ebp + -344)) & 0xFFFFFFFFu; _fb = (uint32_t)(1) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp MEM32(ebp + -344), 1 (32-bit) */
    if (CMP_NE(_fa, _fb)) goto loc_0023F8F8; /* jne: not equal / not zero */
    /* Keep the retail numeric-only gauge for original cars; added cars
     * retain their existing hidden readout. The units getter stays native. */
    if (mm3_speedometer_hide_readout()) goto loc_0023FB95;]=])
    set(original_node [=[loc_0023F8F8: ;
    fp_push(MEMF(0x38641C)); /* fld float */]=])
    set(replacement_node [=[loc_0023F8F8: ;
    mm3_speedometer_node = ebx;
    fp_push(MEMF(0x38641C)); /* fld float */]=])
    set(original_needle [=[loc_0023F91B: ;
    g_fp_cmp = RECOMP_FCMP(fp_top(), MEMF(0x384EF4));]=])
    set(replacement_needle [=[loc_0023F91B: ;
    if (mm3_speedometer_draw(mm3_speedometer_node, ebp, fp_top())) {
        /* The native getter left one speed value on the x87 stack. */
        fp_pop();
        goto loc_0023FB95;
    }
    g_fp_cmp = RECOMP_FCMP(fp_top(), MEMF(0x384EF4));]=])
    string(FIND "${content}" "\r\n" crlf_position)
    if(NOT crlf_position LESS 0)
        foreach(block prologue base selector node needle)
            string(REPLACE "\n" "\r\n" original_${block} "${original_${block}}")
            string(REPLACE "\n" "\r\n" replacement_${block} "${replacement_${block}}")
        endforeach()
    endif()
    string(FIND "${content}" "void sub_0023F09B(void)" owner_position)
    string(REGEX MATCHALL "loc_0023F82D:" labels "${content}")
    list(LENGTH labels label_count)
    if(owner_position LESS 0 OR NOT label_count EQUAL 1)
        message(FATAL_ERROR "MM3 speedometer readout: original 0x0023F09B HUD draw is missing or duplicated")
    endif()
    foreach(block prologue base selector node needle)
        string(FIND "${content}" "${original_${block}}" block_position)
        if(block_position LESS owner_position)
            message(FATAL_ERROR "MM3 speedometer readout: original ${block} block changed")
        endif()
        string(LENGTH "${original_${block}}" block_length)
        math(EXPR tail_position "${block_position} + ${block_length}")
        string(SUBSTRING "${content}" ${tail_position} -1 block_tail)
        string(FIND "${block_tail}" "${original_${block}}" duplicate_position)
        if(NOT duplicate_position LESS 0)
            message(FATAL_ERROR "MM3 speedometer readout: original ${block} block duplicated")
        endif()
        string(REPLACE "${original_${block}}" "${replacement_${block}}" content "${content}")
    endforeach()
    set(${source_variable} "${content}" PARENT_SCOPE)
endfunction()
