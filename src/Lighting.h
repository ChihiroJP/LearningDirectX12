// ======================================
// File: Lighting.h
// Purpose: Lighting data structures shared between CPU and shaders (v1: simple
//          directional "sun" light parameters for mesh shading)
// ======================================

#pragma once

#include <DirectXMath.h>

// Phase 6 (Lighting v1):
// A minimal set of parameters we can tweak from ImGui and pass into mesh draws.
// This is NOT a full material system yet; it's a learning-friendly "PBR-lite"
// control surface.
struct MeshLightingParams {
  // Direction of *sun rays* traveling through the world (not "to the light").
  // In shader we convert to L = -raysDir.
  DirectX::XMFLOAT3 lightDir = {0.3f, -1.0f, 0.2f};
  float lightIntensity = 3.0f;

  DirectX::XMFLOAT3 lightColor = {1.0f, 0.98f, 0.92f};
  float roughness = 0.8f;

  float metallic = 0.0f;
  float _pad0 = 0.0f;
  float _pad1 = 0.0f;
  float _pad2 = 0.0f;
};

