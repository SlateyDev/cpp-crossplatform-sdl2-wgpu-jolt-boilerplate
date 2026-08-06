cmake_minimum_required(VERSION 3.5)
project(aberrant_engine LANGUAGES C CXX)

# CMake 4 removed policy compatibility below 3.5; keep older subprojects configurable.
set(CMAKE_POLICY_VERSION_MINIMUM "3.5")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
endif()

include(FetchContent)

option(ABERRANT_USE_SYSTEM_DEPS "Use pre-installed webgpu/Jolt instead of FetchContent" OFF)

file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_executable(aberrant_engine
    ${SOURCE_FILES}
)

target_compile_features(aberrant_engine PRIVATE cxx_std_20)

if(ABERRANT_USE_SYSTEM_DEPS)
    find_package(glm CONFIG QUIET)
    find_package(RecastNavigation CONFIG QUIET)
endif()

if(NOT TARGET glm::glm)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 0.9.9.8
        GIT_SHALLOW TRUE
    )

    FetchContent_GetProperties(glm)
    if(NOT glm_POPULATED)
        FetchContent_Populate(glm)
    endif()

    add_library(glm_headers INTERFACE)
    add_library(glm::glm ALIAS glm_headers)
    target_include_directories(glm_headers INTERFACE ${glm_SOURCE_DIR})
endif()

if(ABERRANT_USE_SYSTEM_DEPS)
    find_path(CGLTF_INCLUDE_DIR cgltf.h)
endif()

if(NOT CGLTF_INCLUDE_DIR)
    FetchContent_Declare(
        cgltf
        GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
        GIT_TAG v1.14
        GIT_SHALLOW TRUE
    )

    FetchContent_GetProperties(cgltf)
    if(NOT cgltf_POPULATED)
        FetchContent_Populate(cgltf)
    endif()

    set(CGLTF_INCLUDE_DIR ${cgltf_SOURCE_DIR})
endif()

target_link_libraries(aberrant_engine PRIVATE glm::glm)
target_include_directories(aberrant_engine PRIVATE ${CGLTF_INCLUDE_DIR})

FetchContent_Declare(
    lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG v5.4.7
    GIT_SHALLOW TRUE
)

FetchContent_GetProperties(lua)
if(NOT lua_POPULATED)
    FetchContent_Populate(lua)
endif()

