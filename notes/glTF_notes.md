# Asset Pipeline v1 (Phase 5) Notes

This note documents the **glTF 2.0 loading** step (Phase 5).

## Purpose (what and why)
Previously, we only rendered hardcoded geometry (a cube). To build a real scene, we need to load external 3D models.
We chose **glTF 2.0** because it is the modern standard for 3D transmission, designed to map directly to graphics APIs (Vulkan/DX12).
We used **`tinygltf`** (a header-only library) instead of `Assimp` to keep the build light and to force us to understand the raw buffer structure.

---

## Files added/updated in this step
- `CMakeLists.txt` (added `tinygltf` dependency)
- `src/GltfLoader.h` / `.cpp` (new loader class)
- `shaders/mesh.hlsl` (new shader for imported meshes)
- `src/DxContext.h` / `.cpp` (added `CreateMeshResources` and `DrawMesh`)
- `src/main.cpp` (load model on startup and draw it)

---

## 1) Library Integration (`CMakeLists.txt`)

### Code (excerpt)
```cmake
FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG v2.9.3
)
FetchContent_MakeAvailable(tinygltf)
target_link_libraries(DX12Tutorial12 PRIVATE tinygltf)
```

### Explanation
- We use CMake's `FetchContent` to download `tinygltf` automatically.
- It is header-only, but often includes `stb_image` for texture loading, so we need to ensure definitions are handled in one implementation file (`GltfLoader.cpp`).

---

## 2) The Loader (`src/GltfLoader.cpp`)

### Purpose
Parse the JSON structure of a `.gltf` file and extract vertex data (Position, Normal, UV) and indices into a format our engine can use.

### Code (excerpt: Accessing Buffers)
```cpp
// Get accessor for POSITION
const int posIdx = prim.attributes.at("POSITION");
const auto& posAcc = model.accessors[posIdx];
const auto& posBufView = model.bufferViews[posAcc.bufferView];
const auto& posBuf = model.buffers[posBufView.buffer];

// Raw pointer to data
const float* posData = reinterpret_cast<const float*>(
    &posBuf.data[posBufView.byteOffset + posAcc.byteOffset]
);
```

### Explanation
- **glTF structure**: `Buffer` (raw bytes) -> `BufferView` (subsection) -> `Accessor` (type/count/stride).
- We manually map these raw pointers and "interleave" them into our single `MeshVertex` struct:
```cpp
struct MeshVertex {
    float pos[3];
    float normal[3];
    float uv[2];
};
```

---

## 3) GPU Resources (`src/DxContext.cpp`)

### Purpose
Take the CPU vectors (`std::vector<MeshVertex>`) and upload them to DX12 buffers (`ID3D12Resource`).

### Code (excerpt: Upload Heap)
```cpp
D3D12_HEAP_PROPERTIES uploadHeap{};
uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD; // CPU-write, GPU-read

m_device->CreateCommittedResource(..., &m_meshVB);

void* mapped = nullptr;
m_meshVB->Map(0, nullptr, &mapped);
memcpy(mapped, mesh.vertices.data(), vbSize);
m_meshVB->Unmap(0, nullptr);
```

### Explanation
- For this phase, we use `UPLOAD` heaps for simplicity. This lets us write directly from CPU.
- **Optimization note**: real engines use `DEFAULT` heaps (GPU-only) and copy data via a temporary upload buffer. We kept it simple for now.

---

## 4) Rendering (`shaders/mesh.hlsl`)

### Purpose
A dedicated shader for meshes. It currently visualizes **Normals** as color because we don't have texture support hooked up yet.

### Code (excerpt)
```hlsl
float4 PSMain(PSIn i) : SV_TARGET
{
    // Visualize Normal (remapped from -1..1 to 0..1)
    float3 n = normalize(i.normal);
    return float4(n * 0.5f + 0.5f, 1.0f);
}
```

---

## Issues we met + solutions

### Issue 1: "Camera spinning like a tornado"
- **Symptom**: Holding RMB to look around caused extreme wild spinning.
- **Cause**: Initially suspected code sensitivity, but user later confirmed it was **Remote Desktop** injecting weird mouse deltas.
- **Solution**: We drastically reduced sensitivity (`0.0025f` -> `0.00005f`) temporarily, but the real fix is running locally.

### Issue 2: "C4430: Missing type specifier (tinygltf)"
- **Symptom**: Compilation errors saying `tiny_gltf.h` not found or types missing.
- **Cause**: Forgot to include the header directory in CMake or mixed up include order.
- **Solution**: Added `${tinygltf_SOURCE_DIR}` to `target_include_directories`.

### Issue 3: Linker/Compiler errors in `DxContext`
- **Symptom**: `CompileShader` identifier not found inside `DxContext.cpp`.
- **Cause**: The helper function was defined `static` but placed *after* the function calling it. C++ requires declaration before use.
- **Solution**: Added forward declarations at the top of the file:
  ```cpp
  static ComPtr<ID3DBlob> CompileShader(...);
  ```

---

## What this enables next
- **Textures**: We are parsing UVs, but not using them. Next step is loading the texture images (which `tinygltf` already grabbed for us) and binding them.
- **Lighting**: We have Normals. We can now implement Directional Light (Phong/PBR).
