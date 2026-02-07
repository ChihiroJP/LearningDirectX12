# Why tinygltf over Assimp?

## 1. Portfolio Value & Educational Benefit
- **Raw Structure Understanding**: `tinygltf` provides a 1:1 mapping of the glTF JSON structure. This forces the programmer to manually map "Buffer Views" and "Accessors" to DX12 functions like `CreateCommittedResource` and input layout descriptions.
- **"Engine Architect" vs "User"**: Using Assimp is more like being a *user* of a library; it sanitizes data into a generic format. Using `tinygltf` makes you an *architect* who understands how GPU data is actually serialized and deserialized.

## 2. Modern Standards
- **glTF is the "JPEG of 3D"**: It is the industry standard for transmission 3D assets. It is designed specifically to be mapped directly to graphics APIs (Vulkan/DirectX 12/WebGPU) with minimal processing.
- **Direct GPU Mapping**: glTF's buffer model matches modern API concepts better than Assimp's generic post-processed mesh structures.

## 3. Build & Integration Simplicity
- **Header-Only**: `tinygltf` is a single-header library (stb-style). It integrates seamlessly into `CMake` with `FetchContent` or just by copying a file.
- **Compilation Speed**: Assimp is a massive library that significantly increases build times and binary size. For a learning project where iteration speed is key, `tinygltf` is superior.

## 4. Performance
- **Minimal Overhead**: `tinygltf` does very little processing. It parses JSON and gives you the pointers. Assimp performs expensive post-processing steps (tangent generation, triangulation, scene graph flattening) which can be slow and opaque.

## Summary
For a custom DX12 engine portfolio project, `tinygltf` demonstrates a deeper understanding of the graphics pipeline, while Assimp is better reserved for generic tools that need to support legacy formats (FBX, OBJ, etc.).
