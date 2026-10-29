# psx/tests/abi_symbols.cmake
#
# Exported-symbol audit of the built psx library — shared or static (psx-c-api.md §5,
# §9): it must publish exactly the documented C entry points, publish no C++ mangled
# symbols, and reference no C++ runtime symbol. That is the mechanical form of
# "the core is a C library with zero dependencies".
#
# The undefined-symbol set is also the dependency report: on this platform the core
# asks libc for nothing but the allocator (malloc/calloc/free) plus the stack guard.
#
# Invoked as a CTest command:
#   cmake -D PSX_LIBRARY=<path> -D NM=<nm> -P abi_symbols.cmake
#
# The artifact decides how it is audited, not the host: a PE image publishes its ABI
# in the export table (objdump -p / dumpbin /EXPORTS), ELF and Mach-O in the symbol
# table (nm). Detecting the format from the file also lets a cross-built DLL be
# audited from a non-Windows host. When the right tool is missing the audit skips
# with a visible message rather than silently passing.

if(NOT PSX_LIBRARY)
  message(FATAL_ERROR "psx ABI audit: PSX_LIBRARY not set")
endif()
if(NOT EXISTS "${PSX_LIBRARY}")
  message(FATAL_ERROR "psx ABI audit: library not found: ${PSX_LIBRARY}")
endif()

file(READ "${PSX_LIBRARY}" _psx_magic LIMIT 2 HEX)
get_filename_component(_psx_ext "${PSX_LIBRARY}" LAST_EXT)
string(TOLOWER "${_psx_ext}" _psx_ext)
if(_psx_magic STREQUAL "4D5A" OR _psx_ext STREQUAL ".dll")
  set(_psx_flavour "pe")
elseif(APPLE)
  set(_psx_flavour "macho")
else()
  set(_psx_flavour "elf")
endif()

set(_psx_export_table FALSE)
if(_psx_flavour STREQUAL "pe")
  # On PE the export table *is* the ABI. nm is the wrong tool there: it reports every
  # global in the linked objects plus the toolchain's CRT and startup symbols, none of
  # which are exports (measured on a cross-built libpsx.dll: 216 nm names for a
  # 39-entry export table), so it would both invent missing exports and invent stray
  # ones.
  if(NOT PSX_EXPORT_TOOL)
    find_program(PSX_EXPORT_TOOL NAMES objdump llvm-objdump dumpbin)
  endif()
  if(NOT PSX_EXPORT_TOOL)
    message(STATUS "psx ABI audit: skipped (no objdump/dumpbin to read the PE export table)")
    return()
  endif()
  if(PSX_EXPORT_TOOL MATCHES "dumpbin")
    set(_psx_dumpbin TRUE)
    set(_psx_export_args "/EXPORTS")
  else()
    set(_psx_dumpbin FALSE)
    set(_psx_export_args "-p")
  endif()
  set(_psx_export_table TRUE)
  set(_psx_prefix "") # PE export names are undecorated for cdecl
  execute_process(COMMAND "${PSX_EXPORT_TOOL}" ${_psx_export_args} "${PSX_LIBRARY}"
                  OUTPUT_VARIABLE definedOut ERROR_VARIABLE dumpError RESULT_VARIABLE nmRc)
  if(NOT nmRc EQUAL 0)
    message(FATAL_ERROR "psx ABI audit: export dump failed on ${PSX_LIBRARY}: ${dumpError}")
  endif()
  # The "no C++ runtime referenced" check needs an undefined-symbol list, which nm
  # reads from the PE import table. Whether a host tool can read a PE image at all is
  # not knowable from here, so this sub-check is best-effort and says so out loud when
  # it did not run; the export-side checks below stay authoritative.
  set(_psx_undef_tool "${NM}")
  if(NOT _psx_undef_tool)
    find_program(_psx_undef_tool NAMES nm llvm-nm x86_64-w64-mingw32-nm)
  endif()
  set(_psx_undef_checked FALSE)
  if(_psx_undef_tool)
    execute_process(COMMAND "${_psx_undef_tool}" --undefined-only "${PSX_LIBRARY}"
                    OUTPUT_VARIABLE undefinedOut ERROR_QUIET RESULT_VARIABLE undefRc)
    if(undefRc EQUAL 0)
      set(_psx_undef_checked TRUE)
    else()
      set(undefinedOut "")
      message(STATUS
        "psx ABI audit: the no-C++-reference sub-check did not run "
        "(${_psx_undef_tool} cannot read this PE image)")
    endif()
  endif()
