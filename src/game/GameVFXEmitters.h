// ======================================
// File: game/GameVFXEmitters.h
// Purpose: Gameplay VFX particle/emitter types for Phase 1 (VFX & Particles).
// Author : LEE CHEE HOW
// Date   : 2026/02/18
// ======================================

#pragma once

#include "../particle.h"
#include <DirectXMath.h>
#include <random>

// ---- Objective Idle Glow (golden floating particles around uncollected objectives) ----

class ObjectiveGlowParticle : public Particle
{
private:
	float m_scale{ 0.12f };
	float m_alpha{ 1.0f };

public:
	ObjectiveGlowParticle(const DirectX::XMVECTOR& position, const DirectX::XMVECTOR& velocity, double life_time)
		: Particle(position, velocity, life_time) {}

	void Update(double elapsed_time) override;
	ParticleVisual GetVisual() const override;
};

class ObjectiveGlowEmitter : public Emitter
{
private:
	std::mt19937 m_mt{ std::random_device{}() };

public:
	ObjectiveGlowEmitter(size_t capacity, const DirectX::XMVECTOR& position, double particles_per_second, bool is_emmit)
		: Emitter(capacity, position, particles_per_second, is_emmit) {}

protected:
	Particle* createParticle() override;
};

// ---- Pickup Burst (gold sparks on objective collection) ----

class PickupBurstParticle : public Particle
{
private:
	float m_scale{ 0.2f };
	float m_alpha{ 1.0f };

public:
	PickupBurstParticle(const DirectX::XMVECTOR& position, const DirectX::XMVECTOR& velocity, double life_time)
		: Particle(position, velocity, life_time) {}

	void Update(double elapsed_time) override;
	ParticleVisual GetVisual() const override;
};

class PickupBurstEmitter : public Emitter
{
private:
	std::mt19937 m_mt{ std::random_device{}() };

public:
	PickupBurstEmitter(size_t capacity, const DirectX::XMVECTOR& position, double particles_per_second, bool is_emmit)
		: Emitter(capacity, position, particles_per_second, is_emmit) {}

protected:
	Particle* createParticle() override;
};

// ---- Damage Flash (red sparks when player takes damage) ----

class DamageFlashParticle : public Particle
{
private:
	float m_scale{ 0.08f };
	float m_alpha{ 1.0f };

public:
	DamageFlashParticle(const DirectX::XMVECTOR& position, const DirectX::XMVECTOR& velocity, double life_time)
		: Particle(position, velocity, life_time) {}

	void Update(double elapsed_time) override;
	ParticleVisual GetVisual() const override;
};

class DamageFlashEmitter : public Emitter
{
private:
	std::mt19937 m_mt{ std::random_device{}() };

public:
	DamageFlashEmitter(size_t capacity, const DirectX::XMVECTOR& position, double particles_per_second, bool is_emmit)
		: Emitter(capacity, position, particles_per_second, is_emmit) {}

protected:
	Particle* createParticle() override;
};

// ---- Death Spark (orange->red sparks on enemy death) ----

class DeathSparkParticle : public Particle
{
private:
	float m_scale{ 0.12f };
	float m_alpha{ 1.0f };

public:
	DeathSparkParticle(const DirectX::XMVECTOR& position, const DirectX::XMVECTOR& velocity, double life_time)
		: Particle(position, velocity, life_time) {}

	void Update(double elapsed_time) override;
	ParticleVisual GetVisual() const override;
};

class DeathSparkEmitter : public Emitter
{
private:
	std::mt19937 m_mt{ std::random_device{}() };

public:
	DeathSparkEmitter(size_t capacity, const DirectX::XMVECTOR& position, double particles_per_second, bool is_emmit)
		: Emitter(capacity, position, particles_per_second, is_emmit) {}

protected:
	Particle* createParticle() override;
};

// ---- Death Smoke (dark puffs on enemy death) ----

