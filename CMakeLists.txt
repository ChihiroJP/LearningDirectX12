cmake_minimum_required(VERSION 3.24)
project(DX12Tutorial12 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(DX12Tutorial12 WIN32
    src/main.cpp
    src/Win32Window.h
    src/Win32Window.cpp
    src/DxUtil.h
    src/DxUtil.cpp
    src/DxContext.h
    src/DxContext.cpp
)

target_compile_definitions(DX12Tutorial12 PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

if (MSVC)
    target_compile_options(DX12Tutorial12 PRIVATE /W4 /permissive-)
endif()

target_link_libraries(DX12Tutorial12 PRIVATE
    d3d12
    dxgi
    d3dcompiler
)

# Put output in build/bin/<config>/
set_target_properties(DX12Tutorial12 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_command(TARGET DX12Tutorial12 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:DX12Tutorial12>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/shaders" "$<TARGET_FILE_DIR:DX12Tutorial12>/shaders"
)