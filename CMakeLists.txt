cmake_minimum_required(VERSION 3.10)
project(dramsim3 CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

set(default_build_type "Release")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
  set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE
      STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

add_library(inih INTERFACE)
target_include_directories(inih INTERFACE ext/headers)

find_package(fmt)
if(NOT fmt_FOUND)
    message(STATUS "External fmt package not found, using internal one")
    add_library(fmt INTERFACE)
    target_include_directories(fmt INTERFACE ext/fmt/include)
    target_compile_definitions(fmt INTERFACE FMT_HEADER_ONLY=1)
    add_library(fmt::fmt-header-only ALIAS fmt)
endif(NOT fmt_FOUND)

# argparsing library, only used in main program not the library
add_library(args INTERFACE)
target_include_directories(args INTERFACE ext/headers)

add_library(json INTERFACE)
target_include_directories(json INTERFACE ext/headers)

# Main DRAMSim Lib
add_library(dramsim3
    src/bankstate.cc
    src/channel_state.cc
    src/command_queue.cc
    src/common.cc
    src/configuration.cc
    src/controller.cc
    src/dram_system.cc
    src/hmc.cc
    src/refresh.cc
    src/simple_stats.cc
    src/timing.cc
    src/memory_system.cc
)

if (THERMAL)
    # dependency check
    # sudo apt-get install libatlas-base-dev on ubuntu
    find_package(BLAS REQUIRED)
    find_package(OpenMP REQUIRED)
    # YOU need to build superlu on your own. Do the following:
    # git submodule update --init
    # cd ext/SuperLU_MT_3.1 && make lib
    find_library(SUPERLU
        NAME superlu_mt_OPENMP libsuperlu_mt_OPENMP
        HINTS ${PROJECT_SOURCE_DIR}/ext/SuperLU_MT_3.1/lib/
    )

    target_link_libraries(dramsim3
        PRIVATE ${SUPERLU} f77blas atlas m ${OpenMP_C_FLAGS}
    )
    target_sources(dramsim3
        PRIVATE src/thermal.cc src/sp_ienv.c src/thermal_solver.c
    )
    target_compile_options(dramsim3 PRIVATE -DTHERMAL -D_LONGINT -DAdd_ ${OpenMP_C_FLAGS})

    add_executable(thermalreplay src/thermal_replay.cc)
    target_link_libraries(thermalreplay dramsim3 inih)
    target_compile_options(thermalreplay PRIVATE -DTHERMAL -D_LONGINT -DAdd_ ${OpenMP_C_FLAGS})
endif (THERMAL)

if (CMD_TRACE)
    target_compile_options(dramsim3 PRIVATE -DCMD_TRACE)
endif (CMD_TRACE)

if (ADDR_TRACE)
    target_compile_options(dramsim3 PRIVATE -DADDR_TRACE)
endif (ADDR_TRACE)


target_include_directories(dramsim3 INTERFACE src)
target_compile_options(dramsim3 PRIVATE -Wall)
target_link_libraries(dramsim3 PRIVATE inih fmt::fmt-header-only)
set_target_properties(dramsim3 PROPERTIES
    CXX_EXTENSIONS NO
)

# trace CPU, .etc
add_executable(dramsim3main src/main.cc src/cpu.cc)
target_link_libraries(dramsim3main PRIVATE dramsim3 args)
target_compile_options(dramsim3main PRIVATE)
set_target_properties(dramsim3main PROPERTIES
    CXX_EXTENSIONS NO
)

# Unit testing
find_package(Catch2 3 REQUIRED)

add_executable(dramsim3test EXCLUDE_FROM_ALL
    tests/test_config.cc
    tests/test_dramsys.cc
    tests/test_hmcsys.cc # IDK somehow this can literally crush your computer
)
target_link_libraries(dramsim3test Catch2::Catch2WithMain dramsim3)
target_include_directories(dramsim3test PRIVATE src/)

# We have to use this custome command because there's a bug in cmake
# that if you do `make test` it doesn't build your updated test files
# so we're stucking with `make dramsim3test` for now
add_custom_command(
    TARGET dramsim3test POST_BUILD
    COMMAND dramsim3test
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    DEPENDS dramsim3test dramsim3
)