else()
  if(NOT NM)
    message(STATUS "psx ABI audit: skipped (no nm found)")
    return()
  endif()
  # Defined and exported symbols. macOS prefixes C symbols with '_'.
  if(APPLE)
    execute_process(COMMAND "${NM}" -gU "${PSX_LIBRARY}"
                    OUTPUT_VARIABLE definedOut ERROR_VARIABLE nmError RESULT_VARIABLE nmRc)
    execute_process(COMMAND "${NM}" -u "${PSX_LIBRARY}"
                    OUTPUT_VARIABLE undefinedOut RESULT_VARIABLE undefRc)
    set(_psx_prefix "_")
  else()
    execute_process(COMMAND "${NM}" --defined-only --extern-only "${PSX_LIBRARY}"
                    OUTPUT_VARIABLE definedOut ERROR_VARIABLE nmError RESULT_VARIABLE nmRc)
    execute_process(COMMAND "${NM}" --undefined-only "${PSX_LIBRARY}"
                    OUTPUT_VARIABLE undefinedOut RESULT_VARIABLE undefRc)
    set(_psx_prefix "")
  endif()
  if(NOT nmRc EQUAL 0)
    message(FATAL_ERROR "psx ABI audit: nm failed on ${PSX_LIBRARY}: ${nmError}")
  endif()
endif()


# Normalise nm output to a list of bare symbol names. Depending on platform and
# arguments nm emits either "name" or "address type name"; the name is always last.
function(_psx_symbol_list rawText outVar)
  set(lines "")
  string(REPLACE "\n" ";" lineList "${rawText}")
  foreach(line IN LISTS lineList)
    string(STRIP "${line}" entry)
    if(entry STREQUAL "")
      continue()
    endif()
    # A static archive makes nm print one header per member ("bpe.c.o:"); those are
    # not symbols, and the symbol checks below would flag them as undocumented.
    if(entry MATCHES ":$")
      continue()
    endif()
    if(_psx_export_table)
      # Export-table rows only, matched by shape so that neither ordinal-only rows,
      # import rows, nor section headers can be misread as exports:
      #   GNU objdump:  "[  34] +base[  35]  0022 psx_tim_parse"
      #   LLVM objdump: "1   0x31f0  psx_abi_version"
      #   dumpbin:      "      34  0 000031F0 psx_tim_parse = psx_tim_parse"
      # The two objdump spellings cannot be told apart by the executable's name
      # (`objdump` on macOS *is* llvm-objdump, and printed the LLVM shape here), so
      # both are accepted and each row is rejected by shape rather than by trusting
      # which tool happens to be installed. Measured on libpsx.dll: the LLVM shape
      # matches exactly the 39 export rows and none of the import rows, which read
      # as "295  DeleteCriticalSection" (two fields, so the shape cannot take them).
      string(REGEX REPLACE " =.*$" "" entry "${entry}")
      # The cut above leaves a trailing space on dumpbin rows, which would make every
      # one of them fail its own shape test.
      string(STRIP "${entry}" entry)
      set(_psx_row_shapes "")
      if(_psx_dumpbin)
        list(APPEND _psx_row_shapes "^[0-9]+[	 ]+[0-9a-fA-F]+[	 ]+[0-9a-fA-F]+[	 ]+[A-Za-z_][A-Za-z0-9_]*$")
      else()
        list(APPEND _psx_row_shapes "^\\[ *[0-9]+\\][	 ]+\\+base\\[ *[0-9]+\\][	 ]+[^	 ]+[	 ]+[A-Za-z_][A-Za-z0-9_]*$")
        list(APPEND _psx_row_shapes "^[0-9]+[	 ]+0x[0-9a-fA-F]+[	 ]+[A-Za-z_][A-Za-z0-9_]*$")
      endif()
      set(_psx_row_ok FALSE)
      foreach(_psx_shape IN LISTS _psx_row_shapes)
        if(entry MATCHES "${_psx_shape}")
          set(_psx_row_ok TRUE)
        endif()
      endforeach()
      if(NOT _psx_row_ok)
        continue()
      endif()
      string(REGEX REPLACE "^.*[	 ]" "" _psx_last "${entry}")
      if(_psx_last STREQUAL "RVA" OR _psx_last STREQUAL "Name")
        continue()
      endif()
    endif()
    string(REGEX REPLACE "^.*[ \t]" "" name "${entry}")
    list(APPEND lines "${name}")
  endforeach()
  set(${outVar} "${lines}" PARENT_SCOPE)