class DeathSmokeParticle : public Particle
{
private:
	float m_scale{ 0.2f };
	float m_alpha{ 1.0f };

public:
	DeathSmokeParticle(const DirectX::XMVECTOR& position, const DirectX::XMVECTOR& velocity, double life_time)
		: Particle(position, velocity, life_time) {}

	void Update(double elapsed_time) override;
	ParticleVisual GetVisual() const override;
};

class DeathSmokeEmitter : public Emitter
{
private:
	std::mt19937 m_mt{ std::random_device{}() };

public:
	DeathSmokeEmitter(size_t capacity, const DirectX::XMVECTOR& position, double particles_per_second, bool is_emmit)
		: Emitter(capacity, position, particles_per_second, is_emmit) {}

protected:
	Particle* createParticle() override;
};

// ---- Attack Swing (cyan-white arc on player attack) ----

class AttackSwingParticle : public Particle
{
private:
	float m_scale{ 0.15f };
	float m_alpha{ 1.0f };

public:
	AttackSwingParticle(const DirectX::XMVECTOR& position, const DirectX::XMVECTOR& velocity, double life_time)
		: Particle(position, velocity, life_time) {}

	void Update(double elapsed_time) override;
	ParticleVisual GetVisual() const override;
};

class AttackSwingEmitter : public Emitter
{
private:
	std::mt19937 m_mt{ std::random_device{}() };
	float m_playerYaw = 0.0f;

public:
	AttackSwingEmitter(size_t capacity, const DirectX::XMVECTOR& position, double particles_per_second, bool is_emmit, float playerYaw)
		: Emitter(capacity, position, particles_per_second, is_emmit), m_playerYaw(playerYaw) {}

protected:
	Particle* createParticle() override;
};

// ---- Hit Impact (white-yellow sparks on enemy when hit connects) ----

class HitImpactParticle : public Particle
{
private:
	float m_scale{ 0.1f };
	float m_alpha{ 1.0f };

public:
	HitImpactParticle(const DirectX::XMVECTOR& position, const DirectX::XMVECTOR& velocity, double life_time)
		: Particle(position, velocity, life_time) {}

	void Update(double elapsed_time) override;
	ParticleVisual GetVisual() const override;
};

class HitImpactEmitter : public Emitter
{
private:
	std::mt19937 m_mt{ std::random_device{}() };

public:
	HitImpactEmitter(size_t capacity, const DirectX::XMVECTOR& position, double particles_per_second, bool is_emmit)
		: Emitter(capacity, position, particles_per_second, is_emmit) {}

protected:
	Particle* createParticle() override;
};

// ---- Footstep Dust (brown puffs when player walks) ----

class FootstepDustParticle : public Particle
{
private:
	float m_scale{ 0.08f };
	float m_alpha{ 1.0f };

public:
	FootstepDustParticle(const DirectX::XMVECTOR& position, const DirectX::XMVECTOR& velocity, double life_time)
		: Particle(position, velocity, life_time) {}

	void Update(double elapsed_time) override;
	ParticleVisual GetVisual() const override;
};

class FootstepDustEmitter : public Emitter
{
private:
	std::mt19937 m_mt{ std::random_device{}() };

public:
	FootstepDustEmitter(size_t capacity, const DirectX::XMVECTOR& position, double particles_per_second, bool is_emmit)
		: Emitter(capacity, position, particles_per_second, is_emmit) {}

protected:
	Particle* createParticle() override;
};

// ---- Sprint Trail (blue-white particles behind player when sprinting) ----

class SprintTrailParticle : public Particle
{
private:
	float m_scale{ 0.08f };
	float m_alpha{ 1.0f };

public:
	SprintTrailParticle(const DirectX::XMVECTOR& position, const DirectX::XMVECTOR& velocity, double life_time)
		: Particle(position, velocity, life_time) {}

	void Update(double elapsed_time) override;
	ParticleVisual GetVisual() const override;
};

