#include "GltfLoader.h"
#include <iostream>
#include <stdexcept>

// IMPORTANT:
// We link against the `tinygltf` library via CMake. Do NOT compile the
// implementation in this translation unit (no TINYGLTF_IMPLEMENTATION / STB
// implementation macros), otherwise we risk ODR issues / duplicate
// implementations and hard-to-debug crashes.
#include "tiny_gltf.h"

static bool GetAccessorFloatData(const tinygltf::Model &model,
                                 const tinygltf::Accessor &acc,
                                 int expectedType, // e.g. TINYGLTF_TYPE_VEC3
                                 const float *&outData, size_t &outStrideBytes) {
  outData = nullptr;
  outStrideBytes = 0;

  if (acc.bufferView < 0 ||
      acc.bufferView >= static_cast<int>(model.bufferViews.size()))
    return false;
  if (acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
    return false;
  if (acc.type != expectedType)
    return false;
  if (acc.count == 0)
    return false;

  const auto &bv = model.bufferViews[acc.bufferView];
  if (bv.buffer < 0 || bv.buffer >= static_cast<int>(model.buffers.size()))
    return false;
  const auto &buf = model.buffers[bv.buffer];

  // Element sizes (float).
  size_t elemBytes = 0;
  if (expectedType == TINYGLTF_TYPE_VEC2)
    elemBytes = sizeof(float) * 2;
  else if (expectedType == TINYGLTF_TYPE_VEC3)
    elemBytes = sizeof(float) * 3;
  else if (expectedType == TINYGLTF_TYPE_VEC4)
    elemBytes = sizeof(float) * 4;
  else
    return false;

  const size_t stride =
      bv.byteStride ? static_cast<size_t>(bv.byteStride) : elemBytes;
  if (stride < elemBytes)
    return false;

  const size_t start =
      static_cast<size_t>(bv.byteOffset) + static_cast<size_t>(acc.byteOffset);

  // Bounds check last element.
  const size_t last =
      start + (static_cast<size_t>(acc.count) - 1u) * stride + elemBytes;
  if (start >= buf.data.size() || last > buf.data.size())
    return false;

  outData = reinterpret_cast<const float *>(&buf.data[start]);
  outStrideBytes = stride;
  return true;
}

static bool GetAccessorIndexData(const tinygltf::Model &model,
                                 const tinygltf::Accessor &acc,
                                 const uint8_t *&outBytes,
                                 size_t &outStrideBytes,
                                 size_t &outIndexElemBytes) {
  outBytes = nullptr;
  outStrideBytes = 0;
  outIndexElemBytes = 0;

  if (acc.bufferView < 0 ||
      acc.bufferView >= static_cast<int>(model.bufferViews.size()))
    return false;
  if (acc.type != TINYGLTF_TYPE_SCALAR)
    return false;
  if (acc.count == 0)
    return false;

  if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
    outIndexElemBytes = 1;
  else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
    outIndexElemBytes = 2;
  else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
    outIndexElemBytes = 4;
  else
    return false;

  const auto &bv = model.bufferViews[acc.bufferView];
  if (bv.buffer < 0 || bv.buffer >= static_cast<int>(model.buffers.size()))
    return false;
  const auto &buf = model.buffers[bv.buffer];

  const size_t stride =
      bv.byteStride ? static_cast<size_t>(bv.byteStride) : outIndexElemBytes;
  if (stride < outIndexElemBytes)
    return false;

  const size_t start =
      static_cast<size_t>(bv.byteOffset) + static_cast<size_t>(acc.byteOffset);
  const size_t last =
      start + (static_cast<size_t>(acc.count) - 1u) * stride + outIndexElemBytes;
  if (start >= buf.data.size() || last > buf.data.size())
    return false;

  outBytes = &buf.data[start];
  outStrideBytes = stride;
  return true;
}

static LoadedImage ConvertToRgba(const tinygltf::Image &img) {
  LoadedImage out;
  out.width = img.width;
  out.height = img.height;
  out.channels = 4;

  if (img.width <= 0 || img.height <= 0 || img.image.empty())
    return out;

  const int w = img.width;
  const int h = img.height;
  const int c = img.component;

  out.pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);

  const unsigned char *src = img.image.data();
  unsigned char *dst = out.pixels.data();

  if (c == 4) {
    memcpy(dst, src, out.pixels.size());
    return out;
  }

  for (int i = 0; i < w * h; ++i) {
    unsigned char r = 0, g = 0, b = 0, a = 255;
    if (c == 3) {
      r = src[i * 3 + 0];
      g = src[i * 3 + 1];
      b = src[i * 3 + 2];
    } else if (c == 2) {
      r = g = b = src[i * 2 + 0];
      a = src[i * 2 + 1];
    } else if (c == 1) {
      r = g = b = src[i];
    }
    dst[i * 4 + 0] = r;
    dst[i * 4 + 1] = g;
    dst[i * 4 + 2] = b;
    dst[i * 4 + 3] = a;
  }

  return out;
}

