cmake_minimum_required(VERSION 3.22.1)
project(turmite_android C)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
get_filename_component(TURMITE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
set(CORE "${TURMITE_ROOT}/src")

# Compile the repository sources directly: no Android copy of rules or interpreter.
add_library(turmite_host STATIC engine.c
    ${CORE}/ant.c ${CORE}/rules.c ${CORE}/rng.c
    ${CORE}/world.c ${CORE}/scheduler.c ${CORE}/renderer.c)
set_target_properties(turmite_host PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(turmite_host PUBLIC "${CORE}" "${CMAKE_CURRENT_LIST_DIR}")
target_compile_definitions(turmite_host PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(turmite_host PRIVATE -Wall -Wextra -Wpedantic)

if(ANDROID)
    add_library(turmite_android SHARED bridge.c)
    target_link_libraries(turmite_android PRIVATE turmite_host m atomic)

    # Release has no DWARF or static symbol table, even before AGP packaging.
    # JNIEXPORT keeps the three bridge functions visible to Android's loader.
    # CMake 3.22's Android IPO probe selects the removed gold linker and can
    # lose the configured ABI in its nested build. Use the pinned NDK's Clang
    # ThinLTO/lld directly, only for Release, without CMake's IPO machinery.
    foreach(target turmite_host turmite_android)
        target_compile_options(${target} PRIVATE
            "$<$<CONFIG:Release>:-O3;-g0;-flto=thin;-ffunction-sections;-fdata-sections;-fvisibility=hidden>")
        target_compile_definitions(${target} PRIVATE $<$<CONFIG:Release>:NDEBUG>)
    endforeach()
    target_link_options(turmite_android PRIVATE
        "$<$<CONFIG:Release>:-flto=thin;-fuse-ld=lld;-Wl,--gc-sections;-Wl,--strip-all>")
else()
    # Exercise the same native host without an Android SDK or JNI headers.
    find_package(Threads REQUIRED)
    target_link_libraries(turmite_host PUBLIC Threads::Threads m)
    enable_testing()
    add_executable(android_host_test "${TURMITE_ROOT}/android/tests/host_test.c")
    target_compile_definitions(android_host_test PRIVATE _POSIX_C_SOURCE=200809L)
    target_link_libraries(android_host_test PRIVATE turmite_host)
    add_test(NAME android_host_lifecycle COMMAND android_host_test)
endif()
