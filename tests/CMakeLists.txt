# ── Catch2 (fetched at configure time) ───────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

add_executable(flux_tests
    test_lexer.cpp
    test_parser.cpp
    test_typechecker.cpp
    test_passes.cpp
    test_backends.cpp
)
target_link_libraries(flux_tests PRIVATE flux_core Catch2::Catch2WithMain)
if(MINGW)
    target_link_options(flux_tests PRIVATE -static)
endif()

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
catch_discover_tests(flux_tests)

# ── SPIR-V validation of every example (needs spirv-val from SPIRV-Tools) ─────
find_program(SPIRV_VAL spirv-val)
if(SPIRV_VAL)
    file(GLOB FLUX_EXAMPLES ${CMAKE_SOURCE_DIR}/examples/*.flux ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.flux)
    foreach(example ${FLUX_EXAMPLES})
        get_filename_component(name ${example} NAME_WE)
        foreach(opt O2 O0)
            set(flag "")
            if(opt STREQUAL "O0")
                set(flag "-O0")
            endif()
            add_test(NAME spirv-val/${name}/${opt}
                     COMMAND ${CMAKE_COMMAND}
                        -DFLUXC=$<TARGET_FILE:fluxc> -DSPIRV_VAL=${SPIRV_VAL}
                        -DINPUT=${example} -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${name}_${opt}.spv
                        -DFLAG=${flag}
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/validate_spirv.cmake)
        endforeach()
    endforeach()
else()
    message(STATUS "spirv-val not found: skipping SPIR-V validation tests")
endif()
