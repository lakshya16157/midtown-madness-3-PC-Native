# Before the title installs its input bindings, select its own wheel layout
# for player 1 when the enabled host wheel is connected. Keep the saved profile.
function(mm3_apply_wheel_layout source_variable)
    set(content "${${source_variable}}")
    set(saved_layout [=[    MEM8(esi + 0x8B9) = LO8(ecx);
    eax = MEM32(eax + edi * 4 + 0x194);]=])
    set(wheel_layout [=[    MEM8(esi + 0x8B9) = LO8(ecx);
    eax = MEM32(eax + edi * 4 + 0x194);
    if (edi == 0 && xbox_InputWheelConnected(0))
        eax = 2; /* Original Configuration C. */]=])

    string(FIND "${content}" "\r\n" crlf)
    if(NOT crlf LESS 0)
        string(REPLACE "\n" "\r\n" saved_layout "${saved_layout}")
        string(REPLACE "\n" "\r\n" wheel_layout "${wheel_layout}")
    endif()
    string(FIND "${content}" "void sub_001F47E4(void)" owner)
    string(FIND "${content}" "loc_001F47EC:" layout_label)
    string(FIND "${content}" "loc_001F4808:" next_label)
    string(FIND "${content}" "${saved_layout}" layout_read)
    string(FIND "${content}" "${saved_layout}" last_layout_read REVERSE)
    if(owner LESS 0 OR layout_label LESS owner OR layout_read LESS layout_label
            OR next_label LESS layout_read OR NOT layout_read EQUAL last_layout_read)
        message(FATAL_ERROR "MM3 racing wheel: native controller layout read changed or was duplicated")
    endif()
    string(REPLACE "${saved_layout}" "${wheel_layout}" content "${content}")
    set(${source_variable} "#include \"xinput_xbox.h\"\n${content}" PARENT_SCOPE)
endfunction()
