cmake_minimum_required(VERSION 3.24)
project(DX12Tutorial12 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.5
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
    tinyexr
    GIT_REPOSITORY https://github.com/syoyo/tinyexr.git
    GIT_TAG v1.0.12
)
# tinyexr's own CMakeLists builds a sample exe by default; disable it.
set(TINYEXR_BUILD_SAMPLE OFF CACHE BOOL "" FORCE)
set(TINYEXR_USE_MINIZ ON CACHE BOOL "" FORCE)
# Suppress deprecation warning from tinyexr's old cmake_minimum_required(VERSION 3.5)
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(tinyexr)
set(CMAKE_WARN_DEPRECATED ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG v2.9.3
)
FetchContent_MakeAvailable(tinygltf)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG master
)
FetchContent_MakeAvailable(imguizmo)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
    ${imguizmo_SOURCE_DIR}/ImGuizmo.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imguizmo_SOURCE_DIR}
)
if (MSVC)
    target_compile_options(imgui PRIVATE /W0)
endif()

add_executable(DX12Tutorial12 WIN32
    src/main.cpp
    src/Win32Window.h
    src/Win32Window.cpp
    src/DxUtil.h
    src/DxUtil.cpp
    src/HdriLoader.h
    src/HdriLoader.cpp
    src/Lighting.h
    src/Lighting.cpp
    src/ShadowMap.h
    src/ShadowMap.cpp
    src/Input.h
    src/Camera.h
    src/Camera.cpp
    src/ImGuiLayer.h
    src/ImGuiLayer.cpp
    src/DxContext.h
    src/DxContext.cpp
    src/MeshRenderer.h
    src/MeshRenderer.cpp
    src/GltfLoader.h
    src/GltfLoader.cpp
    src/AnimationPlayer.h
    src/AnimationPlayer.cpp
    src/particle.h
    src/particle.cpp
    src/particle_test.h
    src/particle_test.cpp
    src/ParticleRenderer.h
    src/ParticleRenderer.cpp
    src/RenderPass.h
    src/RenderPasses.h
    src/RenderPasses.cpp
    src/SkyRenderer.h
    src/SkyRenderer.cpp
    src/GridRenderer.h
    src/GridRenderer.cpp
    src/PostProcess.h
    src/PostProcess.cpp
    src/IBLGenerator.h
    src/IBLGenerator.cpp
    src/SSAORenderer.h
    src/SSAORenderer.cpp
    src/TerrainLOD.h
    src/TerrainLOD.cpp
    src/ProceduralMesh.h
    src/ProceduralMesh.cpp
    src/gridgame/GridGameState.h
    src/gridgame/GridGame.h
    src/gridgame/GridGame.cpp
    src/gridgame/GridMap.h
    src/gridgame/GridMap.cpp
    src/gridgame/StageData.h
    src/gridgame/StageData.cpp
    src/gridgame/StageSerializer.h
    src/gridgame/StageSerializer.cpp
    src/gridgame/GridEditorCommands.h
    src/engine/Entity.h
    src/engine/Entity.cpp
    src/engine/Scene.h
    src/engine/Scene.cpp
    src/engine/SceneEditor.h
    src/engine/SceneEditor.cpp
    src/engine/CommandHistory.h
    src/engine/CommandHistory.cpp
    src/engine/Commands.h
    src/engine/GizmoController.h
    src/engine/GizmoController.cpp
)

target_compile_definitions(DX12Tutorial12 PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

if (MSVC)
    target_compile_options(DX12Tutorial12 PRIVATE /W4 /permissive- /utf-8)
endif()

target_link_libraries(DX12Tutorial12 PRIVATE
    d3d12
    dxgi
    d3dcompiler
    dbghelp
    xinput
    imgui
    tinyexr
    tinygltf
    nlohmann_json::nlohmann_json
)

target_include_directories(DX12Tutorial12 PRIVATE
    ${tinyexr_SOURCE_DIR}
    ${tinyexr_SOURCE_DIR}/deps/miniz
    ${tinygltf_SOURCE_DIR}
)

# Put output in build/bin/<config>/
set_target_properties(DX12Tutorial12 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_command(TARGET DX12Tutorial12 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:DX12Tutorial12>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/shaders" "$<TARGET_FILE_DIR:DX12Tutorial12>/shaders"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:DX12Tutorial12>/Assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Assets" "$<TARGET_FILE_DIR:DX12Tutorial12>/Assets"
)