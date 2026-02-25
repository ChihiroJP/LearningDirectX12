// ======================================
// File: Scene.cpp
// Purpose: Entity management, FrameData building, GPU resource creation,
//          and JSON scene serialization.
// ======================================

#include "Scene.h"
#include "../DxContext.h"
#include "../GltfLoader.h"
#include "../ProceduralMesh.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>

using namespace DirectX;
using json = nlohmann::json;

// ---- Entity management ----

EntityId Scene::AddEntity(const std::string &name) {
  Entity e;
  e.id = m_nextId++;
  e.name = name;
  m_entities.push_back(std::move(e));
  return m_entities.back().id;
}

void Scene::AddEntityDirect(const Entity &entity) {
  m_entities.push_back(entity);
  if (entity.id >= m_nextId)
    m_nextId = entity.id + 1;
}

EntityId Scene::AllocateId() { return m_nextId++; }

void Scene::RemoveEntity(EntityId id) {
  m_entities.erase(
      std::remove_if(m_entities.begin(), m_entities.end(),
                     [id](const Entity &e) { return e.id == id; }),
      m_entities.end());
}

Entity *Scene::FindEntity(EntityId id) {
  for (auto &e : m_entities)
    if (e.id == id)
      return &e;
  return nullptr;
}

const Entity *Scene::FindEntity(EntityId id) const {
  for (const auto &e : m_entities)
    if (e.id == id)
      return &e;
  return nullptr;
}

void Scene::Clear() {
  m_entities.clear();
  m_nextId = 1;
  m_lightSettings = SceneLightSettings{};
  m_shadowSettings = SceneShadowSettings{};
  m_postProcessSettings = ScenePostProcessSettings{};
}

// ---- Build FrameData from entities ----

void Scene::BuildFrameData(FrameData &frame) const {
  // Apply scene-global lighting settings.
  frame.lighting.lightDir = m_lightSettings.lightDir;
  frame.lighting.lightIntensity = m_lightSettings.lightIntensity;
  frame.lighting.lightColor = m_lightSettings.lightColor;
  // iblIntensity applied after BuildFrameData in main.cpp (gated by iblEnabled toggle).

  // Apply scene-global shadow & SSAO settings (Phase 4).
  frame.shadowsEnabled = m_shadowSettings.shadowsEnabled;
  frame.shadowBias = m_shadowSettings.shadowBias;
  frame.shadowStrength = m_shadowSettings.shadowStrength;
  frame.ssaoEnabled = m_shadowSettings.ssaoEnabled;
  frame.ssaoRadius = m_shadowSettings.ssaoRadius;
  frame.ssaoBias = m_shadowSettings.ssaoBias;
  frame.ssaoPower = m_shadowSettings.ssaoPower;
  frame.ssaoKernelSize = m_shadowSettings.ssaoKernelSize;
  frame.ssaoStrength = m_shadowSettings.ssaoStrength;

  // Apply scene-global post-processing settings (Phase 4).
  frame.exposure = m_postProcessSettings.exposure;
  frame.bloomEnabled = m_postProcessSettings.bloomEnabled;
  frame.bloomThreshold = m_postProcessSettings.bloomThreshold;
  frame.bloomIntensity = m_postProcessSettings.bloomIntensity;
  frame.taaEnabled = m_postProcessSettings.taaEnabled;
  frame.taaBlendFactor = m_postProcessSettings.taaBlendFactor;
  frame.fxaaEnabled = m_postProcessSettings.fxaaEnabled;
  frame.motionBlurEnabled = m_postProcessSettings.motionBlurEnabled;
  frame.motionBlurStrength = m_postProcessSettings.motionBlurStrength;
  frame.motionBlurSamples = m_postProcessSettings.motionBlurSamples;
  frame.dofEnabled = m_postProcessSettings.dofEnabled;
  frame.dofFocalDistance = m_postProcessSettings.dofFocalDistance;
  frame.dofFocalRange = m_postProcessSettings.dofFocalRange;
  frame.dofMaxBlur = m_postProcessSettings.dofMaxBlur;

  for (const auto &e : m_entities) {
    if (!e.active)
      continue;

    XMMATRIX world = e.transform.WorldMatrix();

    // Mesh -> RenderItem
    if (e.mesh.has_value() && e.mesh->meshId != UINT32_MAX) {
      frame.opaqueItems.push_back({e.mesh->meshId, world});
    }

    // PointLight -> GPUPointLight
    if (e.pointLight.has_value() && e.pointLight->enabled) {
      GPUPointLight pl = {};
      pl.position = e.transform.position;
      pl.range = e.pointLight->range;
      pl.color = e.pointLight->color;
      pl.intensity = e.pointLight->intensity;
      frame.pointLights.push_back(pl);
    }

    // SpotLight -> GPUSpotLight
    if (e.spotLight.has_value() && e.spotLight->enabled) {
      GPUSpotLight sl = {};
      sl.position = e.transform.position;
      sl.range = e.spotLight->range;
      sl.color = e.spotLight->color;
      sl.intensity = e.spotLight->intensity;
      sl.direction = e.spotLight->direction;
      float innerRad = XMConvertToRadians(e.spotLight->innerAngleDeg);
      float outerRad = XMConvertToRadians(e.spotLight->outerAngleDeg);
      sl.innerConeAngleCos = std::cos(innerRad);
      sl.outerConeAngleCos = std::cos(outerRad);
      frame.spotLights.push_back(sl);
    }
  }
}

