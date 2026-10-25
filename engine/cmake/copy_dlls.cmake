# Windows: copies every DLL that EXE needs and that is not part of Windows into the program's folder, searched in
# DIRS (the folder of SDL3.dll, MSYS2's clang64/bin). Windows looks next to the program before PATH, so the engine
# then starts outside the MSYS2 terminal too. SDL3.dll alone was not enough: MSYS2's SDL3.dll needs libiconv-2.dll,
# which a Windows test (2026-10-21) found loaded from Git for Windows' folder on PATH instead.
#   cmake -DEXE=<biing.exe> -DDIRS=<dir> -DOBJDUMP=<objdump> -P copy_dlls.cmake
if(POLICY CMP0207)  # match the exclude patterns against C:/WINDOWS/system32/... with forward slashes (CMake 4.2+)
  cmake_policy(SET CMP0207 NEW)
endif()
set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "objdump")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${OBJDUMP}")
get_filename_component(dest "${EXE}" DIRECTORY)
file(GET_RUNTIME_DEPENDENCIES
  EXECUTABLES "${EXE}"
  DIRECTORIES ${DIRS}
  RESOLVED_DEPENDENCIES_VAR found
  UNRESOLVED_DEPENDENCIES_VAR missing
  PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
  POST_EXCLUDE_REGEXES "[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\]")
foreach(dll IN LISTS found)
  get_filename_component(dir "${dll}" DIRECTORY)
  if(NOT dir STREQUAL dest)
    file(COPY "${dll}" DESTINATION "${dest}")
  endif()
endforeach()
# Windows' own DLLs (KERNEL32, USER32, ...) are found in the system folder; anything else unresolved is a real gap
foreach(dll IN LISTS missing)
  if(NOT dll MATCHES "^(api-ms-|ext-ms-)")
    message(WARNING "copy_dlls: ${dll} not found next to ${EXE} or in ${DIRS}")
  endif()
endforeach()
