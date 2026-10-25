# Clean-room Biing! engine. Reads the user's own copy of the game (the BIING! directory); no game data is
# built in. Written from the specs in docs/, never from the disassembly (see AGENTS.md).
#
#   cmake -S engine -B build -G Ninja && cmake --build build && ctest --test-dir build
#   build/biing <path-to-BIING!-dir>
cmake_minimum_required(VERSION 3.24)
project(biing C)
option(BIING_RECORD_CHECKS "Diagnose wrong staff/slot records in the working-mood path" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
# signed overflow wraps as in the original's x86 code instead of being undefined (docs/engine.md, 2026-10-20)
add_compile_options(-fwrapv)
# Windows (MSYS2 clang64 / MinGW) gives the main thread 1 MB of stack, macOS 8 MB; the game and the tests keep large
# structures on the stack (test_world overflowed at 1 MB), so every program gets 16 MB there.
if(WIN32)
  add_link_options(-Wl,--stack,16777216)
endif()

# Everything that does not need SDL: formats, decoders, frame buffer, text, screen layer, game state.
add_library(biingcore STATIC src/fixes.c src/assets.c src/gfx.c src/program.c src/text.c src/dulog.c src/game.c src/ranking.c src/world.c src/sim.c src/sim_ai.c src/sim_patients.c src/sim_staff.c src/sim_rooms.c src/sim_schedule.c src/sim_news.c src/sim_news_gen.c src/sim_player.c src/sim_books.c src/estate.c src/estate_ui.c src/personnel.c src/personnel_ui.c src/rooms.c src/rooms_ui.c src/depts.c src/depts_ui.c src/prices.c src/prices_ui.c src/purchase.c src/purchase_ui.c src/bank.c src/bank_ui.c src/report.c src/advice.c src/newspaper.c src/reports_ui.c src/support.c src/support_ui.c src/sim_thugs.c src/garage.c src/sim_race.c src/garage_ui.c src/desktop.c src/desktop_ui.c src/mixer.c src/music.c src/midi.c src/cdaudio.c src/sound.c src/room_sound.c src/sheet.c src/intro.c src/intro_pre.c src/intro_relo.c src/intro_bint.c src/vpx.c src/tutorial.c src/startup.c)
target_include_directories(biingcore PUBLIC src)
target_sources(biingcore PRIVATE src/staff_record.c src/control.c src/inspector.c)
if(BIING_RECORD_CHECKS)
  target_compile_definitions(biingcore PUBLIC BIING_RECORD_CHECKS=1)
endif()
if(UNIX AND NOT APPLE)
  target_link_libraries(biingcore PUBLIC m) # the intro programs' sine table
endif()

find_package(SDL3 REQUIRED CONFIG)
add_executable(biing src/main.c src/viewer.c src/midi_host.c)
target_link_libraries(biing PRIVATE biingcore SDL3::SDL3)
# MIDI music to the host (midi_host.c, SND-9): only the window build links the platform's MIDI libraries, so biingcore
# and the tests stay free of them.
if(APPLE)
  target_link_libraries(biing PRIVATE "-framework AudioToolbox" "-framework CoreMIDI" "-framework CoreFoundation")
elseif(WIN32)
  target_link_libraries(biing PRIVATE winmm)
endif()
# Windows finds a DLL next to the program before PATH: copy SDL3.dll and the DLLs it needs there, so build/biing.exe
# also starts outside the MSYS2 terminal (from PowerShell, cmd or Explorer), where clang64/bin is not in PATH and
# another program's copy of a DLL could be found instead (MANUAL.md section 8, engine/cmake/copy_dlls.cmake).
if(WIN32 AND TARGET SDL3::SDL3-shared)
  add_custom_command(TARGET biing POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:biing> -DDIRS=$<TARGET_FILE_DIR:SDL3::SDL3-shared> -DOBJDUMP=${CMAKE_OBJDUMP}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_dlls.cmake
    VERBATIM)
endif()

# Decodes every asset of a game directory and prints one line per item, for parity checks against
# the Python tools (tools/engine_parity.py).
add_executable(biing-check src/check.c)
target_link_libraries(biing-check PRIVATE biingcore)

# Save games on the command line: info, round trip, the engine's new game, and its diff against a save.
add_executable(biing-save src/savetool.c)
target_link_libraries(biing-save PRIVATE biingcore)

enable_testing()
add_executable(test_assets tests/test_assets.c)
target_link_libraries(test_assets PRIVATE biingcore)
add_test(NAME assets COMMAND test_assets)
# Compile the real file and program loaders with test-only libc failure injection.
add_executable(test_loader_io tests/test_loader_io.c src/assets.c src/program.c)
target_include_directories(test_loader_io PRIVATE src)
target_compile_definitions(test_loader_io PRIVATE fopen=loader_test_fopen fclose=loader_test_fclose
    fseek=loader_test_fseek ftell=loader_test_ftell fread=loader_test_fread malloc=loader_test_malloc
    calloc=loader_test_calloc free=loader_test_free getenv=loader_test_getenv atexit=loader_test_atexit)
add_test(NAME loader_io COMMAND test_loader_io)
add_test(NAME map_log_alloc_first COMMAND test_loader_io 1)
add_test(NAME map_log_alloc_second COMMAND test_loader_io 2)
add_test(NAME map_log_control COMMAND test_loader_io 0)
add_executable(test_ui tests/test_ui.c)
target_link_libraries(test_ui PRIVATE biingcore)
add_test(NAME ui COMMAND test_ui)
add_executable(test_world tests/test_world.c)
target_link_libraries(test_world PRIVATE biingcore)
add_test(NAME world COMMAND test_world)
add_executable(test_books tests/test_books.c)
target_link_libraries(test_books PRIVATE biingcore)
add_test(NAME books COMMAND test_books)
add_executable(test_sim tests/test_sim.c)
target_link_libraries(test_sim PRIVATE biingcore)
add_test(NAME sim COMMAND test_sim)
add_executable(test_staff_record tests/test_staff_record.c)
target_link_libraries(test_staff_record PRIVATE biingcore)
add_test(NAME staff_record COMMAND test_staff_record)
if(BIING_RECORD_CHECKS)
  add_test(NAME staff_record_failures COMMAND ${CMAKE_COMMAND} -DDRIVER=$<TARGET_FILE:test_staff_record>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_staff_record.cmake)
endif()
# The real save-driver entry point and environment wiring, with a synthetic program-table loader only.
add_executable(test_savetool tests/test_savetool.c src/savetool.c)
target_compile_definitions(test_savetool PRIVATE main=savetool_main program_load=savetool_test_program_load)
target_link_libraries(test_savetool PRIVATE biingcore)
add_test(NAME savetool COMMAND ${CMAKE_COMMAND} -DDRIVER=$<TARGET_FILE:test_savetool>
    -DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/savetool-fixture -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_savetool.cmake)
# Real headless entry point with synthetic frame/open callbacks and a portable failing trace stream.
add_executable(test_shot tests/test_shot.c src/main.c src/midi_host.c)
target_compile_definitions(test_shot PRIVATE SDL_MAIN_HANDLED main=shot_main biing_open=shot_test_open
    biing_close=shot_test_close biing_frame=shot_test_frame biing_interrupt=shot_test_interrupt fopen=shot_test_fopen
    startup_open=shot_test_startup_open startup_advance=shot_test_startup_advance)
target_link_libraries(test_shot PRIVATE $<TARGET_PROPERTY:biing,LINK_LIBRARIES>)
add_test(NAME shot COMMAND ${CMAKE_COMMAND} -DDRIVER=$<TARGET_FILE:test_shot>
    -DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/shot-fixture -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_shot.cmake)
add_test(NAME window_input COMMAND test_shot window-input)
add_test(NAME window_mouse COMMAND test_shot window-mouse)
add_test(NAME window_focus COMMAND test_shot window-focus)
set_tests_properties(window_input window_mouse window_focus PROPERTIES TIMEOUT 15
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;BIING_MIDI_OUT=none;BIING_FIXES=off")
# Real window audio submission and device consumption, with synthetic game/startup sound only.
add_executable(test_window_audio tests/test_window_audio.c src/main.c src/midi_host.c)
target_compile_definitions(test_window_audio PRIVATE SDL_MAIN_HANDLED main=audio_test_main
    biing_open=audio_test_open biing_close=audio_test_close biing_frame=audio_test_frame
    biing_interrupt=audio_test_interrupt startup_open=audio_test_startup_open
    startup_advance=audio_test_startup_advance sound_interrupt=audio_test_sound_interrupt
    SDL_OpenAudioDeviceStream=audio_test_open_stream SDL_PutAudioStreamData=audio_test_put_stream)
target_link_libraries(test_window_audio PRIVATE $<TARGET_PROPERTY:biing,LINK_LIBRARIES>)
add_test(NAME window_audio COMMAND test_window_audio)
add_test(NAME window_intro_audio COMMAND test_window_audio intro)
set_tests_properties(window_audio window_intro_audio PROPERTIES TIMEOUT 15
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;BIING_MIDI_OUT=none;BIING_FIXES=off")
add_executable(test_control tests/test_control.c)
target_link_libraries(test_control PRIVATE biingcore)
add_test(NAME control COMMAND test_control)
add_executable(test_inspector tests/test_inspector.c)
target_link_libraries(test_inspector PRIVATE biingcore)
add_test(NAME inspector COMMAND test_inspector)
add_test(NAME control_session COMMAND ${CMAKE_COMMAND} -DDRIVER=$<TARGET_FILE:test_shot>
    -DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/control-fixture -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_control.cmake)
add_executable(test_estate tests/test_estate.c)
target_link_libraries(test_estate PRIVATE biingcore)
add_test(NAME estate COMMAND test_estate)
add_executable(test_personnel tests/test_personnel.c)
target_link_libraries(test_personnel PRIVATE biingcore)
add_test(NAME personnel COMMAND test_personnel)
add_executable(test_news tests/test_news.c)
target_link_libraries(test_news PRIVATE biingcore)
add_test(NAME news COMMAND test_news)
add_executable(test_news_gen tests/test_news_gen.c)
target_link_libraries(test_news_gen PRIVATE biingcore)
add_test(NAME news_gen COMMAND test_news_gen)
add_executable(test_rooms tests/test_rooms.c)
target_link_libraries(test_rooms PRIVATE biingcore)
add_test(NAME rooms COMMAND test_rooms)
add_executable(test_player_rooms tests/test_player_rooms.c)
target_link_libraries(test_player_rooms PRIVATE biingcore)
add_test(NAME player_rooms COMMAND test_player_rooms)
add_executable(test_purchase tests/test_purchase.c)
target_link_libraries(test_purchase PRIVATE biingcore)
add_test(NAME purchase COMMAND test_purchase)
add_executable(test_depts tests/test_depts.c)
target_link_libraries(test_depts PRIVATE biingcore)
add_test(NAME depts COMMAND test_depts)
add_executable(test_prices tests/test_prices.c)
target_link_libraries(test_prices PRIVATE biingcore)
add_test(NAME prices COMMAND test_prices)
add_executable(test_bank tests/test_bank.c)
target_link_libraries(test_bank PRIVATE biingcore)
add_test(NAME bank COMMAND test_bank)
add_executable(test_support tests/test_support.c)
target_link_libraries(test_support PRIVATE biingcore)
add_test(NAME support COMMAND test_support)
add_executable(test_reports tests/test_reports.c)
target_link_libraries(test_reports PRIVATE biingcore)
add_test(NAME reports COMMAND test_reports)
add_executable(test_sheet tests/test_sheet.c)
target_link_libraries(test_sheet PRIVATE biingcore)
add_test(NAME sheet COMMAND test_sheet)
add_executable(test_garage tests/test_garage.c)
target_link_libraries(test_garage PRIVATE biingcore)
add_test(NAME garage COMMAND test_garage)
add_executable(test_sound tests/test_sound.c)
target_link_libraries(test_sound PRIVATE biingcore)
add_test(NAME sound COMMAND test_sound)
add_executable(test_music tests/test_music.c)
target_link_libraries(test_music PRIVATE biingcore)
add_test(NAME music COMMAND test_music)
add_executable(test_midi tests/test_midi.c)
target_link_libraries(test_midi PRIVATE biingcore)
add_test(NAME midi COMMAND test_midi)
# The host output opened and closed, nothing audible sent (exit 77 = skipped: no output on this system or session).
add_executable(test_midi_host tests/test_midi_host.c src/midi_host.c)
target_include_directories(test_midi_host PRIVATE src)
if(APPLE)
  target_link_libraries(test_midi_host PRIVATE "-framework AudioToolbox" "-framework CoreMIDI" "-framework CoreFoundation")
elseif(WIN32)
  target_link_libraries(test_midi_host PRIVATE winmm)
  add_executable(test_midi_winmm tests/test_midi_winmm.c)
  target_include_directories(test_midi_winmm PRIVATE src)
  target_link_libraries(test_midi_winmm PRIVATE winmm)
  add_test(NAME midi_winmm COMMAND test_midi_winmm)
  set_tests_properties(midi_winmm PROPERTIES TIMEOUT 10)
endif()
add_test(NAME midi_host COMMAND test_midi_host)
set_tests_properties(midi_host PROPERTIES SKIP_RETURN_CODE 77)
add_executable(test_intro tests/test_intro.c)
target_link_libraries(test_intro PRIVATE biingcore)
add_test(NAME intro COMMAND test_intro)
add_executable(test_tutorial tests/test_tutorial.c)
target_link_libraries(test_tutorial PRIVATE biingcore)
add_test(NAME tutorial COMMAND test_tutorial)
add_executable(test_program tests/test_program.c)
target_link_libraries(test_program PRIVATE biingcore)
add_test(NAME program COMMAND test_program)
# The switch of the opt-in fixes (docs/fixes.md, section 1): the parser and, on the command line, the programs' start
# line, the error exit and the window build's options in any order (a missing game folder ends each run after them).
add_executable(test_fixes tests/test_fixes.c)
target_link_libraries(test_fixes PRIVATE biingcore)
add_test(NAME fixes COMMAND test_fixes)
add_test(NAME fixes_cli_error COMMAND biing /nonexistent-biing-dir)
add_test(NAME fixes_cli_off COMMAND biing /nonexistent-biing-dir --no-intro)
add_test(NAME fixes_cli_all COMMAND biing /nonexistent-biing-dir --no-intro --fixes)
add_test(NAME fixes_cli_order COMMAND biing /nonexistent-biing-dir --fixes --no-intro)
add_test(NAME fixes_cli_list COMMAND biing /nonexistent-biing-dir)
add_test(NAME fixes_cli_usage COMMAND biing /nonexistent-biing-dir --fixed)
add_test(NAME fixes_save_error COMMAND biing-save info /nonexistent-biing-save)
add_test(NAME fixes_save_quiet COMMAND biing-save info /nonexistent-biing-save)
add_test(NAME fixes_save_on COMMAND biing-save info /nonexistent-biing-save)
set_tests_properties(fixes_cli_error fixes_save_error PROPERTIES ENVIRONMENT "BIING_FIXES=bogus"
  PASS_REGULAR_EXPRESSION "\\[ERROR\\] BIING_FIXES: unknown fix 'bogus'")
set_tests_properties(fixes_cli_off PROPERTIES ENVIRONMENT "BIING_FIXES="
  PASS_REGULAR_EXPRESSION "\\[INFO\\] fixes of the original's bugs: off")
set_tests_properties(fixes_cli_all fixes_cli_order PROPERTIES ENVIRONMENT "BIING_FIXES="
  PASS_REGULAR_EXPRESSION "\\[INFO\\] fixes of the original's bugs: all \\(FIX-1 to FIX-32, FIX-47 to FIX-49, FIX-53, FIX-54\\)")
set_tests_properties(fixes_cli_list PROPERTIES ENVIRONMENT "BIING_FIXES=FIX-8,24"
  PASS_REGULAR_EXPRESSION "\\[INFO\\] fixes of the original's bugs: FIX-8, FIX-24\n")
set_tests_properties(fixes_cli_usage PROPERTIES ENVIRONMENT "BIING_FIXES=" PASS_REGULAR_EXPRESSION "usage:"
  FAIL_REGULAR_EXPRESSION "fixes of the original")
set_tests_properties(fixes_save_quiet PROPERTIES ENVIRONMENT "BIING_FIXES=off" PASS_REGULAR_EXPRESSION "\\[FAIL\\]"
  FAIL_REGULAR_EXPRESSION "fixes of the original")
set_tests_properties(fixes_save_on PROPERTIES ENVIRONMENT "BIING_FIXES=FIX-12"
  PASS_REGULAR_EXPRESSION "\\[INFO\\] fixes of the original's bugs: FIX-12\n")

# The engine's sound code on the command line: one sample or one song to a WAV file, a song's length.
add_executable(biing-sound src/soundtool.c)
target_link_libraries(biing-sound PRIVATE biingcore)
