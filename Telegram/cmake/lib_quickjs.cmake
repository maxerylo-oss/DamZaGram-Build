# QuickJS-ng: the JavaScript engine behind DamZaGram plugins.
add_library(damza_quickjs STATIC)
set_target_properties(damza_quickjs PROPERTIES FOLDER "(damza)")

set(quickjs_loc ${third_party_loc}/quickjs-ng)

target_sources(damza_quickjs
PRIVATE
    ${quickjs_loc}/dtoa.c
    ${quickjs_loc}/libregexp.c
    ${quickjs_loc}/libunicode.c
    ${quickjs_loc}/quickjs.c
)
target_include_directories(damza_quickjs SYSTEM
PUBLIC
    ${quickjs_loc}
)
target_compile_definitions(damza_quickjs
PRIVATE
    _GNU_SOURCE
)
set_target_properties(damza_quickjs PROPERTIES C_STANDARD 11)
if (MSVC)
    target_compile_definitions(damza_quickjs PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
    # /O2 can't be combined with the /RTC1 of Debug configurations.
    target_compile_options(damza_quickjs PRIVATE /W0 $<$<NOT:$<CONFIG:Debug>>:/O2>)
else()
    # The engine is slow at -O0, keep it optimized even in Debug.
    target_compile_options(damza_quickjs PRIVATE -w -O2)
    target_link_libraries(damza_quickjs PUBLIC m ${CMAKE_DL_LIBS})
endif()
