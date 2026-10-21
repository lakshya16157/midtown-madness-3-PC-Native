# The existing building wrapper owns sub_001C7B15. After the ordinary
# definition-only hook names its body _original, retain that exact generated
# implementation as _generated and supply the scalar _original in project C.
# Pin the complete arithmetic/branch/frame body: a regeneration must not
# silently enable a native implementation for a changed guest contract.
function(mm3_keep_masked_visibility_fallback source_variable)
    set(content "${${source_variable}}")
    string(REGEX MATCHALL "void sub_001C7B15_original[(]void[)]" owners "${content}")
    list(LENGTH owners owner_count)
    string(FIND "${content}" "void sub_001C7B15_original(void)" owner_start)
    if(NOT owner_count EQUAL 1 OR owner_start LESS 0)
        message(FATAL_ERROR "MM3 masked visibility: missing or duplicate hooked leaf")
    endif()
    string(SUBSTRING "${content}" ${owner_start} -1 tail)
    string(FIND "${tail}" "\n}" owner_end)
    if(owner_end LESS 0)
        message(FATAL_ERROR "MM3 masked visibility: incomplete leaf")
    endif()
    math(EXPR owner_length "${owner_end} + 2")
    string(SUBSTRING "${tail}" 0 ${owner_length} body)
    string(REPLACE "\r\n" "\n" normalized "${body}")
    string(SHA256 fingerprint "${normalized}")
    if(NOT fingerprint STREQUAL "1a7a645bb75a067e7c637b841baf180a0656d69bf415663fbd6e48243e879e35")
        message(FATAL_ERROR "MM3 masked visibility: generated arithmetic/frame contract changed")
    endif()
    string(REPLACE "void sub_001C7B15_original(void)"
        "void sub_001C7B15_generated(void)" renamed "${body}")
    string(REPLACE "${body}" "${renamed}" content "${content}")
    set(${source_variable} "${content}" PARENT_SCOPE)
endfunction()
