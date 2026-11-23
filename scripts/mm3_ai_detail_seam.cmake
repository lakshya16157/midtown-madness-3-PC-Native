# "Opponents use full-detail cars" (MM3_AI_FULL_DETAIL, mm3_ai_full_detail in
# src/mm3_car_order.c). The car init sub_00220761 checks the car's type at
# +0x488 (loc_0022084A); for a computer-driven car (2 or 3) it inserts "_lod1"
# into the model name at loc_0022085A, before it loads the collision file
# (data.ccol) and the model (sub_002202EE) from that folder. With the mod on,
# jump to loc_00220885, where the player's car (type 0 or 1) goes: the full
# model with its own collision, a pair the game already loads for every car.
function(mm3_add_ai_full_detail source_variable)
    set(content "${${source_variable}}")
    set(owner sub_00220761)
    string(REGEX MATCHALL "void ${owner}[(]void[)]" owners "${content}")
    list(LENGTH owners owner_count)
    if(NOT owner_count EQUAL 1)
        message(FATAL_ERROR "MM3 AI detail: missing or duplicate ${owner}")
    endif()
    string(FIND "${content}" "void ${owner}(void)" owner_start)
    string(SUBSTRING "${content}" ${owner_start} -1 owner_tail)
    string(FIND "${owner_tail}" "\nvoid " owner_end)
    if(owner_end LESS 0)
        set(body "${owner_tail}")
    else()
        string(SUBSTRING "${owner_tail}" 0 ${owner_end} body)
    endif()
    string(FIND "${body}" "mm3_ai_full_detail" already)
    if(NOT already LESS 0)
        message(FATAL_ERROR "MM3 AI detail: ${owner} already patched")
    endif()
    # The type check, the rename's first call (find "." in the name) and the
    # label after the rename, each once: the jump only stands when all three
    # are as read. (FIND, not lists: the C text is full of semicolons.)
    set(type_read "\n    eax = MEM32(ebx + 0x488);")
    set(rename "\nloc_0022085A: ;\n    PUSH32(esp, 0xFFFFFFFFu);\n    PUSH32(esp, 0x3B59AC);")
    set(after "\nloc_00220885: ;\n    PUSH32(esp, 0x3B59C0);")
    string(FIND "${body}" "\r\n" crlf)
    foreach(anchor type_read rename after)
        if(NOT crlf LESS 0)
            string(REPLACE "\n" "\r\n" ${anchor} "${${anchor}}")
        endif()
        string(FIND "${body}" "${${anchor}}" first)
        string(FIND "${body}" "${${anchor}}" last REVERSE)
        if(first LESS 0 OR NOT first EQUAL last)
            message(FATAL_ERROR "MM3 AI detail: ${owner} model rename changed")
        endif()
    endforeach()
    string(REPLACE "\nloc_0022085A: ;" "\nloc_0022085A: ; if (mm3_ai_full_detail()) goto loc_00220885;"
        patched "${body}")
    string(REPLACE "${body}" "${patched}" content "${content}")
    set(${source_variable} "extern int mm3_ai_full_detail(void);\n${content}" PARENT_SCOPE)
endfunction()
