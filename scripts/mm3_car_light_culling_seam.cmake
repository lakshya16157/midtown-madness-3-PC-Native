# Collect vehicle illumination after the current world transform is composed,
# before the body's sphere/OBB/occlusion rejection. Geometry retains its gates.
function(mm3_add_car_light_culling_seam source_variable function_name)
    set(content "${${source_variable}}")
    if(NOT function_name MATCHES "^sub_000E01C2$|^sub_000E0277$")
        message(FATAL_ERROR "MM3 car lights: unsupported culling owner ${function_name}")
    endif()
    string(REGEX MATCHALL "void ${function_name}[(]void[)]" owners "${content}")
    list(LENGTH owners owner_count)
    if(NOT owner_count EQUAL 1)
        message(FATAL_ERROR "MM3 car lights: missing/duplicate ${function_name}")
    endif()
    string(FIND "${content}" "void ${function_name}(void)" owner_start)
    string(SUBSTRING "${content}" ${owner_start} -1 owner_tail)
    string(FIND "${owner_tail}" "\nvoid " owner_end)
    if(owner_end LESS 0)
        set(body "${owner_tail}")
    else()
        string(SUBSTRING "${owner_tail}" 0 ${owner_end} body)
    endif()
    set(boundary "loc_000E029C: ;\n    eax = ebx + edi * 4 + 0x11C;")
    string(FIND "${body}" "${boundary}" boundary_start)
    if(boundary_start LESS 0)
        message(FATAL_ERROR "MM3 car lights: current-transform/culling boundary changed in ${function_name}")
    endif()
    string(REPLACE "${boundary}"
        "loc_000E029C: ;\n    mm3_car_lighting_model(ebx, MEM32(ebp + 0xC));\n    eax = ebx + edi * 4 + 0x11C;"
        transformed "${body}")
    string(REPLACE "${body}" "${transformed}" content "${content}")
    set(${source_variable}
        "#include <stdint.h>\nextern void mm3_car_lighting_model(uint32_t, uint32_t);\n${content}"
        PARENT_SCOPE)
endfunction()