bool GltfLoader::LoadModel(const std::string &path) {
  tinygltf::Model model;
  tinygltf::TinyGLTF loader;
  std::string err;
  std::string warn;

  bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, path);
  // If it was a binary .glb, we would use LoadBinaryFromFile

  if (!warn.empty()) {
    std::cout << "WARN: " << warn << std::endl;
  }

  if (!err.empty()) {
    std::cerr << "ERR: " << err << std::endl;
  }

  if (!ret) {
    std::cerr << "Failed to parse glTF: " << path << std::endl;
    return false;
  }

  // Extract first mesh for simplicity
  if (model.meshes.empty())
    return false;

  const auto &mesh = model.meshes[0];
  if (mesh.primitives.empty())
    return false;

  const auto &prim = mesh.primitives[0];

  // Get accessors
  // POSITION
  if (prim.attributes.find("POSITION") == prim.attributes.end())
    return false;
  const int posIdx = prim.attributes.at("POSITION");
  if (posIdx < 0 || posIdx >= static_cast<int>(model.accessors.size()))
    return false;
  const auto &posAcc = model.accessors[posIdx];
  const float *posData = nullptr;
  size_t posStride = 0;
  if (!GetAccessorFloatData(model, posAcc, TINYGLTF_TYPE_VEC3, posData,
                            posStride))
    return false;

  // NORMAL
  const float *normData = nullptr;
  size_t normStride = 0;
  if (prim.attributes.find("NORMAL") != prim.attributes.end()) {
    const int normIdx = prim.attributes.at("NORMAL");
    if (normIdx >= 0 && normIdx < static_cast<int>(model.accessors.size())) {
      const auto &normAcc = model.accessors[normIdx];
      GetAccessorFloatData(model, normAcc, TINYGLTF_TYPE_VEC3, normData,
                           normStride);
    }
  }

  // TEXCOORD_0
  const float *uvData = nullptr;
  size_t uvStride = 0;
  if (prim.attributes.find("TEXCOORD_0") != prim.attributes.end()) {
    const int uvIdx = prim.attributes.at("TEXCOORD_0");
    if (uvIdx >= 0 && uvIdx < static_cast<int>(model.accessors.size())) {
      const auto &uvAcc = model.accessors[uvIdx];
      GetAccessorFloatData(model, uvAcc, TINYGLTF_TYPE_VEC2, uvData, uvStride);
    }
  }

  // INDICES
  if (prim.indices < 0)
    return false;
  if (prim.indices >= static_cast<int>(model.accessors.size()))
    return false;
  const auto &idxAcc = model.accessors[prim.indices];
  const uint8_t *idxBytes = nullptr;
  size_t idxStride = 0;
  size_t idxElemBytes = 0;
  if (!GetAccessorIndexData(model, idxAcc, idxBytes, idxStride, idxElemBytes))
    return false;

  // Resize output
  m_mesh.vertices.resize(posAcc.count);

  // Interleave
  for (size_t i = 0; i < posAcc.count; ++i) {
    const float *p = reinterpret_cast<const float *>(
        reinterpret_cast<const uint8_t *>(posData) + i * posStride);
    m_mesh.vertices[i].pos[0] = p[0];
    m_mesh.vertices[i].pos[1] = p[1];
    m_mesh.vertices[i].pos[2] = p[2];

    if (normData) {
      const float *n = reinterpret_cast<const float *>(
          reinterpret_cast<const uint8_t *>(normData) + i * normStride);
      m_mesh.vertices[i].normal[0] = n[0];
      m_mesh.vertices[i].normal[1] = n[1];
      m_mesh.vertices[i].normal[2] = n[2];
    } else {
      m_mesh.vertices[i].normal[0] = 0.0f;
      m_mesh.vertices[i].normal[1] = 1.0f;
      m_mesh.vertices[i].normal[2] = 0.0f;
    }

    if (uvData) {
      const float *uv = reinterpret_cast<const float *>(
          reinterpret_cast<const uint8_t *>(uvData) + i * uvStride);
      m_mesh.vertices[i].uv[0] = uv[0];
      m_mesh.vertices[i].uv[1] = uv[1];
    } else {
      m_mesh.vertices[i].uv[0] = 0.0f;
      m_mesh.vertices[i].uv[1] = 0.0f;
    }
  }

  // Indices (handling format)
  // Supports unsigned byte (5121), unsigned short (5123) or unsigned int (5125)
  m_mesh.indices.reserve(idxAcc.count);
  m_mesh.indices.clear();
  for (size_t i = 0; i < idxAcc.count; ++i) {
    const uint8_t *e = idxBytes + i * idxStride;
    uint32_t idx = 0;
    if (idxElemBytes == 1) {
      idx = *reinterpret_cast<const uint8_t *>(e);
    } else if (idxElemBytes == 2) {
      idx = *reinterpret_cast<const uint16_t *>(e);
    } else if (idxElemBytes == 4) {
      idx = *reinterpret_cast<const uint32_t *>(e);
    }
    m_mesh.indices.push_back(static_cast<uint16_t>(idx));
  }

  std::cout << "Extracted " << m_mesh.vertices.size() << " vertices, "
            << m_mesh.indices.size() << " indices.\n";

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

  // Fallback: if there is no baseColor texture specified, just pick image[0].
  if (imageIndex < 0 && !model.images.empty())
    imageIndex = 0;

  if (imageIndex >= 0 && imageIndex < static_cast<int>(model.images.size())) {
    const auto &img = model.images[imageIndex];
    m_baseColorImage = ConvertToRgba(img);
    std::cout << "BaseColor Image: " << img.width << "x" << img.height << " ("
              << img.component << " -> RGBA)\n";
  } else {
    std::cout << "No baseColor texture image found in glTF.\n";
  }

  return true;
}
