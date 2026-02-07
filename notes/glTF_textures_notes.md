## Asset Pipeline v2 (Phase 5.5) Notes — glTF Textures

This note documents **Phase 5.5**: taking the glTF model we already load (Phase 5) and wiring up the **correct baseColor texture** so the mesh shows real albedo instead of a purple normal map.

---

### Purpose (why this phase exists)
Phase 5 got geometry on screen, but a real asset pipeline needs materials and textures.

The key learning goals here:
- understand glTF’s **material → texture → image** indirection
- create an SRV that matches the **color space** (sRGB for baseColor)
- bind the texture SRV correctly at draw time

---

## What changed

### 1) Selecting the correct texture image (baseColor)
Previously we did “first image in the file” (`model.images[0]`), which often ends up being the **normal map** (purple).

Now we resolve baseColor like this:
- `primitive.material`
- → `model.materials[mat].pbrMetallicRoughness.baseColorTexture.index`
- → `model.textures[tex].source`
- → `model.images[image]`

Code (excerpt) — `src/GltfLoader.cpp`:

```cpp
  // Phase 5.5: Extract the material's baseColor texture (albedo) image.
  // This avoids accidentally using the normal map (purple) as the "diffuse".
  m_baseColorImage = {};
  int imageIndex = -1;

  if (prim.material >= 0 && prim.material < static_cast<int>(model.materials.size())) {
    const auto &mat = model.materials[prim.material];
    const int texIndex = mat.pbrMetallicRoughness.baseColorTexture.index;
    if (texIndex >= 0 && texIndex < static_cast<int>(model.textures.size())) {
      const int srcImg = model.textures[texIndex].source;
      if (srcImg >= 0 && srcImg < static_cast<int>(model.images.size()))
        imageIndex = srcImg;
    }
  }
```

### 2) Converting to RGBA on CPU
glTF images can be RGB, RGBA, or grayscale. For simplicity, we always convert to **RGBA8** for upload.

Code (excerpt) — `src/GltfLoader.cpp`:

```cpp
static LoadedImage ConvertToRgba(const tinygltf::Image &img) {
  LoadedImage out;
  out.width = img.width;
  out.height = img.height;
  out.channels = 4;
  // ... handle 4/3/2/1 channel inputs ...
  return out;
}
```

---

## 3) GPU texture format + SRGB correctness

BaseColor textures are authored in **sRGB**. In D3D12, the best practice is:
- create the resource as **TYPELESS**
- create the SRV as **UNORM_SRGB**

Code (excerpt) — `src/MeshRenderer.cpp`:

```cpp
// CreateTextureForMesh(...)
tex.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
// ...
srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
```

Why typeless matters: without it, creating an SRGB view can fail validation on some drivers.

---

## 4) Binding the SRV at draw time

`mesh.hlsl` samples `t0`, so we must bind the SRV descriptor table (root param 1) before drawing.

Code (excerpt) — `src/MeshRenderer.cpp`:

```cpp
// DrawMesh(...)
// Root param 0 = root CBV (b0), root param 1 = SRV table (t0).
cmd->SetGraphicsRootConstantBufferView(0, cbGpu);
if (mesh.baseColorSrvGpu.ptr != 0)
  cmd->SetGraphicsRootDescriptorTable(1, mesh.baseColorSrvGpu);
```

---

## 5) Display gamma (temporary)

When sampling an SRGB texture, the shader receives **linear** values. Since our current renderer writes directly to an UNORM backbuffer (no full tonemap pipeline yet), we gamma-encode the output temporarily:

Code (excerpt) — `shaders/mesh.hlsl`:

```hlsl
// Gamma encode for UNORM backbuffer.
color = pow(max(color, 0.0f), 1.0f / 2.2f);
return float4(color, 1.0f);
```

Later (Phase 6/9), we’ll replace this with a proper linear workflow + tonemapping.

---

## Issues we met + solutions

### Issue: “Model is purple”
- **Cause**: we were sampling the normal map as if it were the baseColor texture.
- **Fix**: resolve baseColor through glTF material → texture → image.

### Issue: “Texture colors look wrong / too dark”
- **Cause**: sRGB/linear mismatch.
- **Fix**: SRV as `UNORM_SRGB` + temporary gamma encode on output.

