# Observe the native loader's actual resident grid before its distance clamp.
# Keep every guest instruction and fail closed if regeneration changes it.
function(mm3_add_city_streaming_limit source_variable)
    set(content "${${source_variable}}")
    set(block [=[loc_00224C24: ;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(0) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp eax, 0 (32-bit) */
    if (CMP_EQ(_fa, _fb)) goto loc_00224C52; /* je: equal / zero */

loc_00224C28: ;
    fp_push(MEMF(eax + 0x1505C)); /* fld float */
    fp_top() = RECOMP_FP_PC(fp_top() * MEMF(0x3860E0)); /* fmul dword ptr [0x3860e0] */]=])
    string(FIND "${content}" "\r\n" crlf_position)
    set(newline "\n")
    if(NOT crlf_position LESS 0)
        set(newline "\r\n")
        string(REPLACE "\n" "\r\n" block "${block}")
    endif()
    string(REGEX MATCHALL "loc_00224C24:" sites "${content}")
    list(LENGTH sites site_count)
    string(FIND "${content}" "${block}" block_start)
    string(REGEX MATCHALL "void sub_00224A07[(]void[)]" owners "${content}")
    list(LENGTH owners owner_count)
    if(NOT owner_count EQUAL 1 OR NOT site_count EQUAL 1 OR block_start LESS 0)
        message(FATAL_ERROR "MM3 city streaming limit: native loader lookup/cap changed or duplicated")
    endif()
    string(REPLACE "loc_00224C24: ;" "loc_00224C24: ;${newline}    mm3_city_streaming_limit(eax);"
        replacement "${block}")
    string(REPLACE "${block}" "${replacement}" content "${content}")
    set(content "extern void mm3_city_streaming_limit(unsigned int renderer);${newline}${content}")
    set(${source_variable} "${content}" PARENT_SCOPE)
endfunction()
