# PC-sized lamp glow list (src/mm3_lamp_glow.c). Native 22C86A creates the
# flare manager with room for 256 glows a frame; that one constant sizes both
# the 64-byte glow records (1A8467) and the three vertex buffers they are drawn
# from (248518: 8 vertices a glow), so it is replaced where it is passed in.
function(mm3_add_glow_capacity source_variable)
    set(content "${${source_variable}}")
    string(REGEX MATCHALL "void sub_0022C86A[(]void[)]" owners "${content}")
    list(LENGTH owners owner_count)
    string(FIND "${content}" "void sub_0022C86A(void)" owner_start)
    if(NOT owner_count EQUAL 1 OR owner_start LESS 0)
        message(FATAL_ERROR "MM3 glow capacity: missing or duplicate sub_0022C86A")
    endif()
    string(SUBSTRING "${content}" ${owner_start} -1 tail)
    string(FIND "${tail}" "\n}" owner_end)
    if(owner_end LESS 0)
        message(FATAL_ERROR "MM3 glow capacity: incomplete sub_0022C86A")
    endif()
    math(EXPR owner_length "${owner_end} + 2")
    string(SUBSTRING "${tail}" 0 ${owner_length} body)
    set(block [=[loc_0022C891: ;
    PUSH32(esp, 3);
    PUSH32(esp, eax);
    eax = 0x100;
    PUSH32(esp, 0x0022C89Eu); RECOMP_ABI_CALL(0x00248518u, sub_00248518); /* call 0x00248518 */
]=])
    set(fixed [=[loc_0022C891: ;
    PUSH32(esp, 3);
    PUSH32(esp, eax);
    eax = mm3_glow_capacity(); /* PC-sized glow list */
    PUSH32(esp, 0x0022C89Eu); RECOMP_ABI_CALL(0x00248518u, sub_00248518); /* call 0x00248518 */
]=])
    if(content MATCHES "\r\n")
        string(REPLACE "\n" "\r\n" block "${block}")
        string(REPLACE "\n" "\r\n" fixed "${fixed}")
    endif()
    string(FIND "${body}" "${block}" anchor)
    string(FIND "${body}" "mm3_glow_capacity" already)
    if(anchor LESS 0 OR NOT already LESS 0)
        message(FATAL_ERROR "MM3 glow capacity: sub_0022C86A anchor changed")
    endif()
    string(REPLACE "${block}" "${fixed}" transformed "${body}")
    string(REPLACE "${body}" "${transformed}" content "${content}")
    string(FIND "${content}" "extern uint32_t mm3_glow_capacity(void);" declared)
    if(declared LESS 0)
        if(content MATCHES "\r\n")
            set(content "#include <stdint.h>\r\nextern uint32_t mm3_glow_capacity(void);\r\n${content}")
        else()
            set(content "#include <stdint.h>\nextern uint32_t mm3_glow_capacity(void);\n${content}")
        endif()
    endif()
    set(${source_variable} "${content}" PARENT_SCOPE)
endfunction()
