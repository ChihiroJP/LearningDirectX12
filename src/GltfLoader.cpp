#include "GltfLoader.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// stb_image is compiled inside tinygltf; we only need the header for stbi_load.
#include "stb_image.h"

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

// Compute tangents from UV-based per-triangle method (Lengyel's method).
// Fallback when glTF does not provide TANGENT attribute.
// NOTE: For production use, MikkTSpace is the standard. This is simpler and
// adequate for learning.
static void ComputeTangents(LoadedMesh &mesh) {
  const size_t vertCount = mesh.vertices.size();
  const size_t idxCount = mesh.indices.size();

  // Accumulate tangent per vertex.
  std::vector<float> tanAccum(vertCount * 3, 0.0f);

  for (size_t i = 0; i + 2 < idxCount; i += 3) {
    const uint16_t i0 = mesh.indices[i + 0];
    const uint16_t i1 = mesh.indices[i + 1];
    const uint16_t i2 = mesh.indices[i + 2];

    const auto &v0 = mesh.vertices[i0];
    const auto &v1 = mesh.vertices[i1];
    const auto &v2 = mesh.vertices[i2];

    float e1x = v1.pos[0] - v0.pos[0];
    float e1y = v1.pos[1] - v0.pos[1];
    float e1z = v1.pos[2] - v0.pos[2];

    float e2x = v2.pos[0] - v0.pos[0];
    float e2y = v2.pos[1] - v0.pos[1];
    float e2z = v2.pos[2] - v0.pos[2];

    float du1 = v1.uv[0] - v0.uv[0];
    float dv1 = v1.uv[1] - v0.uv[1];
    float du2 = v2.uv[0] - v0.uv[0];
    float dv2 = v2.uv[1] - v0.uv[1];

    float det = du1 * dv2 - du2 * dv1;
    if (std::abs(det) < 1e-8f)
      det = 1.0f; // degenerate UV, use identity
    float invDet = 1.0f / det;

    float tx = (dv2 * e1x - dv1 * e2x) * invDet;
    float ty = (dv2 * e1y - dv1 * e2y) * invDet;
    float tz = (dv2 * e1z - dv1 * e2z) * invDet;

    // Accumulate for all 3 vertices of the triangle.
    for (uint16_t idx : {i0, i1, i2}) {
      tanAccum[idx * 3 + 0] += tx;
      tanAccum[idx * 3 + 1] += ty;
      tanAccum[idx * 3 + 2] += tz;
    }
  }

  // Normalize accumulated tangents and store. w=1.0 (right-handed assumption).
  for (size_t v = 0; v < vertCount; ++v) {
    float tx = tanAccum[v * 3 + 0];
    float ty = tanAccum[v * 3 + 1];
    float tz = tanAccum[v * 3 + 2];
    float len = std::sqrt(tx * tx + ty * ty + tz * tz);
    if (len > 1e-8f) {
      mesh.vertices[v].tangent[0] = tx / len;
      mesh.vertices[v].tangent[1] = ty / len;
      mesh.vertices[v].tangent[2] = tz / len;
    } else {
      // Degenerate: fall back to +X.
      mesh.vertices[v].tangent[0] = 1.0f;
      mesh.vertices[v].tangent[1] = 0.0f;
      mesh.vertices[v].tangent[2] = 0.0f;
    }
    mesh.vertices[v].tangent[3] = 1.0f;
  }
}