file(GLOB LUA_SOURCES CONFIGURE_DEPENDS ${lua_SOURCE_DIR}/*.c)
list(REMOVE_ITEM LUA_SOURCES
    ${lua_SOURCE_DIR}/lua.c
    ${lua_SOURCE_DIR}/luac.c
    ${lua_SOURCE_DIR}/onelua.c
)

add_library(lua_static STATIC ${LUA_SOURCES})
target_include_directories(lua_static PUBLIC ${lua_SOURCE_DIR})
if(UNIX AND NOT APPLE)
    target_compile_definitions(lua_static PRIVATE LUA_USE_LINUX)
elseif(APPLE)
    target_compile_definitions(lua_static PRIVATE LUA_USE_MACOSX)
endif()

FetchContent_Declare(
    wren
    GIT_REPOSITORY https://github.com/wren-lang/wren.git
    GIT_TAG 0.4.0
    GIT_SHALLOW TRUE
)

FetchContent_GetProperties(wren)
if(NOT wren_POPULATED)
    FetchContent_Populate(wren)
endif()

file(GLOB WREN_SOURCES CONFIGURE_DEPENDS ${wren_SOURCE_DIR}/src/vm/*.c ${wren_SOURCE_DIR}/src/optional/*.c)
add_library(wren_static STATIC ${WREN_SOURCES})
target_include_directories(wren_static
    PUBLIC ${wren_SOURCE_DIR}/src/include
    PRIVATE ${wren_SOURCE_DIR}/src/vm ${wren_SOURCE_DIR}/src/optional
)
target_compile_definitions(wren_static PUBLIC WREN_STATIC)

target_link_libraries(aberrant_engine PRIVATE lua_static wren_static)

set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
set(RMLUI_FONT_ENGINE "freetype" CACHE STRING "" FORCE)

set(FT_DISABLE_ZLIB TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI TRUE CACHE BOOL "" FORCE)
FetchContent_Declare(
    freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG VER-2-13-3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(freetype)
if(TARGET freetype AND NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()

FetchContent_Declare(
    RmlUi
    GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
    GIT_TAG 6.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(RmlUi)

target_link_libraries(aberrant_engine PRIVATE RmlUi::Core RmlUi::Debugger)

if(MSVC)
    foreach(target_name aberrant_engine lua_static wren_static rmlui_core rmlui_debugger freetype)
        if(TARGET ${target_name})
            set_property(TARGET ${target_name} PROPERTY
                MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        endif()
    endforeach()
    target_compile_options(aberrant_engine PRIVATE $<$<CONFIG:Debug>:/Zi /Od>)
    target_link_options(aberrant_engine PRIVATE $<$<CONFIG:Debug>:/DEBUG>)
endif()

if(EMSCRIPTEN)
    if(NOT ABERRANT_USE_SYSTEM_DEPS)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

        set(JPH_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(JPH_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
        set(JPH_BUILD_PERF_TESTS OFF CACHE BOOL "" FORCE)
        set(RECASTNAVIGATION_DEMO OFF CACHE BOOL "" FORCE)
        set(RECASTNAVIGATION_TESTS OFF CACHE BOOL "" FORCE)
        set(RECASTNAVIGATION_EXAMPLES OFF CACHE BOOL "" FORCE)

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

        FetchContent_Declare(
            RecastNavigation
            GIT_REPOSITORY https://github.com/recastnavigation/recastnavigation.git
            GIT_TAG v1.6.0
            GIT_SHALLOW TRUE
        )

        FetchContent_MakeAvailable(JoltPhysics WebGPU_Distribution webgpu_headers RecastNavigation)
        target_include_directories(aberrant_engine PRIVATE ${webgpu_headers_SOURCE_DIR})
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
        target_link_libraries(aberrant_engine PRIVATE Jolt)
    elseif(TARGET JoltPhysics)
        target_link_libraries(aberrant_engine PRIVATE JoltPhysics)
    else()
        message(FATAL_ERROR "Could not locate Jolt target (tried: Jolt, JoltPhysics).")
    endif()

    target_compile_definitions(aberrant_engine PRIVATE ABERRANT_WASM=1)
    target_compile_options(aberrant_engine PRIVATE
        "-sUSE_SDL=2"
        "-sUSE_SDL_IMAGE=2"
        "-sUSE_SDL_MIXER=2"
        "-sSDL2_IMAGE_FORMATS=['png']"
    )
    target_link_options(aberrant_engine PRIVATE
        "-sUSE_SDL=2"
        "-sUSE_SDL_IMAGE=2"
        "-sUSE_SDL_MIXER=2"
        "-sSDL2_IMAGE_FORMATS=['png']"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sASYNCIFY"
        "-sSTACK_SIZE=1048576"
        "--preload-file"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets"
    )
    target_link_libraries(aberrant_engine PRIVATE ${WGPU_TARGET})
    set_target_properties(aberrant_engine PROPERTIES SUFFIX ".html")
else()
    if(ABERRANT_USE_SYSTEM_DEPS)
        find_package(webgpu CONFIG QUIET)
    endif()

    if(NOT ABERRANT_USE_SYSTEM_DEPS)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST OFF CACHE BOOL "" FORCE)

        set(JPH_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(JPH_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
        set(JPH_BUILD_PERF_TESTS OFF CACHE BOOL "" FORCE)
        set(RECASTNAVIGATION_DEMO OFF CACHE BOOL "" FORCE)
        set(RECASTNAVIGATION_TESTS OFF CACHE BOOL "" FORCE)
        set(RECASTNAVIGATION_EXAMPLES OFF CACHE BOOL "" FORCE)

        FetchContent_Declare(
            SDL2
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG release-2.30.7
            GIT_SHALLOW TRUE
        )

        set(SDL2IMAGE_VENDORED ON CACHE BOOL "" FORCE)
        set(SDL2IMAGE_SAMPLES OFF CACHE BOOL "" FORCE)

        FetchContent_Declare(
            SDL2_image
            GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
            GIT_TAG release-2.8.2
            GIT_SHALLOW TRUE
        )

        set(SDL2MIXER_VENDORED ON CACHE BOOL "" FORCE)
        set(SDL2MIXER_SAMPLES OFF CACHE BOOL "" FORCE)

        FetchContent_Declare(
            SDL2_mixer
            GIT_REPOSITORY https://github.com/libsdl-org/SDL_mixer.git
            GIT_TAG release-2.8.0
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

        FetchContent_Declare(
            RecastNavigation
            GIT_REPOSITORY https://github.com/recastnavigation/recastnavigation.git
            GIT_TAG v1.6.0
            GIT_SHALLOW TRUE
        )

        set(WEBGPU_BACKEND "WGPU" CACHE STRING "" FORCE)
        set(WEBGPU_BUILD_FROM_SOURCE OFF CACHE BOOL "" FORCE)
        set(WEBGPU_LINK_TYPE "SHARED" CACHE STRING "" FORCE)

        FetchContent_MakeAvailable(SDL2)
        FetchContent_MakeAvailable(SDL2_image SDL2_mixer JoltPhysics WebGPU_Distribution RecastNavigation)
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
        target_link_libraries(aberrant_engine PRIVATE Jolt)
    elseif(TARGET JoltPhysics)
        target_link_libraries(aberrant_engine PRIVATE JoltPhysics)
    else()
        message(FATAL_ERROR "Could not locate Jolt target (tried: Jolt, JoltPhysics).")
    endif()

    target_link_libraries(aberrant_engine PRIVATE ${WGPU_TARGET})
    if(TARGET SDL2-static)
        target_link_libraries(aberrant_engine PRIVATE SDL2-static)
    elseif(TARGET SDL2::SDL2)
        target_link_libraries(aberrant_engine PRIVATE SDL2::SDL2)
    else()
        message(FATAL_ERROR "Could not locate SDL2 target (tried: SDL2-static, SDL2::SDL2).")
    endif()

    if(TARGET SDL2_image::SDL2_image-static)
        target_link_libraries(aberrant_engine PRIVATE SDL2_image::SDL2_image-static)
    elseif(TARGET SDL2_image::SDL2_image)
        target_link_libraries(aberrant_engine PRIVATE SDL2_image::SDL2_image)
    else()
        message(FATAL_ERROR "Could not locate SDL2_image target (tried: SDL2_image::SDL2_image-static, SDL2_image::SDL2_image).")
    endif()

    if(TARGET SDL2_mixer::SDL2_mixer-static)
        target_link_libraries(aberrant_engine PRIVATE SDL2_mixer::SDL2_mixer-static)
    elseif(TARGET SDL2_mixer::SDL2_mixer)
        target_link_libraries(aberrant_engine PRIVATE SDL2_mixer::SDL2_mixer)
    else()
        message(FATAL_ERROR "Could not locate SDL2_mixer target (tried: SDL2_mixer::SDL2_mixer-static, SDL2_mixer::SDL2_mixer).")
    endif()

    if(COMMAND target_copy_webgpu_binaries)
        target_copy_webgpu_binaries(aberrant_engine)
    endif()
endif()

if(TARGET Recast AND TARGET Detour)
    target_link_libraries(aberrant_engine PRIVATE Recast Detour)
else()
    message(FATAL_ERROR "Could not locate Recast/Detour targets (expected: Recast and Detour).")
endif()
