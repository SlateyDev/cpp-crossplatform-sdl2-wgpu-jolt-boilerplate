cmake_minimum_required(VERSION 3.24)
project(wgpu_jolt_starter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

option(WGPU_JOLT_USE_SYSTEM_DEPS "Use pre-installed webgpu/Jolt instead of FetchContent" OFF)

add_executable(wgpu_jolt_app
    src/main.cpp
)

target_compile_features(wgpu_jolt_app PRIVATE cxx_std_20)

if(MSVC)
    set_property(TARGET wgpu_jolt_app PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(EMSCRIPTEN)
    if(NOT WGPU_JOLT_USE_SYSTEM_DEPS)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

        set(JPH_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(JPH_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
        set(JPH_BUILD_PERF_TESTS OFF CACHE BOOL "" FORCE)

        FetchContent_Declare(
            WebGPU_Distribution
            GIT_REPOSITORY https://github.com/eliemichel/WebGPU-distribution.git
            GIT_TAG main
            GIT_SHALLOW TRUE
        )

        FetchContent_Declare(
            webgpu_headers
            GIT_REPOSITORY https://github.com/webgpu-native/webgpu-headers.git
            GIT_TAG main
            GIT_SHALLOW TRUE
        )

        set(WEBGPU_BACKEND "EMDAWNWEBGPU" CACHE STRING "" FORCE)

        FetchContent_Declare(
            JoltPhysics
            GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
            GIT_TAG master
            GIT_SHALLOW TRUE
            SOURCE_SUBDIR Build
        )

        FetchContent_MakeAvailable(JoltPhysics WebGPU_Distribution webgpu_headers)
        target_include_directories(wgpu_jolt_app PRIVATE ${webgpu_headers_SOURCE_DIR})
    endif()

    set(WGPU_TARGET "")
    if(TARGET wgpu_native)
        set(WGPU_TARGET wgpu_native)
    elseif(TARGET webgpu)
        set(WGPU_TARGET webgpu)
    elseif(TARGET wgpu)
        set(WGPU_TARGET wgpu)
    endif()

    if(WGPU_TARGET STREQUAL "")
        message(FATAL_ERROR "Could not locate a WebGPU target (tried: wgpu_native, webgpu, wgpu).")
    endif()

    if(TARGET Jolt)
        target_link_libraries(wgpu_jolt_app PRIVATE Jolt)
    elseif(TARGET JoltPhysics)
        target_link_libraries(wgpu_jolt_app PRIVATE JoltPhysics)
    else()
        message(FATAL_ERROR "Could not locate Jolt target (tried: Jolt, JoltPhysics).")
    endif()

    target_compile_definitions(wgpu_jolt_app PRIVATE WGPU_JOLT_WASM=1)
    target_compile_options(wgpu_jolt_app PRIVATE
        "-sUSE_SDL=2"
    )
    target_link_options(wgpu_jolt_app PRIVATE
        "-sUSE_SDL=2"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sASYNCIFY"
    )
    target_link_libraries(wgpu_jolt_app PRIVATE ${WGPU_TARGET})
    set_target_properties(wgpu_jolt_app PROPERTIES SUFFIX ".html")
else()
    if(WGPU_JOLT_USE_SYSTEM_DEPS)
        find_package(webgpu CONFIG QUIET)
    endif()

    if(NOT WGPU_JOLT_USE_SYSTEM_DEPS)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST OFF CACHE BOOL "" FORCE)

        set(JPH_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(JPH_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
        set(JPH_BUILD_PERF_TESTS OFF CACHE BOOL "" FORCE)

        FetchContent_Declare(
            SDL2
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG release-2.30.7
            GIT_SHALLOW TRUE
        )

        FetchContent_Declare(
            JoltPhysics
            GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
            GIT_TAG master
            GIT_SHALLOW TRUE
            SOURCE_SUBDIR Build
        )

        FetchContent_Declare(
            WebGPU_Distribution
            GIT_REPOSITORY https://github.com/eliemichel/WebGPU-distribution.git
            GIT_TAG main
            GIT_SHALLOW TRUE
        )

        set(WEBGPU_BACKEND "WGPU" CACHE STRING "" FORCE)
        set(WEBGPU_BUILD_FROM_SOURCE OFF CACHE BOOL "" FORCE)
        set(WEBGPU_LINK_TYPE "SHARED" CACHE STRING "" FORCE)

        FetchContent_MakeAvailable(SDL2 JoltPhysics WebGPU_Distribution)
    endif()

    set(WGPU_TARGET "")
    if(TARGET wgpu_native)
        set(WGPU_TARGET wgpu_native)
    elseif(TARGET webgpu)
        set(WGPU_TARGET webgpu)
    elseif(TARGET wgpu)
        set(WGPU_TARGET wgpu)
    endif()

    if(WGPU_TARGET STREQUAL "")
        message(FATAL_ERROR "Could not locate a WebGPU target (tried: wgpu_native, webgpu, wgpu).")
    endif()

    if(TARGET Jolt)
        target_link_libraries(wgpu_jolt_app PRIVATE Jolt)
    elseif(TARGET JoltPhysics)
        target_link_libraries(wgpu_jolt_app PRIVATE JoltPhysics)
    else()
        message(FATAL_ERROR "Could not locate Jolt target (tried: Jolt, JoltPhysics).")
    endif()

    target_link_libraries(wgpu_jolt_app PRIVATE ${WGPU_TARGET})
    if(TARGET SDL2-static)
        target_link_libraries(wgpu_jolt_app PRIVATE SDL2-static)
    elseif(TARGET SDL2::SDL2)
        target_link_libraries(wgpu_jolt_app PRIVATE SDL2::SDL2)
    else()
        message(FATAL_ERROR "Could not locate SDL2 target (tried: SDL2-static, SDL2::SDL2).")
    endif()

    if(COMMAND target_copy_webgpu_binaries)
        target_copy_webgpu_binaries(wgpu_jolt_app)
    endif()
endif()
