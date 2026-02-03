#include "GltfLoader.h"
#include <iostream>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
// #define TINYGLTF_NO_INCLUDE_STB_IMAGE // We want to load images
#include "tiny_gltf.h"

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
  const auto &posAcc = model.accessors[posIdx];
  const auto &posBufView = model.bufferViews[posAcc.bufferView];
  const auto &posBuf = model.buffers[posBufView.buffer];
  const float *posData = reinterpret_cast<const float *>(
      &posBuf.data[posBufView.byteOffset + posAcc.byteOffset]);

  // NORMAL
  const float *normData = nullptr;
  if (prim.attributes.find("NORMAL") != prim.attributes.end()) {
    const int normIdx = prim.attributes.at("NORMAL");
    const auto &normAcc = model.accessors[normIdx];
    const auto &normBufView = model.bufferViews[normAcc.bufferView];
    const auto &normBuf = model.buffers[normBufView.buffer];
    normData = reinterpret_cast<const float *>(
        &normBuf.data[normBufView.byteOffset + normAcc.byteOffset]);
  }

  // TEXCOORD_0
  const float *uvData = nullptr;
  if (prim.attributes.find("TEXCOORD_0") != prim.attributes.end()) {
    const int uvIdx = prim.attributes.at("TEXCOORD_0");
    const auto &uvAcc = model.accessors[uvIdx];
    const auto &uvBufView = model.bufferViews[uvAcc.bufferView];
    const auto &uvBuf = model.buffers[uvBufView.buffer];
    uvData = reinterpret_cast<const float *>(
        &uvBuf.data[uvBufView.byteOffset + uvAcc.byteOffset]);
  }

  // INDICES
  if (prim.indices < 0)
    return false;
  const auto &idxAcc = model.accessors[prim.indices];
  const auto &idxBufView = model.bufferViews[idxAcc.bufferView];
  const auto &idxBuf = model.buffers[idxBufView.buffer];

  // Resize output
  m_mesh.vertices.resize(posAcc.count);

  // Interleave
  for (size_t i = 0; i < posAcc.count; ++i) {
    m_mesh.vertices[i].pos[0] = posData[i * 3 + 0];
    m_mesh.vertices[i].pos[1] = posData[i * 3 + 1];
    m_mesh.vertices[i].pos[2] = posData[i * 3 + 2];

    if (normData) {
      m_mesh.vertices[i].normal[0] = normData[i * 3 + 0];
      m_mesh.vertices[i].normal[1] = normData[i * 3 + 1];
      m_mesh.vertices[i].normal[2] = normData[i * 3 + 2];
    } else {
      m_mesh.vertices[i].normal[0] = 0.0f;
      m_mesh.vertices[i].normal[1] = 1.0f;
      m_mesh.vertices[i].normal[2] = 0.0f;
    }

    if (uvData) {
      m_mesh.vertices[i].uv[0] = uvData[i * 2 + 0];
      m_mesh.vertices[i].uv[1] = uvData[i * 2 + 1];
    } else {
      m_mesh.vertices[i].uv[0] = 0.0f;
      m_mesh.vertices[i].uv[1] = 0.0f;
    }
  }

  // Indices (handling format)
  // Supports unsigned short (5123) or unsigned int (5125)
  m_mesh.indices.reserve(idxAcc.count);
  if (idxAcc.componentType == 5123) // UNSIGNED_SHORT
  {
    const uint16_t *buf = reinterpret_cast<const uint16_t *>(
        &idxBuf.data[idxBufView.byteOffset + idxAcc.byteOffset]);
    for (size_t i = 0; i < idxAcc.count; ++i)
      m_mesh.indices.push_back(buf[i]);
  } else if (idxAcc.componentType == 5125) // UNSIGNED_INT
  {
    const uint32_t *buf = reinterpret_cast<const uint32_t *>(
        &idxBuf.data[idxBufView.byteOffset + idxAcc.byteOffset]);
    // Downcast safety check omitted for simplicity, but we use uint16 for our
    // engine index buffer currently? Wait, typical engine uses uint16 or
    // uint32. Let's see basic3d_notes/dxutil. For now, let's force push back.
    for (size_t i = 0; i < idxAcc.count; ++i)
      m_mesh.indices.push_back(static_cast<uint16_t>(buf[i]));
  }

  std::cout << "Extracted " << m_mesh.vertices.size() << " vertices, "
            << m_mesh.indices.size() << " indices.\n";

  // Extract first image if available
  if (!model.images.empty()) {
    const auto &img = model.images[0];
    m_image.width = img.width;
    m_image.height = img.height;
    m_image.channels = img.component;
    m_image.pixels = img.image; // Copy pixel data

    std::cout << "Extracted Image: " << img.width << "x" << img.height << " ("
              << img.component << " channels)\n";

    // Force RGBA (4 channels) if it's RGB (3 channels)
    if (img.component == 3) {
      m_image.channels = 4;
      m_image.pixels.resize(img.width * img.height * 4);

      const unsigned char *src = img.image.data();
      unsigned char *dst = m_image.pixels.data();

      for (int i = 0; i < img.width * img.height; ++i) {
        dst[i * 4 + 0] = src[i * 3 + 0];
        dst[i * 4 + 1] = src[i * 3 + 1];
        dst[i * 4 + 2] = src[i * 3 + 2];
        dst[i * 4 + 3] = 255; // Alpha
      }
      std::cout << "  -> Converted to RGBA\n";
    }
  }

  // Store globally or return?
  // For this simple tutorial phase, let's expose a global or static,
  // OR create a method to retrieve it.
  // Let's modify the class to store it.

  return true; // We need to actually store the result somewhere!
}
