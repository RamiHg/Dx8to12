cmake_minimum_required(VERSION 3.11...3.26)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(DX8TO12_USE_ALLOCATOR OFF)
set(DX8TO12_ENABLE_VALIDATION ON)

project(
  Dx8to12
  VERSION 0.1
  LANGUAGES CXX)

add_library(d3d8 SHARED)
set_property(TARGET d3d8 PROPERTY CXX_STANDARD 20)
set_property(TARGET d3d8 PROPERTY CXX_STANDARD_REQUIRED ON)

add_subdirectory(src)

target_link_libraries(d3d8 PUBLIC DXGI.lib D3D12.lib D3DCompiler.lib dxguid.lib)
target_include_directories(d3d8 PUBLIC src src/DirectX8 third_party)
target_compile_definitions(
  d3d8 PRIVATE WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS NOMINMAX
               CURRENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
if(DX8TO12_USE_ALLOCATOR)
  target_compile_definitions(d3d8
                             PRIVATE USE_ALLOCATOR="${DX8TO12_USE_ALLOCATOR}")
endif()
if(DX8TO12_ENABLE_VALIDATION)
  target_compile_definitions(d3d8 PRIVATE DX8TO12_ENABLE_VALIDATION)
endif()

# target_compile_options(d3d8 PRIVATE "$<$<CONFIG:Debug>:/Ox>")

target_precompile_headers(d3d8 PRIVATE third_party/aixlog.hpp <windows.h>
                          <d3d12.h>)

if(MSVC)
  target_compile_options(d3d8 PRIVATE /W4 /wd4100 /wd4505)
  target_compile_options(d3d8 PRIVATE /MP)
  target_link_options(d3d8 PRIVATE /LARGEADDRESSAWARE)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(d3d8 PRIVATE -Wno-unused-parameter)
  target_compile_options(
    d3d8
    PRIVATE -Wall
            -Wno-c++98-compat
            -Wno-reserved-macro-identifier
            -Wno-reserved-identifier
            -Wno-gnu-anonymous-struct
            -Wno-c++98-compat-pedantic
            -Wno-nested-anon-types
            -Wno-extra-semi
            -Wno-newline-eof
            -Wno-signed-enum-bitfield
            -Wno-language-extension-token
            -Wno-switch-enum
            -Wno-undefined-reinterpret-cast
            -Wno-old-style-cast
            # Maybe someday I'll fix sign conversion.
            -Wno-sign-conversion
            -Werror)
endif()

# target_compile_options(d3d8 PRIVATE /fsanitize=address)

if(DX8TO12_USE_ALLOCATOR)
  include(FetchContent)
  include(CMakePrintHelpers)

  FetchContent_Declare(
    D3D12MemoryAllocator
    GIT_REPOSITORY
      https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator
    GIT_TAG 554cd184af2a85dfe10eba8c30bdca28e22dd12a)
  FetchContent_MakeAvailable(D3D12MemoryAllocator)
  get_target_property(D3D12MemoryAllocator_INCLUDES D3D12MemoryAllocator
                      INCLUDE_DIRECTORIES)

  target_link_libraries(d3d8 PUBLIC D3D12MemoryAllocator)
  target_include_directories(d3d8 PRIVATE "${D3D12MemoryAllocator_INCLUDES}")
endif()