// Helper: extract a texture image from a glTF material texture reference.
static LoadedImage ExtractTextureImage(const tinygltf::Model &model,
                                       int textureIndex) {
  LoadedImage result{};
  if (textureIndex < 0 || textureIndex >= static_cast<int>(model.textures.size()))
    return result;
  const int srcImg = model.textures[textureIndex].source;
  if (srcImg < 0 || srcImg >= static_cast<int>(model.images.size()))
    return result;
  return ConvertToRgba(model.images[srcImg]);
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

  // TANGENT (vec4: xyz = tangent direction, w = handedness)
  const float *tanData = nullptr;
  size_t tanStride = 0;
  bool hasTangents = false;
  if (prim.attributes.find("TANGENT") != prim.attributes.end()) {
    const int tanIdx = prim.attributes.at("TANGENT");
    if (tanIdx >= 0 && tanIdx < static_cast<int>(model.accessors.size())) {
      const auto &tanAcc = model.accessors[tanIdx];
      hasTangents = GetAccessorFloatData(model, tanAcc, TINYGLTF_TYPE_VEC4,
                                         tanData, tanStride);
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

    if (hasTangents && tanData) {
      const float *t = reinterpret_cast<const float *>(
          reinterpret_cast<const uint8_t *>(tanData) + i * tanStride);
      m_mesh.vertices[i].tangent[0] = t[0];
      m_mesh.vertices[i].tangent[1] = t[1];
      m_mesh.vertices[i].tangent[2] = t[2];
      m_mesh.vertices[i].tangent[3] = t[3]; // handedness
    } else {
      // Will be computed below if not present.
      m_mesh.vertices[i].tangent[0] = 1.0f;
      m_mesh.vertices[i].tangent[1] = 0.0f;
      m_mesh.vertices[i].tangent[2] = 0.0f;
      m_mesh.vertices[i].tangent[3] = 1.0f;
    }
  }

  // Indices (handling format)
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

  // Compute tangents from UVs if glTF didn't provide them.
  if (!hasTangents) {
    ComputeTangents(m_mesh);
    std::cout << "Computed tangents from UVs (glTF had no TANGENT attribute).\n";
  }

  std::cout << "Extracted " << m_mesh.vertices.size() << " vertices, "
            << m_mesh.indices.size() << " indices.\n";

  // ---- Extract material textures ----
  m_baseColorImage = {};
  m_normalImage = {};
  m_metalRoughImage = {};
  m_aoImage = {};
  m_emissiveImage = {};
  m_emissiveFactor[0] = m_emissiveFactor[1] = m_emissiveFactor[2] = 0.0f;

  if (prim.material >= 0 && prim.material < static_cast<int>(model.materials.size())) {
    const auto &mat = model.materials[prim.material];

    // BaseColor texture
    int imageIndex = -1;
    const int bcTexIndex = mat.pbrMetallicRoughness.baseColorTexture.index;
    if (bcTexIndex >= 0 && bcTexIndex < static_cast<int>(model.textures.size())) {
      const int srcImg = model.textures[bcTexIndex].source;
      if (srcImg >= 0 && srcImg < static_cast<int>(model.images.size()))
        imageIndex = srcImg;
    }
    // Fallback: if there is no baseColor texture specified, just pick image[0].
    if (imageIndex < 0 && !model.images.empty())
      imageIndex = 0;
    if (imageIndex >= 0 && imageIndex < static_cast<int>(model.images.size())) {
      m_baseColorImage = ConvertToRgba(model.images[imageIndex]);
      std::cout << "BaseColor Image: " << model.images[imageIndex].width << "x"
                << model.images[imageIndex].height << " ("
                << model.images[imageIndex].component << " -> RGBA)\n";
    } else {
      std::cout << "No baseColor texture image found in glTF.\n";
    }

    // Normal map texture
    const int normTexIndex = mat.normalTexture.index;
    m_normalImage = ExtractTextureImage(model, normTexIndex);
    if (!m_normalImage.pixels.empty())
      std::cout << "Normal map: " << m_normalImage.width << "x" << m_normalImage.height << "\n";

    // MetallicRoughness texture (glTF: G=roughness, B=metallic)
    const int mrTexIndex = mat.pbrMetallicRoughness.metallicRoughnessTexture.index;
    m_metalRoughImage = ExtractTextureImage(model, mrTexIndex);
    if (!m_metalRoughImage.pixels.empty())
      std::cout << "MetalRough map: " << m_metalRoughImage.width << "x" << m_metalRoughImage.height << "\n";

    // Occlusion (AO) texture
    const int aoTexIndex = mat.occlusionTexture.index;
    m_aoImage = ExtractTextureImage(model, aoTexIndex);
    if (!m_aoImage.pixels.empty())
      std::cout << "AO map: " << m_aoImage.width << "x" << m_aoImage.height << "\n";

    // Emissive texture + factor
    const int emTexIndex = mat.emissiveTexture.index;
    m_emissiveImage = ExtractTextureImage(model, emTexIndex);
    if (!m_emissiveImage.pixels.empty())
      std::cout << "Emissive map: " << m_emissiveImage.width << "x" << m_emissiveImage.height << "\n";

    if (mat.emissiveFactor.size() >= 3) {
      m_emissiveFactor[0] = static_cast<float>(mat.emissiveFactor[0]);
      m_emissiveFactor[1] = static_cast<float>(mat.emissiveFactor[1]);
      m_emissiveFactor[2] = static_cast<float>(mat.emissiveFactor[2]);
    }

    // Phase 11.5: populate Material struct from glTF scalars
    const auto &bcf = mat.pbrMetallicRoughness.baseColorFactor;
    if (bcf.size() >= 4) {
      m_material.baseColorFactor = {
        static_cast<float>(bcf[0]), static_cast<float>(bcf[1]),
        static_cast<float>(bcf[2]), static_cast<float>(bcf[3])};
    }
    m_material.metallicFactor  = static_cast<float>(mat.pbrMetallicRoughness.metallicFactor);
    m_material.roughnessFactor = static_cast<float>(mat.pbrMetallicRoughness.roughnessFactor);
    m_material.emissiveFactor  = {m_emissiveFactor[0], m_emissiveFactor[1], m_emissiveFactor[2]};

    // Texture presence flags
    m_material.hasBaseColor  = !m_baseColorImage.pixels.empty();
    m_material.hasNormal     = !m_normalImage.pixels.empty();
    m_material.hasMetalRough = !m_metalRoughImage.pixels.empty();
    m_material.hasAO         = !m_aoImage.pixels.empty();
    m_material.hasEmissive   = !m_emissiveImage.pixels.empty();
    // hasHeight set later in LoadHeightMap()
  } else {
    // No material: try image[0] as baseColor fallback.
    if (!model.images.empty()) {
      m_baseColorImage = ConvertToRgba(model.images[0]);
      std::cout << "No material; using image[0] as baseColor.\n";
    }
  }

  return true;
}

bool GltfLoader::LoadHeightMap(const std::string &path) {
  m_heightImage = {};

  int w = 0, h = 0, c = 0;
  unsigned char *data = stbi_load(path.c_str(), &w, &h, &c, 4); // force RGBA
  if (!data) {
    std::cerr << "Failed to load height map: " << path << "\n";
    return false;
  }

  m_heightImage.width = w;
  m_heightImage.height = h;
  m_heightImage.channels = 4;
  m_heightImage.pixels.assign(data, data + static_cast<size_t>(w) * h * 4);
  stbi_image_free(data);

  m_material.hasHeight = true;

  std::cout << "Height map: " << w << "x" << h << " (" << c << " -> RGBA)\n";
  return true;
}

// ---- Standalone image loader (for editor texture slot assignment) ----

bool LoadImageFile(const std::string &path, LoadedImage &outImage) {
  outImage = {};
  int w = 0, h = 0, c = 0;
  unsigned char *data = stbi_load(path.c_str(), &w, &h, &c, 4); // force RGBA
  if (!data) {
    std::cerr << "Failed to load image: " << path << "\n";
    return false;
  }
  outImage.width = w;
  outImage.height = h;
  outImage.channels = 4;
  outImage.pixels.assign(data, data + static_cast<size_t>(w) * h * 4);
  stbi_image_free(data);
  return true;
}
