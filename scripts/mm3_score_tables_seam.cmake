# Best-times file with more than the Xbox's 80 race tables (src/mm3_score_tables.c).
# Writer sub_001E8A7A pads the file to 80 tables with `80 - tables` empty ones;
# above 80 (circuit races) that count wrapped and it wrote until memory ran out.
# Loader sub_001E9060 rejects any other size and reads exactly 80 tables.
function(mm3_score_tables_replace body_variable owner original replacement)
    set(body "${${body_variable}}")
    string(FIND "${body}" "\r\n" crlf)
    if(NOT crlf LESS 0)
        string(REPLACE "\n" "\r\n" original "${original}")
        string(REPLACE "\n" "\r\n" replacement "${replacement}")
    endif()
    string(FIND "${body}" "${original}" position)
    if(position LESS 0)
        message(FATAL_ERROR "MM3 score tables: ${owner} boundary changed or already patched")
    endif()
    string(LENGTH "${original}" length)
    math(EXPR after "${position} + ${length}")
    string(SUBSTRING "${body}" ${after} -1 tail)
    string(FIND "${tail}" "${original}" duplicate)
    if(NOT duplicate LESS 0)
        message(FATAL_ERROR "MM3 score tables: duplicate ${owner} boundary")
    endif()
    string(REPLACE "${original}" "${replacement}" body "${body}")
    set(${body_variable} "${body}" PARENT_SCOPE)
endfunction()

function(mm3_fix_score_tables source_variable owner)
    set(content "${${source_variable}}")
    string(REGEX MATCHALL "void ${owner}[(]void[)]" owners "${content}")
    list(LENGTH owners owner_count)
    if(NOT owner_count EQUAL 1)
        message(FATAL_ERROR "MM3 score tables: missing or duplicate ${owner}")
    endif()
    string(FIND "${content}" "void ${owner}(void)" owner_start)
    string(SUBSTRING "${content}" ${owner_start} -1 owner_tail)
    string(FIND "${owner_tail}" "\nvoid " owner_end)
    if(owner_end LESS 0)
        set(body "${owner_tail}")
    else()
        string(SUBSTRING "${owner_tail}" 0 ${owner_end} body)
    endif()
    set(original_body "${body}")
    if(owner STREQUAL "sub_001E8A7A")
        mm3_score_tables_replace(body ${owner} [=[loc_001E8B79: ;
    PUSH32(esp, 0x50);
    POP32(esp, eax);
    _fb = (uint32_t)(MEM32(edi + 0x2C)) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* sub source, before the write */
    eax = eax - MEM32(edi + 0x2C);
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* sub result */
    if ((_fa == 0)) goto loc_001E8BD1; /* je: equal / zero */]=] [=[loc_001E8B79: ;
    PUSH32(esp, 0x50);
    POP32(esp, eax);
    _fb = (uint32_t)(MEM32(edi + 0x2C)) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* sub source, before the write */
    eax = eax - MEM32(edi + 0x2C);
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* sub result */
    { extern int mm3_score_tables_unpadded(uint32_t tables); if ((_fa == 0) || mm3_score_tables_unpadded(MEM32(edi + 0x2C))) goto loc_001E8BD1; } /* Pad to 80 tables only from fewer. */]=])
    elseif(owner STREQUAL "sub_001E9060")
        mm3_score_tables_replace(body ${owner} [=[loc_001E91F7: ;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(esi) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp eax, esi (32-bit) */
    if (CMP_EQ(_fa, _fb)) goto loc_001E9204; /* je: equal / zero */]=] [=[loc_001E91F7: ;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(esi) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp eax, esi (32-bit) */
    { extern uint32_t mm3_score_file_tables(uint32_t data, uint32_t size, uint32_t expected, uint32_t scores); if (mm3_score_file_tables(MEM32(ebp + 0x14), eax, esi, MEM32(ebp + 0x7C))) goto loc_001E9204; } /* The Xbox's size, or more than 80 tables. */]=])
        mm3_score_tables_replace(body ${owner} [=[loc_001E9232: ;
    MEM32(ebp + 0x64) = 0x50;]=] [=[loc_001E9232: ;
    { extern uint32_t mm3_score_file_table_count(void); MEM32(ebp + 0x64) = mm3_score_file_table_count(); } /* The Xbox's 0x50, or the file's count. */]=])
    else()
        message(FATAL_ERROR "MM3 score tables: unknown owner ${owner}")
    endif()
    string(REPLACE "${original_body}" "${body}" content "${content}")
    set(${source_variable} "${content}" PARENT_SCOPE)
endfunction()
