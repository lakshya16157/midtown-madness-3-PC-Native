# Retail 13AB27 saves EBX/ESI/EDI before its single outbound argument at
# 13AB35. The lifted first-call guard accidentally includes saved EDI in the
# argument block. On failed dispatch it must retain all three saved registers.
# Move only the host recovery marker; guest instructions and logging stay intact.
function(mm3_fix_ai_query_stack source_variable)
    set(content "${${source_variable}}")
    string(REGEX MATCHALL "void sub_0013AB27[(]void[)]" owners "${content}")
    list(LENGTH owners owner_count)
    string(FIND "${content}" "void sub_0013AB27(void)" owner_start)
    if(NOT owner_count EQUAL 1 OR owner_start LESS 0)
        message(FATAL_ERROR "MM3 AI query stack: missing or duplicate sub_0013AB27")
    endif()
    string(SUBSTRING "${content}" ${owner_start} -1 tail)
    string(FIND "${tail}" "\n}" owner_end)
    if(owner_end LESS 0)
        message(FATAL_ERROR "MM3 AI query stack: incomplete sub_0013AB27")
    endif()
    math(EXPR owner_length "${owner_end} + 2")
    string(SUBSTRING "${tail}" 0 ${owner_length} body)
    set(block [=[loc_0013AB27: ;
    PUSH32(esp, ebx);
    PUSH32(esp, esi);
    { uint32_t _icall_esp = g_esp;
    PUSH32(esp, edi);
    edi = MEM32(esp + 0x10);
    esi = ecx;
    ecx = MEM32(edi + 0x30);
    eax = MEM32(ecx);
    PUSH32(esp, edi);
    { uint32_t _icall_target = MEM32(eax + 4); PUSH32(esp, 0x0013AB39u); RECOMP_ICALL_SAFE_AT(_icall_target, _icall_esp, 0x0013AB36u); } /* indirect call */
    }]=])
    set(fixed [=[loc_0013AB27: ;
    PUSH32(esp, ebx);
    PUSH32(esp, esi);
    PUSH32(esp, edi);
    edi = MEM32(esp + 0x10);
    esi = ecx;
    ecx = MEM32(edi + 0x30);
    eax = MEM32(ecx);
    { uint32_t _icall_esp = g_esp;
    PUSH32(esp, edi);
    { uint32_t _icall_target = MEM32(eax + 4); PUSH32(esp, 0x0013AB39u); RECOMP_ICALL_SAFE_AT(_icall_target, _icall_esp, 0x0013AB36u); } /* indirect call */
    }]=])
    set(epilogue [=[loc_0013AB8E: ;
    POP32(esp, edi);
    POP32(esp, esi);
    POP32(esp, ebx);
    esp += 8; return; /* ret 4 */]=])
    if(content MATCHES "\r\n")
        string(REPLACE "\n" "\r\n" block "${block}")
        string(REPLACE "\n" "\r\n" fixed "${fixed}")
        string(REPLACE "\n" "\r\n" epilogue "${epilogue}")
    endif()
    string(REGEX MATCHALL "0x0013AB36u" sites "${body}")
    list(LENGTH sites site_count)
    string(FIND "${body}" "${block}" block_start)
    string(FIND "${body}" "${epilogue}" epilogue_start)
    if(NOT site_count EQUAL 1 OR block_start LESS 0 OR epilogue_start LESS 0)
        message(FATAL_ERROR "MM3 AI query stack: original prologue, argument, call or epilogue changed")
    endif()
    string(REPLACE "${block}" "${fixed}" transformed "${body}")
    string(REPLACE "${body}" "${transformed}" content "${content}")
    set(${source_variable} "${content}" PARENT_SCOPE)
endfunction()
