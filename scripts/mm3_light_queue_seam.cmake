# PC-sized local-light queue (src/mm3_light_queue.c). The manager's static
# initializer allocates more bytes and attaches an extended record pointer
# array; push, point/spot submission and the consumer use that array and its
# capacity. Every expected pattern must occur exactly as counted, or the
# build stops rather than leaving a function on the 256-entry array.
function(mm3_light_queue_replace body_variable from to expected label)
    set(body "${${body_variable}}")
    string(FIND "${body}" "${from}" at)
    set(count 0)
    set(rest "${body}")
    while(NOT at LESS 0)
        math(EXPR count "${count} + 1")
        string(LENGTH "${from}" length)
        math(EXPR next "${at} + ${length}")
        string(SUBSTRING "${rest}" ${next} -1 rest)
        string(FIND "${rest}" "${from}" at)
    endwhile()
    if(NOT count EQUAL expected)
        message(FATAL_ERROR "MM3 light queue: ${label} occurs ${count} times, expected ${expected}")
    endif()
    string(REPLACE "${from}" "${to}" body "${body}")
    set(${body_variable} "${body}" PARENT_SCOPE)
endfunction()

function(mm3_add_light_queue_seam source_variable function_name)
    set(content "${${source_variable}}")
    string(REGEX MATCHALL "void ${function_name}[(]void[)]" owners "${content}")
    list(LENGTH owners owner_count)
    if(NOT owner_count EQUAL 1)
        message(FATAL_ERROR "MM3 light queue: missing/duplicate ${function_name}")
    endif()
    string(FIND "${content}" "void ${function_name}(void)" owner_start)
    string(SUBSTRING "${content}" ${owner_start} -1 owner_tail)
    string(FIND "${owner_tail}" "\nvoid " owner_end)
    if(owner_end LESS 0)
        set(body "${owner_tail}")
    else()
        string(SUBSTRING "${owner_tail}" 0 ${owner_end} body)
    endif()
    set(transformed "${body}")
    if(function_name STREQUAL "sub_00254FDD")
        mm3_light_queue_replace(transformed "PUSH32(esp, 0x5B20);"
            "PUSH32(esp, mm3_light_queue_alloc_size()); /* PC-sized light queue */" 1 "manager size")
        mm3_light_queue_replace(transformed "loc_00254FE7: ;\n"
            "loc_00254FE7: ;\n    mm3_light_queue_allocated(eax);\n" 1 "allocation result")
        mm3_light_queue_replace(transformed "loc_00254FF1: ;\n"
            "loc_00254FF1: ;\n    mm3_light_queue_attach();\n" 1 "constructed manager")
    elseif(function_name STREQUAL "sub_0022FF8B")
        mm3_light_queue_replace(transformed "ecx + edi * 4 + 0x5410"
            "mm3_light_queue_slots(ecx) + edi * 4" 3 "push slots")
    elseif(function_name MATCHES "^sub_(0022FFD1|0023006E)$")
        mm3_light_queue_replace(transformed "MEM32(edi + ecx * 4 + 0x5410)"
            "MEM32(mm3_light_queue_slots(edi) + ecx * 4)" 1 "submit slot")
        mm3_light_queue_replace(transformed "_fb = (uint32_t)(0x100) & 0xFFFFFFFFu;"
            "_fb = (uint32_t)(mm3_light_queue_capacity(edi)) & 0xFFFFFFFFu; /* PC-sized light queue */"
            1 "submit capacity")
    elseif(function_name STREQUAL "sub_00230970")
        mm3_light_queue_replace(transformed "eax = esi + 0x5410;"
            "eax = mm3_light_queue_slots(esi);" 1 "consumer slots")
        mm3_light_queue_replace(transformed "loc_0023097F: ;\n    PUSH32(esp, esi);\n    esi = MEM32(ebp + 0x78);\n"
            "loc_0023097F: ;\n    PUSH32(esp, esi);\n    esi = MEM32(ebp + 0x78);\n    mm3_light_queue_frame(esi);\n"
            1 "consumer usage")
    else()
        message(FATAL_ERROR "MM3 light queue: unsupported owner ${function_name}")
    endif()
    string(FIND "${transformed}" "0x5410" leftover)
    if(NOT leftover LESS 0)
        message(FATAL_ERROR "MM3 light queue: ${function_name} still addresses the retail array")
    endif()
    string(REPLACE "${body}" "${transformed}" content "${content}")
    set(declaration "#include <stdint.h>\nextern uint32_t mm3_light_queue_slots(uint32_t);\nextern uint32_t mm3_light_queue_capacity(uint32_t);\nextern uint32_t mm3_light_queue_alloc_size(void);\nextern void mm3_light_queue_allocated(uint32_t);\nextern void mm3_light_queue_attach(void);\nextern void mm3_light_queue_frame(uint32_t);\n")
    set(${source_variable} "${declaration}${content}" PARENT_SCOPE)
endfunction()