class SprintTrailEmitter : public Emitter
{
private:
	std::mt19937 m_mt{ std::random_device{}() };

public:
	SprintTrailEmitter(size_t capacity, const DirectX::XMVECTOR& position, double particles_per_second, bool is_emmit)
		: Emitter(capacity, position, particles_per_second, is_emmit) {}

protected:
	Particle* createParticle() override;
};

// ---- Landing Dust (bigger burst on jump landing) ----

class LandingDustEmitter : public Emitter
{
private:
	std::mt19937 m_mt{ std::random_device{}() };

public:
	LandingDustEmitter(size_t capacity, const DirectX::XMVECTOR& position, double particles_per_second, bool is_emmit)
		: Emitter(capacity, position, particles_per_second, is_emmit) {}

protected:
	Particle* createParticle() override;
};

// ---- Ambient Dust (subtle floating particles in the world) ----

class AmbientDustParticle : public Particle
{
private:
	float m_scale{ 0.03f };
	float m_alpha{ 0.2f };

public:
	AmbientDustParticle(const DirectX::XMVECTOR& position, const DirectX::XMVECTOR& velocity, double life_time)
		: Particle(position, velocity, life_time) {}

	void Update(double elapsed_time) override;
	ParticleVisual GetVisual() const override;
};

class AmbientDustEmitter : public Emitter
{
private:
	std::mt19937 m_mt{ std::random_device{}() };

public:
	AmbientDustEmitter(size_t capacity, const DirectX::XMVECTOR& position, double particles_per_second, bool is_emmit)
		: Emitter(capacity, position, particles_per_second, is_emmit) {}

protected:
	Particle* createParticle() override;
};

// ---- Lightning Spark (blue-white sparks on lightning strike) ----

class LightningSparkParticle : public Particle
{
private:
	float m_scale{ 0.2f };
	float m_alpha{ 1.0f };

public:
	LightningSparkParticle(const DirectX::XMVECTOR& position, const DirectX::XMVECTOR& velocity, double life_time)
		: Particle(position, velocity, life_time) {}

	void Update(double elapsed_time) override;
	ParticleVisual GetVisual() const override;
};

class LightningSparkEmitter : public Emitter
{
private:
	std::mt19937 m_mt{ std::random_device{}() };

public:
	LightningSparkEmitter(size_t capacity, const DirectX::XMVECTOR& position, double particles_per_second, bool is_emmit)
		: Emitter(capacity, position, particles_per_second, is_emmit) {}

protected:
	Particle* createParticle() override;
};

// ---- Dash Burst (radial blue-white sparks on dash start) ----

class DashBurstEmitter : public Emitter
{
private:
	std::mt19937 m_mt{ std::random_device{}() };

public:
	DashBurstEmitter(size_t capacity, const DirectX::XMVECTOR& position, double particles_per_second, bool is_emmit)
		: Emitter(capacity, position, particles_per_second, is_emmit) {}

protected:
	Particle* createParticle() override;
};

// ---- Health Pickup Burst (green sparks on health pickup collection) ----

class HealthPickupBurstParticle : public Particle
{
private:
	float m_scale{ 0.15f };
	float m_alpha{ 1.0f };

public:
	HealthPickupBurstParticle(const DirectX::XMVECTOR& position, const DirectX::XMVECTOR& velocity, double life_time)
		: Particle(position, velocity, life_time) {}

	void Update(double elapsed_time) override;
	ParticleVisual GetVisual() const override;
};

class HealthPickupBurstEmitter : public Emitter
{
private:
	std::mt19937 m_mt{ std::random_device{}() };

public:
	HealthPickupBurstEmitter(size_t capacity, const DirectX::XMVECTOR& position, double particles_per_second, bool is_emmit)
		: Emitter(capacity, position, particles_per_second, is_emmit) {}

protected:
	Particle* createParticle() override;
};