endfunction()

_psx_symbol_list("${definedOut}" definedSymbols)
_psx_symbol_list("${undefinedOut}" undefinedSymbols)

set(_psx_required
    # version
    psx_version_major psx_version_minor psx_version_patch psx_version_string psx_abi_version
    # error
    psx_error_name psx_error_message psx_error_is_bpe psx_error_is_tim psx_error_is_mrg psx_error_is_deff
    psx_error_is_xa
    # bpe
    psx_bpe_is_bpe psx_bpe_declared_size psx_bpe_decompress psx_bpe_compressed_size psx_bpe_compress
    # tim
    psx_tim_is_tim psx_tim_parse psx_tim_clut_entry_count psx_tim_image_init
    # xa
    psx_xa_stream_init psx_xa_coding_info psx_xa_decoded_samples psx_xa_decode_sector
    # archive
    psx_archive_is_mrg psx_archive_is_deff
    psx_archive_mrg_entry_count psx_archive_mrg_parse
    psx_mrg_table_parse psx_mrg_table_destroy psx_mrg_table_count psx_mrg_table_virtual_count
    psx_mrg_table_view psx_mrg_view_at psx_mrg_view_slice
    psx_archive_deff_subfile_count psx_archive_deff_parse
    psx_deff_table_parse psx_deff_table_destroy psx_deff_table_count psx_deff_table_view
    psx_deff_view_at psx_deff_view_slice)

# psx_bytes_is_valid, psx_writable_bytes_is_valid and psx_mrg_entry_is_virtual are
# `static inline` header predicates: trivial, inlinable, and deliberately absent from
# the ABI, so nm cannot see them. They are exercised by the C consumer instead
# (psx.c_consumer), which would fail to compile if they disappeared.
set(_psx_header_inline psx_bytes_is_valid psx_writable_bytes_is_valid psx_mrg_entry_is_virtual)

set(_psx_problems "")
foreach(symbol IN LISTS _psx_required)
  if(NOT "${_psx_prefix}${symbol}" IN_LIST definedSymbols)
    list(APPEND _psx_problems "missing export: ${symbol}")
  endif()
endforeach()

# Nothing undocumented is exported: every defined symbol is a psx entry point.
foreach(symbol IN LISTS definedSymbols)
  if(NOT symbol MATCHES "^${_psx_prefix}psx_")
    list(APPEND _psx_problems "undocumented export: ${symbol}")
  endif()
endforeach()

# No C++ mangling in either direction: nothing of the C++ runtime is exported and
# nothing of it is required. A std::__ reference in the undefined set would mean the
# core had quietly acquired a C++ dependency.
foreach(symbol IN LISTS definedSymbols)
  if(symbol MATCHES "_Z|__cxa|_ZNSt")
    list(APPEND _psx_problems "C++ symbol exported: ${symbol}")
  endif()
endforeach()
foreach(symbol IN LISTS undefinedSymbols)
  if(symbol MATCHES "_Z|__cxa|_ZNSt|std::__")
    list(APPEND _psx_problems "C++ runtime reference: ${symbol}")
  endif()
endforeach()

list(LENGTH _psx_required requiredCount)
list(LENGTH _psx_header_inline inlineCount)
list(LENGTH definedSymbols definedCount)
if(_psx_problems)
  list(REMOVE_DUPLICATES _psx_problems)
  message(FATAL_ERROR
          "psx ABI audit FAILED for ${PSX_LIBRARY}\n"
          "  required: ${requiredCount} exported + ${inlineCount} header-inline\n"
          "  exported by library: ${definedCount}\n"
          "  problems: ${_psx_problems}\n"
          "  Every documented entry point must be exported, and the core must stay free"
          " of C++ symbols (psx-c-api.md 5, 9).")
endif()

# The report says what was actually verified. On PE the import table may be
# unreadable from the host, and that is reported rather than folded into a
# blanket "no C++ symbols" claim.
if(_psx_flavour STREQUAL "pe" AND NOT _psx_undef_checked)
  set(_psx_undef_note "export table only (import table not read)")
else()
  set(_psx_undef_note "no C++ symbols exported or referenced")
endif()
message(STATUS
        "psx ABI audit (${_psx_flavour}): ${requiredCount} exported symbols present (${definedCount} total),"
        " ${inlineCount} header-inline predicates checked by the C consumer, ${_psx_undef_note}")
