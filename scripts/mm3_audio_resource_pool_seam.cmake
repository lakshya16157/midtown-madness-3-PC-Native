# Retail 17BB4B searches the active audio-resource wrappers, then reads back()
# from the free deque without checking its count. back() on an empty deque can
# read the allocator header before its first block as an object pointer. Use
# the factory's existing null-result path, which both callers already handle.
# Keep active-resource reuse first and leave every nonempty operation intact.
function(mm3_guard_audio_resource_pool source_variable)
    set(content "${${source_variable}}")
    string(REGEX MATCHALL "void sub_0017BB4B[(]void[)]" owners "${content}")
    list(LENGTH owners owner_count)
    string(FIND "${content}" "void sub_0017BB4B(void)" owner_start)
    if(NOT owner_count EQUAL 1 OR owner_start LESS 0)
        message(FATAL_ERROR "MM3 audio resource pool: missing or duplicate sub_0017BB4B")
    endif()
    string(SUBSTRING "${content}" ${owner_start} -1 tail)
    string(FIND "${tail}" "\n}" owner_end)
    if(owner_end LESS 0)
        message(FATAL_ERROR "MM3 audio resource pool: incomplete sub_0017BB4B")
    endif()
    math(EXPR owner_length "${owner_end} + 2")
    string(SUBSTRING "${tail}" 0 ${owner_length} body)
    set(block [=[loc_0017BBB1: ;
    edi = ebx + 0xC;
    ecx = edi;
    g_ebp = ebp; /* frame stays current across calls */
    g_seh_ebp = ebp;
    PUSH32(esp, 0x0017BBBBu); RECOMP_ABI_CALL(0x00079626u, sub_00079626); /* call 0x00079626 */

loc_0017BBBB: ;
    esi = MEM32(eax);]=])
    set(fixed [=[loc_0017BBB1: ;
    /* Native PC: an exhausted free deque has no resource to return. */
    if (MEM32(ebx + 0x1C) == 0) {
        mm3_stress_audio_pool_empty(); /* opt-in host counter, no guest writes */
        goto loc_0017BC03;
    }
    edi = ebx + 0xC;
    ecx = edi;
    g_ebp = ebp; /* frame stays current across calls */
    g_seh_ebp = ebp;
    PUSH32(esp, 0x0017BBBBu); RECOMP_ABI_CALL(0x00079626u, sub_00079626); /* call 0x00079626 */

loc_0017BBBB: ;
    esi = MEM32(eax);]=])
    set(epilogue [=[loc_0017BC03: ;
    eax = 0; /* xor self */
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* xor result */

loc_0017BC05: ;
    POP32(esp, edi);
    POP32(esp, esi);
    POP32(esp, ebx);
    POP32(esp, ebp);
    esp += 20; return; /* ret 16 */]=])
    if(content MATCHES "\r\n")
        string(REPLACE "\n" "\r\n" block "${block}")
        string(REPLACE "\n" "\r\n" fixed "${fixed}")
        string(REPLACE "\n" "\r\n" epilogue "${epilogue}")
    endif()
    string(REGEX MATCHALL "RECOMP_ABI_CALL[(]0x00079626u" sites "${body}")
    list(LENGTH sites site_count)
    string(FIND "${body}" "${block}" block_start)
    string(FIND "${body}" "${epilogue}" epilogue_start)
    if(NOT site_count EQUAL 1 OR block_start LESS 0 OR epilogue_start LESS 0)
        message(FATAL_ERROR "MM3 audio resource pool: free deque acquisition or null-result epilogue changed")
    endif()
    string(REPLACE "${block}" "${fixed}" transformed "${body}")
    string(REPLACE "${body}" "${transformed}" content "${content}")
    set(content "#include \"mm3_stress_debug.h\"\n${content}")
    set(${source_variable} "${content}" PARENT_SCOPE)
endfunction()
