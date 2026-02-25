#pragma once
#include "DxUtil.h"
#include "Lighting.h" // Material struct
#include <d3d12.h>
#include <string>
#include <vector>

// Forward declaration
namespace tinygltf {
class Model;
}

struct MeshVertex {
  float pos[3];
  float normal[3];
  float uv[2];
  float tangent[4]; // xyz = tangent direction, w = handedness (+/-1)
};

struct LoadedMesh {
  std::vector<MeshVertex> vertices;
  std::vector<uint16_t> indices;
};

struct LoadedImage {
  int width = 0;
  int height = 0;
  int channels = 0;
  std::vector<unsigned char> pixels;
};

// Per-mesh material images extracted from glTF.
struct MaterialImages {
  const LoadedImage *baseColor = nullptr;
  const LoadedImage *normal = nullptr;
  const LoadedImage *metalRough = nullptr;
  const LoadedImage *ao = nullptr;
  const LoadedImage *emissive = nullptr;
  const LoadedImage *height = nullptr; // displacement / height map for POM
  float emissiveFactor[3] = {0.0f, 0.0f, 0.0f};
};

// Load any image file (PNG/JPG/BMP/TGA) into a LoadedImage via stb_image.
bool LoadImageFile(const std::string &path, LoadedImage &outImage);

class GltfLoader {
public:
  bool LoadModel(const std::string &path);
  const LoadedMesh &GetMesh() const { return m_mesh; }
  const LoadedImage &GetBaseColorImage() const { return m_baseColorImage; }
  const LoadedImage &GetNormalImage() const { return m_normalImage; }
  const LoadedImage &GetMetalRoughImage() const { return m_metalRoughImage; }
  const LoadedImage &GetAOImage() const { return m_aoImage; }
  const LoadedImage &GetEmissiveImage() const { return m_emissiveImage; }
  const LoadedImage &GetHeightImage() const { return m_heightImage; }

  // Load a standalone height/displacement map (PNG/JPG) for POM.
  bool LoadHeightMap(const std::string &path);

  // Material properties extracted from glTF (scalars + texture flags).
  const Material &GetMaterial() const { return m_material; }

  // Emissive factor from glTF material (multiplied with emissive texture).
  float GetEmissiveFactorR() const { return m_emissiveFactor[0]; }
  float GetEmissiveFactorG() const { return m_emissiveFactor[1]; }
  float GetEmissiveFactorB() const { return m_emissiveFactor[2]; }

  // Build a MaterialImages struct from the loaded data (convenience).
  MaterialImages GetMaterialImages() const {
    MaterialImages mat;
    if (!m_baseColorImage.pixels.empty()) mat.baseColor = &m_baseColorImage;
    if (!m_normalImage.pixels.empty()) mat.normal = &m_normalImage;
    if (!m_metalRoughImage.pixels.empty()) mat.metalRough = &m_metalRoughImage;
    if (!m_aoImage.pixels.empty()) mat.ao = &m_aoImage;
    if (!m_emissiveImage.pixels.empty()) mat.emissive = &m_emissiveImage;
    if (!m_heightImage.pixels.empty()) mat.height = &m_heightImage;
    mat.emissiveFactor[0] = m_emissiveFactor[0];
    mat.emissiveFactor[1] = m_emissiveFactor[1];
    mat.emissiveFactor[2] = m_emissiveFactor[2];
    return mat;
  }

private:
  LoadedMesh m_mesh;
  LoadedImage m_baseColorImage;
  LoadedImage m_normalImage;
  LoadedImage m_metalRoughImage;
  LoadedImage m_aoImage;
  LoadedImage m_emissiveImage;
  LoadedImage m_heightImage;
  float m_emissiveFactor[3] = {0.0f, 0.0f, 0.0f};
  Material m_material; // Phase 11.5: per-mesh material scalars + flags
};
