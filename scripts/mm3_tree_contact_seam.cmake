# Select the struck tree before the native speed/hop gate. Its first impact
# uses native solid resistance/effects; the wrapper creates a native rigid body.
function(mm3_add_tree_contacts source_variable)
    set(content "${${source_variable}}")
    set(original [=[    MEM32(ebp + 0x64) = ecx;
    _fa = (uint32_t)(HI8(eax)) & 0xFFu; _fb = (uint32_t)(0x41) & 0xFFu;
    _fas = (int32_t)(int8_t)(_fa); _fbs = (int32_t)(int8_t)(_fb); /* test HI8(eax), 0x41 (8-bit) */
    if (TEST_Z(_fa, _fb)) goto loc_001D1723;]=])
    set(replacement [=[    MEM32(ebp + 0x64) = ecx;
    if (mm3_tree_use_solid_contact(ecx, MEMF(ebp + 0x6C))) goto loc_001D1723;
    _fa = (uint32_t)(HI8(eax)) & 0xFFu; _fb = (uint32_t)(0x41) & 0xFFu;
    _fas = (int32_t)(int8_t)(_fa); _fbs = (int32_t)(int8_t)(_fb); /* test HI8(eax), 0x41 (8-bit) */
    if (TEST_Z(_fa, _fb)) goto loc_001D1723;]=])
    string(FIND "${content}" "\r\n" crlf)
    if(NOT crlf LESS 0)
        string(REPLACE "\n" "\r\n" original "${original}")
        string(REPLACE "\n" "\r\n" replacement "${replacement}")
    endif()
    string(FIND "${content}" "${original}" position)
    string(FIND "${content}" "mm3_tree_use_solid_contact" already)
    if(position LESS 0 OR NOT already LESS 0)
        message(FATAL_ERROR "MM3 tree contact: original hop boundary changed or already patched")
    endif()
    string(LENGTH "${original}" length)
    math(EXPR after "${position} + ${length}")
    string(SUBSTRING "${content}" ${after} -1 tail)
    string(FIND "${tail}" "${original}" duplicate)
    if(NOT duplicate LESS 0)
        message(FATAL_ERROR "MM3 tree contact: duplicate hop boundary")
    endif()
    string(REPLACE "${original}" "${replacement}" content "${content}")
    set(declaration "#include <stdint.h>\nextern int mm3_tree_use_solid_contact(uint32_t, float);\n")
    if(NOT crlf LESS 0)
        string(REPLACE "\n" "\r\n" declaration "${declaration}")
    endif()
    set(${source_variable} "${declaration}${content}" PARENT_SCOPE)
endfunction()

# Native geometry factories assume their 0x27C allocation succeeded. A detached
# tree reserves that allocation before entry; regular callers keep the retail
# allocator. Guard exact allocation boundaries in both static instance classes.
function(mm3_add_tree_geometry source_variable owner)
    set(content "${${source_variable}}")
    if(owner STREQUAL "sub_0015CD35")
        set(return_address "0015CD97")
    elseif(owner STREQUAL "sub_0015B9AB")
        set(return_address "0015BA0E")
    else()
        message(FATAL_ERROR "MM3 tree geometry: unknown owner ${owner}")
    endif()
    set(original "    PUSH32(esp, 0x${return_address}u); RECOMP_ABI_CALL(0x0016FF04u, sub_0016FF04); /* call 0x0016FF04 */")
    set(replacement "    PUSH32(esp, 0x${return_address}u); if (!mm3_tree_body_geometry_allocation()) RECOMP_ABI_CALL(0x0016FF04u, sub_0016FF04); /* call 0x0016FF04 */")
    string(FIND "${content}" "${original}" position)
    string(FIND "${content}" "0x${return_address}u); if (!mm3_tree_body_geometry_allocation())" already)
    if(position LESS 0 OR NOT already LESS 0)
        message(FATAL_ERROR "MM3 tree geometry: ${owner} allocation boundary changed or already patched")
    endif()
    string(LENGTH "${original}" length)
    math(EXPR after "${position} + ${length}")
    string(SUBSTRING "${content}" ${after} -1 tail)
    string(FIND "${tail}" "${original}" duplicate)
    if(NOT duplicate LESS 0)
        message(FATAL_ERROR "MM3 tree geometry: duplicate ${owner} allocation boundary")
    endif()
    string(REPLACE "${original}" "${replacement}" content "${content}")
    set(declaration "extern int mm3_tree_body_geometry_allocation(void);\n")
    string(FIND "${content}" "${declaration}" declared)
    if(declared LESS 0)
        set(content "${declaration}${content}")
    endif()
    set(${source_variable} "${content}" PARENT_SCOPE)
endfunction()

# The tree billboard builds upright vertices from the transform's translation,
# ignoring its rotated basis. Falling draws must use the full native mesh.
function(mm3_add_tree_mesh source_variable)
    set(content "${${source_variable}}")
    set(original [=[loc_0016B04E: ;
    fp_push(MEMF(ebp + 8)); /* fld float */]=])
    set(replacement [=[loc_0016B04E: ;
    if (mm3_tree_fall_drawing()) goto loc_0016B072;
    fp_push(MEMF(ebp + 8)); /* fld float */]=])
    string(FIND "${content}" "\r\n" crlf)
    if(NOT crlf LESS 0)
        string(REPLACE "\n" "\r\n" original "${original}")
        string(REPLACE "\n" "\r\n" replacement "${replacement}")
    endif()
    string(FIND "${content}" "${original}" position)
    string(FIND "${content}" "mm3_tree_fall_drawing" already)
    set(mesh_boundary [=[loc_0016B072: ;
    esi = MEM32(ebp + 0xC);

loc_0016B075: ;]=])
    if(NOT crlf LESS 0)
        string(REPLACE "\n" "\r\n" mesh_boundary "${mesh_boundary}")
    endif()
    string(FIND "${content}" "${mesh_boundary}" mesh_entry)
    if(position LESS 0 OR NOT already LESS 0 OR mesh_entry LESS 0)
        message(FATAL_ERROR "MM3 tree mesh: billboard boundary changed or already patched")
    endif()
    string(LENGTH "${original}" length)
    math(EXPR after "${position} + ${length}")
    string(SUBSTRING "${content}" ${after} -1 tail)
    string(FIND "${tail}" "${original}" duplicate)
    if(NOT duplicate LESS 0)
        message(FATAL_ERROR "MM3 tree mesh: duplicate billboard boundary")
    endif()
    string(REPLACE "${original}" "${replacement}" content "${content}")
    set(${source_variable} "extern int mm3_tree_fall_drawing(void);\n${content}" PARENT_SCOPE)
endfunction()