// ---- GPU resource creation ----

void Scene::CreateGpuResources(DxContext &dx) {
  for (auto &e : m_entities) {
    if (e.mesh.has_value())
      CreateEntityMeshGpu(dx, e);
  }
}

void Scene::CreateEntityMeshGpu(DxContext &dx, Entity &entity) {
  if (!entity.mesh.has_value())
    return;

  auto &comp = entity.mesh.value();
  LoadedMesh mesh;

  switch (comp.sourceType) {
  case MeshSourceType::ProceduralCube:
    mesh = ProceduralMesh::CreateCube(comp.size);
    break;
  case MeshSourceType::ProceduralPlane:
    mesh = ProceduralMesh::CreatePlane(comp.width, comp.height);
    break;
  case MeshSourceType::ProceduralCylinder:
    mesh = ProceduralMesh::CreateCylinder(comp.width, comp.height, comp.segments);
    break;
  case MeshSourceType::ProceduralCone:
    mesh = ProceduralMesh::CreateCone(comp.width, comp.height, comp.segments);
    break;
  case MeshSourceType::ProceduralSphere:
    mesh = ProceduralMesh::CreateSphere(comp.size, comp.rings, comp.segments);
    break;
  case MeshSourceType::GltfFile: {
    GltfLoader loader;
    if (loader.LoadModel(comp.gltfPath)) {
      comp.meshId =
          dx.CreateMeshResources(loader.GetMesh(), loader.GetMaterialImages(),
                                 comp.material);
    }
    // Apply per-slot texture overrides (fall through to override block below).
    break;
  }
  default:
    break;
  }

  // For non-glTF types, create mesh with default textures.
  if (comp.sourceType != MeshSourceType::GltfFile) {
    comp.meshId = dx.CreateMeshResources(mesh, {}, comp.material);
  }

  // Apply per-slot texture path overrides (editor texture assignment).
  if (comp.meshId != UINT32_MAX) {
    for (uint32_t i = 0; i < 6; ++i) {
      if (!comp.texturePaths[i].empty()) {
        LoadedImage img;
        if (LoadImageFile(comp.texturePaths[i], img)) {
          dx.ReplaceMeshTexture(comp.meshId, i, img);
        }
      }
    }
  }
}

// ---- JSON serialization ----

bool Scene::SaveToFile(const std::string &path) const {
  json j;
  j["nextId"] = m_nextId;

  json entArr = json::array();
  for (const auto &e : m_entities)
    entArr.push_back(EntityToJson(e));
  j["entities"] = entArr;
  j["lightSettings"] = SceneLightSettingsToJson(m_lightSettings);
  j["shadowSettings"] = SceneShadowSettingsToJson(m_shadowSettings);
  j["postProcessSettings"] = ScenePostProcessSettingsToJson(m_postProcessSettings);

  std::ofstream file(path);
  if (!file.is_open())
    return false;

  file << j.dump(2);
  return file.good();
}

bool Scene::LoadFromFile(const std::string &path, DxContext &dx) {
  std::ifstream file(path);
  if (!file.is_open())
    return false;

  json j;
  try {
    file >> j;
  } catch (const json::parse_error &) {
    return false;
  }

  Clear();

  if (j.contains("nextId"))
    m_nextId = j["nextId"].get<EntityId>();

  if (j.contains("lightSettings"))
    m_lightSettings = JsonToSceneLightSettings(j["lightSettings"]);
  else
    m_lightSettings = SceneLightSettings{};

  if (j.contains("shadowSettings"))
    m_shadowSettings = JsonToSceneShadowSettings(j["shadowSettings"]);
  else
    m_shadowSettings = SceneShadowSettings{};

  if (j.contains("postProcessSettings"))
    m_postProcessSettings = JsonToScenePostProcessSettings(j["postProcessSettings"]);
  else
    m_postProcessSettings = ScenePostProcessSettings{};

  if (j.contains("entities") && j["entities"].is_array()) {
    for (const auto &ej : j["entities"])
      m_entities.push_back(JsonToEntity(ej));
  }

  // Recreate GPU resources for all mesh entities.
  CreateGpuResources(dx);

  return true;
}
