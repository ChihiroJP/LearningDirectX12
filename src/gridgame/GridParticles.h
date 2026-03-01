// ======================================
// File: GridParticles.h
// Purpose: Game-specific particle emitters for Grid Gauntlet neon VFX.
//          Fire embers (orange rising), ice crystals (cyan drifting).
// ======================================

#pragma once

#include "../particle.h"

#include <DirectXMath.h>
#include <random>

// ---- Fire Ember: small orange particle rising from fire tiles ----

class FireEmberParticle : public Particle {
  float m_scale{0.12f};
  float m_alpha{1.0f};

public:
  FireEmberParticle(const DirectX::XMVECTOR &pos,
                    const DirectX::XMVECTOR &vel, double life)
      : Particle(pos, vel, life) {}

  void Update(double dt) override {
    float ratio =
        static_cast<float>(std::min(GetAccumulatedTime() / GetLifeTime(), 1.0));
    m_scale = 0.12f * (1.0f - ratio);
    m_alpha = 1.0f - ratio * ratio; // slow fade then fast drop
    AddPosition(
        DirectX::XMVectorScale(GetVelocity(), static_cast<float>(dt)));
    // Slight upward drift (buoyancy).
    AddVelocity(DirectX::XMVectorScale(
        DirectX::XMVectorSet(0.0f, 0.5f, 0.0f, 0.0f),
        static_cast<float>(dt)));
    Particle::Update(dt);
  }

  ParticleVisual GetVisual() const override {
    ParticleVisual v{};
    DirectX::XMStoreFloat3(&v.position, GetPosition());
    v.scale = m_scale;
    // Orange -> red shift.
    float ratio =
        static_cast<float>(std::min(GetAccumulatedTime() / GetLifeTime(), 1.0));
    v.color = {1.0f, 0.5f - ratio * 0.3f, 0.1f - ratio * 0.08f, m_alpha};
    return v;
  }
};

class FireEmberEmitter : public Emitter {
  std::mt19937 m_mt{std::random_device{}()};

public:
  FireEmberEmitter(size_t cap, const DirectX::XMVECTOR &pos, double rate,
                   bool emit)
      : Emitter(cap, pos, rate, emit) {}

protected:
  Particle *createParticle() override {
    std::uniform_real_distribution<float> off(-0.4f, 0.4f);
    std::uniform_real_distribution<float> spd(0.3f, 0.8f);
    std::uniform_real_distribution<float> life(0.6f, 1.5f);

    auto spawn = DirectX::XMVectorAdd(
        GetPosition(),
        DirectX::XMVectorSet(off(m_mt), 0.0f, off(m_mt), 0.0f));
    float s = spd(m_mt);
    auto vel = DirectX::XMVectorSet(off(m_mt) * 0.2f, s, off(m_mt) * 0.2f, 0.0f);
    return new FireEmberParticle(spawn, vel, life(m_mt));
  }
};

// ---- Ice Crystal: small cyan particle drifting up from ice tiles ----

class IceCrystalParticle : public Particle {
  float m_scale{0.08f};
  float m_alpha{1.0f};

public:
  IceCrystalParticle(const DirectX::XMVECTOR &pos,
                     const DirectX::XMVECTOR &vel, double life)
      : Particle(pos, vel, life) {}

  void Update(double dt) override {
    float ratio =
        static_cast<float>(std::min(GetAccumulatedTime() / GetLifeTime(), 1.0));
    m_scale = 0.08f * (1.0f - ratio * 0.5f); // slight shrink
    m_alpha = 1.0f - ratio;
    AddPosition(
        DirectX::XMVectorScale(GetVelocity(), static_cast<float>(dt)));
    Particle::Update(dt);
  }

  ParticleVisual GetVisual() const override {
    ParticleVisual v{};
    DirectX::XMStoreFloat3(&v.position, GetPosition());
    v.scale = m_scale;
    v.color = {0.2f, 0.7f, 1.0f, m_alpha * 0.8f}; // bright cyan
    return v;
  }
};

class IceCrystalEmitter : public Emitter {
  std::mt19937 m_mt{std::random_device{}()};

public:
  IceCrystalEmitter(size_t cap, const DirectX::XMVECTOR &pos, double rate,
                    bool emit)
      : Emitter(cap, pos, rate, emit) {}

protected:
  Particle *createParticle() override {
    std::uniform_real_distribution<float> off(-0.3f, 0.3f);
    std::uniform_real_distribution<float> spd(0.1f, 0.4f);
    std::uniform_real_distribution<float> drift(-0.15f, 0.15f);
    std::uniform_real_distribution<float> life(1.0f, 2.5f);

    auto spawn = DirectX::XMVectorAdd(
        GetPosition(),
        DirectX::XMVectorSet(off(m_mt), 0.0f, off(m_mt), 0.0f));
    auto vel =
        DirectX::XMVectorSet(drift(m_mt), spd(m_mt), drift(m_mt), 0.0f);
    return new IceCrystalParticle(spawn, vel, life(m_mt));
  }
};
