# A pending same-key resubmit must remove its old queue entry before replacing
# the map record. Retail 24F964 instead skips a FOUND iterator and erases END.
# 24FA01 then queues the same map node twice; retrieval frees a still-queued
# node. Accepted/in-flight work remains at the queue front in 24F837, so its
# existing reset+pop path must run before freeing the node. An END iterator has
# no worker-owned queue record and needs only map erasure.
function(mm3_fix_route_cancellation source_variable)
    set(content "${${source_variable}}")
    string(REGEX MATCHALL "void sub_0024F964[(]void[)]" owners "${content}")
    list(LENGTH owners owner_count)
    string(FIND "${content}" "void sub_0024F964(void)" owner_start)
    if(NOT owner_count EQUAL 1 OR owner_start LESS 0)
        message(FATAL_ERROR "MM3 route cancellation: missing or duplicate sub_0024F964")
    endif()
    string(SUBSTRING "${content}" ${owner_start} -1 tail)
    string(FIND "${tail}" "\n}" owner_end)
    if(owner_end LESS 0)
        message(FATAL_ERROR "MM3 route cancellation: incomplete sub_0024F964")
    endif()
    math(EXPR owner_length "${owner_end} + 2")
    string(SUBSTRING "${tail}" 0 ${owner_length} body)
    set(block [=[loc_0024F9B7: ;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(MEM32(ebp + -16)) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp eax, MEM32(ebp + -16) (32-bit) */
    if (CMP_NE(_fa, _fb)) goto loc_0024F9F8; /* jne: not equal / not zero */

loc_0024F9BC: ;]=])
    set(fixed [=[loc_0024F9B7: ;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(MEM32(ebp + -16)) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp eax, MEM32(ebp + -16) (32-bit) */
    /* Native PC: remove a found pending entry; never erase the end iterator. */
    if (CMP_EQ(_fa, _fb)) goto loc_0024F9E9;

loc_0024F9BC: ;]=])
    if(content MATCHES "\r\n")
        string(REPLACE "\n" "\r\n" block "${block}")
        string(REPLACE "\n" "\r\n" fixed "${fixed}")
    endif()
    string(FIND "${body}" "${block}" anchor)
    string(REGEX MATCHALL "RECOMP_ABI_CALL[(]0x0005BCAFu" finds "${body}")
    list(LENGTH finds find_count)
    string(REGEX MATCHALL "RECOMP_ABI_CALL[(]0x0005B400u" erases "${body}")
    list(LENGTH erases erase_count)
    string(FIND "${body}" "RECOMP_ICALL_SAFE_AT(_icall_target, _icall_esp, 0x0024F9CDu)" reset)
    string(FIND "${body}" "esp += 12; return; /* ret 8 */" epilogue)
    if(anchor LESS 0 OR NOT find_count EQUAL 1 OR NOT erase_count EQUAL 1 OR reset LESS 0 OR epilogue LESS 0)
        message(FATAL_ERROR "MM3 route cancellation: original find/branch/reset/erase/return contract changed")
    endif()
    string(REPLACE "${block}" "${fixed}" transformed "${body}")
    string(REPLACE "${body}" "${transformed}" content "${content}")
    set(${source_variable} "${content}" PARENT_SCOPE)
endfunction()
