# Police car creation sub_0015070A stops at the first step that returns 0
# (src/mm3_police_log.c): note each step's return point it reaches, and the
# car made (EAX) or not at its return, so [MM3_POLICE] can name the step.
# Inside its factory call the factory sub_0013BB36, the car init sub_00220761
# and the model build sub_002202EE note each result too, so a refused car
# names what the factory could not do and the model it was loading.

# The one body of `owner` in the variable named by content_variable, refused
# when missing, duplicated or already noted.
function(_mm3_police_body content_variable owner body_variable)
    set(content "${${content_variable}}")
    string(REGEX MATCHALL "void ${owner}[(]void[)]" owners "${content}")
    list(LENGTH owners owner_count)
    if(NOT owner_count EQUAL 1)
        message(FATAL_ERROR "MM3 police create: missing or duplicate ${owner}")
    endif()
    string(FIND "${content}" "void ${owner}(void)" owner_start)
    string(SUBSTRING "${content}" ${owner_start} -1 owner_tail)
    string(FIND "${owner_tail}" "\nvoid " owner_end)
    if(owner_end LESS 0)
        set(body "${owner_tail}")
    else()
        string(SUBSTRING "${owner_tail}" 0 ${owner_end} body)
    endif()
    string(FIND "${body}" "mm3_police_" already)
    if(NOT already LESS 0)
        message(FATAL_ERROR "MM3 police create: ${owner} already patched")
    endif()
    set(${body_variable} "${body}" PARENT_SCOPE)
endfunction()

# Run `code` at `label` in the body, refused when the label changed.
function(_mm3_police_at body_variable owner label code)
    set(body "${${body_variable}}")
    string(FIND "${body}" "\n${label}: ;" at)
    if(at LESS 0)
        message(FATAL_ERROR "MM3 police create: ${owner} label ${label} changed")
    endif()
    string(REPLACE "\n${label}: ;" "\n${label}: ; ${code}" body "${body}")
    set(${body_variable} "${body}" PARENT_SCOPE)
endfunction()

function(mm3_add_police_create_steps source_variable)
    set(content "${${source_variable}}")
    set(owner sub_0015070A)
    _mm3_police_body(content ${owner} body)
    set(original_body "${body}")
    set(step 0)
    # Manager ready, then after the car type lookup, the police class check,
    # the placement, the 0x298C allocation and the factory.
    foreach(label loc_00150720 loc_0015072D loc_0015074D loc_00150764 loc_0015077C loc_001507A4)
        _mm3_police_at(body ${owner} ${label}
            "{ extern void mm3_police_step(int step); mm3_police_step(${step}); }")
        math(EXPR step "${step} + 1")
    endforeach()
    # The factory call follows; its notes count from here.
    _mm3_police_at(body ${owner} loc_0015079D
        "{ extern void mm3_police_factory_begin(void); mm3_police_factory_begin(); }")
    set(original [=[
loc_001507D3: ;
    POP32(esp, edi);]=])
    set(replacement [=[
loc_001507D3: ; { extern void mm3_police_created(uint32_t actor, uint32_t name); mm3_police_created(eax, MEM32(ebp + 8)); }
    POP32(esp, edi);]=])
    string(FIND "${body}" "\r\n" crlf)
    if(NOT crlf LESS 0)
        string(REPLACE "\n" "\r\n" original "${original}")
        string(REPLACE "\n" "\r\n" replacement "${replacement}")
    endif()
    string(FIND "${body}" "${original}" at)
    if(at LESS 0)
        message(FATAL_ERROR "MM3 police create: ${owner} return changed")
    endif()
    string(REPLACE "${original}" "${replacement}" body "${body}")
    string(REPLACE "${original_body}" "${body}" content "${content}")
    set(${source_variable} "${content}" PARENT_SCOPE)
endfunction()

# Notes in the order of src/mm3_police_log.c's enum: the factory's 0x82C
# allocation, constructed car, car init and attach results; the init's
# missing object handler, missing collision files (with the car's model name)
# and model build result; the build's streamed model, body model (with its
# path), setup and physics results.
set(MM3_POLICE_FACTORY_OWNERS sub_0013BB36 sub_00220761 sub_002202EE)
function(mm3_add_police_factory_notes source_variable owner)
    set(content "${${source_variable}}")
    _mm3_police_body(content ${owner} body)
    set(original_body "${body}")
    set(note "{ extern void mm3_police_note(int where, uint32_t value); mm3_police_note(")
    set(path "{ extern void mm3_police_note_path(uint32_t text); mm3_police_note_path(")
    if(owner STREQUAL "sub_0013BB36")
        _mm3_police_at(body ${owner} loc_0013BB7C "${note}0, eax); }")
        _mm3_police_at(body ${owner} loc_0013BB8E "${note}1, edi); }")
        _mm3_police_at(body ${owner} loc_0013BBD0 "${note}2, LO8(eax)); }")
        _mm3_police_at(body ${owner} loc_0013BBF5 "${note}3, LO8(eax)); }")
    elseif(owner STREQUAL "sub_00220761")
        _mm3_police_at(body ${owner} loc_00220786 "${note}4, 0); }")
        _mm3_police_at(body ${owner} loc_00220A3C "${path}ebx + 0x1F8); } ${note}5, 0); }")
        _mm3_police_at(body ${owner} loc_00220AF1 "${note}6, LO8(eax)); }")
    elseif(owner STREQUAL "sub_002202EE")
        _mm3_police_at(body ${owner} loc_00220362 "${path}ebp - 28); } ${note}7, eax); }")
        _mm3_police_at(body ${owner} loc_0022037E "${path}ebp - 28); } ${note}8, eax); }")
        _mm3_police_at(body ${owner} loc_002203BF "${note}9, LO8(eax)); }")
        _mm3_police_at(body ${owner} loc_002203CD "${note}10, LO8(eax)); }")
    else()
        message(FATAL_ERROR "MM3 police create: ${owner} has no factory notes")
    endif()
    string(REPLACE "${original_body}" "${body}" content "${content}")
    set(${source_variable} "${content}" PARENT_SCOPE)
endfunction()
