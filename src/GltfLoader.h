#pragma once
#include "DxUtil.h"
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

class GltfLoader {
public:
  bool LoadModel(const std::string &path);
  const LoadedMesh &GetMesh() const { return m_mesh; }
  // Phase 5.5: return the material's baseColor texture image (albedo).
  const LoadedImage &GetBaseColorImage() const { return m_baseColorImage; }

private:
  LoadedMesh m_mesh;
  LoadedImage m_baseColorImage;
  // We'll add helper functions here to parse nodes/meshes
};
