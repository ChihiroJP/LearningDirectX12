// ======================================
// File: game/GameVFXEmitters.cpp
// Purpose: Gameplay VFX particle/emitter implementations.
// Author : LEE CHEE HOW
// Date   : 2026/02/18
// ======================================

#include "GameVFXEmitters.h"
using namespace DirectX;

// ============================================================================
// ObjectiveGlowParticle — gold, floats upward, peaks scale at 50% then shrinks
// ============================================================================

void ObjectiveGlowParticle::Update(double elapsed_time)
{
	float ratio = static_cast<float>(std::min(GetAccumulatedTime() / GetLifeTime(), 1.0));

	// Peak scale at 50% life, then shrink
	if (ratio < 0.5f)
		m_scale = 0.12f + ratio * 0.26f; // 0.12 -> 0.25
	else
		m_scale = 0.25f - (ratio - 0.5f) * 0.5f; // 0.25 -> 0.0

	m_alpha = 1.0f - ratio;

	AddPosition(XMVectorScale(GetVelocity(), static_cast<float>(elapsed_time)));
	// Gentle buoyancy
	AddVelocity(XMVectorScale(XMVectorSet(0.0f, 0.1f, 0.0f, 0.0f), static_cast<float>(elapsed_time)));

	Particle::Update(elapsed_time);
}

ParticleVisual ObjectiveGlowParticle::GetVisual() const
{
	ParticleVisual v{};
	XMStoreFloat3(&v.position, GetPosition());
	v.scale = m_scale;
	v.color = XMFLOAT4{ 1.0f, 0.85f, 0.3f, m_alpha }; // gold
	return v;
}

Particle* ObjectiveGlowEmitter::createParticle()
{
	std::uniform_real_distribution<float> angle_dist{ -XM_PI, XM_PI };
	std::uniform_real_distribution<float> radius_dist{ 1.0f, 2.0f };
	std::uniform_real_distribution<float> speed_dist{ 0.3f, 0.8f };
	std::uniform_real_distribution<float> drift_dist{ -0.1f, 0.1f };
	std::uniform_real_distribution<float> lifetime_dist{ 1.5f, 2.5f };

	float angle = angle_dist(m_mt);
	float radius = radius_dist(m_mt);

	XMVECTOR spawnPos = XMVectorAdd(
		GetPosition(),
		XMVectorSet(cosf(angle) * radius, 0.0f, sinf(angle) * radius, 0.0f)
	);

	float upSpeed = speed_dist(m_mt);
	XMVECTOR velocity = XMVectorSet(
		drift_dist(m_mt),
		upSpeed,
		drift_dist(m_mt),
		0.0f
	);

	return new ObjectiveGlowParticle(spawnPos, velocity, lifetime_dist(m_mt));
}

// ============================================================================
// PickupBurstParticle — bright gold, radial burst, moderate gravity
// ============================================================================

void PickupBurstParticle::Update(double elapsed_time)
{
	float ratio = static_cast<float>(std::min(GetAccumulatedTime() / GetLifeTime(), 1.0));

	m_scale = 0.2f - ratio * 0.18f; // 0.2 -> 0.02
	m_alpha = 1.0f - ratio;

	AddPosition(XMVectorScale(GetVelocity(), static_cast<float>(elapsed_time)));
	AddVelocity(XMVectorScale(XMVectorSet(0.0f, -4.0f, 0.0f, 0.0f), static_cast<float>(elapsed_time)));

	Particle::Update(elapsed_time);
}

ParticleVisual PickupBurstParticle::GetVisual() const
{
	ParticleVisual v{};
	XMStoreFloat3(&v.position, GetPosition());
	v.scale = m_scale;
	// Gold shifting toward white
	float ratio = static_cast<float>(std::min(GetAccumulatedTime() / GetLifeTime(), 1.0));
	float g = 0.95f - ratio * 0.2f; // 0.95 -> 0.75
	float b = 0.5f + ratio * 0.3f;  // 0.5 -> 0.8
	v.color = XMFLOAT4{ 1.0f, g, b, m_alpha };
	return v;
}

Particle* PickupBurstEmitter::createParticle()
{
	std::uniform_real_distribution<float> angle_dist{ -XM_PI, XM_PI };
	std::uniform_real_distribution<float> elev_dist{ 0.3f, 1.0f };
	std::uniform_real_distribution<float> speed_dist{ 3.0f, 6.0f };
	std::uniform_real_distribution<float> lifetime_dist{ 0.4f, 0.8f };

	float angle = angle_dist(m_mt);
	float elevation = elev_dist(m_mt);
	float speed = speed_dist(m_mt);

	XMVECTOR spawnPos = GetPosition();

	XMVECTOR velocity = XMVectorSet(
		cosf(angle) * speed * (1.0f - elevation),
		speed * elevation,
		sinf(angle) * speed * (1.0f - elevation),
		0.0f
	);

	return new PickupBurstParticle(spawnPos, velocity, lifetime_dist(m_mt));
}

// ============================================================================
// DamageFlashParticle — red, fast, strong gravity
// ============================================================================

void DamageFlashParticle::Update(double elapsed_time)
{
	float ratio = static_cast<float>(std::min(GetAccumulatedTime() / GetLifeTime(), 1.0));

	m_scale = 0.35f - ratio * 0.25f; // 0.35 -> 0.10
	m_alpha = 1.0f - ratio;

	AddPosition(XMVectorScale(GetVelocity(), static_cast<float>(elapsed_time)));
	AddVelocity(XMVectorScale(XMVectorSet(0.0f, -9.8f, 0.0f, 0.0f), static_cast<float>(elapsed_time)));

	Particle::Update(elapsed_time);
}

ParticleVisual DamageFlashParticle::GetVisual() const
{
	ParticleVisual v{};
	XMStoreFloat3(&v.position, GetPosition());
	v.scale = m_scale;
	// Red with some orange variation
	float ratio = static_cast<float>(std::min(GetAccumulatedTime() / GetLifeTime(), 1.0));
	float g = 0.15f - ratio * 0.1f; // 0.15 -> 0.05
	v.color = XMFLOAT4{ 1.0f, g, 0.1f, m_alpha };
	return v;
}

Particle* DamageFlashEmitter::createParticle()
{
	std::uniform_real_distribution<float> angle_dist{ -XM_PI, XM_PI };
	std::uniform_real_distribution<float> elev_dist{ 0.2f, 0.8f };
	std::uniform_real_distribution<float> speed_dist{ 3.0f, 6.0f };
	std::uniform_real_distribution<float> lifetime_dist{ 0.3f, 0.7f };

	float angle = angle_dist(m_mt);
	float elevation = elev_dist(m_mt);
	float speed = speed_dist(m_mt);

	XMVECTOR spawnPos = GetPosition();

	XMVECTOR velocity = XMVectorSet(
		cosf(angle) * speed * (1.0f - elevation),
		speed * elevation,
		sinf(angle) * speed * (1.0f - elevation),
		0.0f
	);

	return new DamageFlashParticle(spawnPos, velocity, lifetime_dist(m_mt));
}

// ============================================================================
// DeathSparkParticle — orange->dark red, radial+upward, strong gravity
// ============================================================================

void DeathSparkParticle::Update(double elapsed_time)
{
	float ratio = static_cast<float>(std::min(GetAccumulatedTime() / GetLifeTime(), 1.0));

	m_scale = 0.4f - ratio * 0.3f; // 0.4 -> 0.10
	m_alpha = 1.0f - ratio;

	AddPosition(XMVectorScale(GetVelocity(), static_cast<float>(elapsed_time)));
	AddVelocity(XMVectorScale(XMVectorSet(0.0f, -9.8f, 0.0f, 0.0f), static_cast<float>(elapsed_time)));

	Particle::Update(elapsed_time);
}

ParticleVisual DeathSparkParticle::GetVisual() const
{
	ParticleVisual v{};
	XMStoreFloat3(&v.position, GetPosition());
	v.scale = m_scale;
	// Orange (1.0, 0.6, 0.1) -> dark red (0.7, 0.1, 0.0)
	float ratio = static_cast<float>(std::min(GetAccumulatedTime() / GetLifeTime(), 1.0));
	float r = 1.0f - ratio * 0.3f;  // 1.0 -> 0.7
	float g = 0.6f - ratio * 0.5f;  // 0.6 -> 0.1
	float b = 0.1f - ratio * 0.1f;  // 0.1 -> 0.0
	v.color = XMFLOAT4{ r, g, b, m_alpha };
	return v;
}

Particle* DeathSparkEmitter::createParticle()
{
	std::uniform_real_distribution<float> angle_dist{ -XM_PI, XM_PI };
	std::uniform_real_distribution<float> elev_dist{ 0.4f, 1.0f };
	std::uniform_real_distribution<float> speed_dist{ 3.0f, 7.0f };
	std::uniform_real_distribution<float> lifetime_dist{ 0.4f, 1.0f };

	float angle = angle_dist(m_mt);
	float elevation = elev_dist(m_mt);
	float speed = speed_dist(m_mt);

	XMVECTOR spawnPos = GetPosition();

	XMVECTOR velocity = XMVectorSet(
		cosf(angle) * speed * (1.0f - elevation),
		speed * elevation,
		sinf(angle) * speed * (1.0f - elevation),
		0.0f
	);

	return new DeathSparkParticle(spawnPos, velocity, lifetime_dist(m_mt));
}

// ============================================================================
// DeathSmokeParticle — dark grey-brown, grows, buoyant, slow fade
// ============================================================================

void DeathSmokeParticle::Update(double elapsed_time)
{
	float ratio = static_cast<float>(std::min(GetAccumulatedTime() / GetLifeTime(), 1.0));

	m_scale = 0.4f + ratio * 1.2f; // 0.4 -> 1.6
	m_alpha = 1.0f - ratio;

	AddPosition(XMVectorScale(GetVelocity(), static_cast<float>(elapsed_time)));
	// Buoyancy
	AddVelocity(XMVectorScale(XMVectorSet(0.0f, 0.3f, 0.0f, 0.0f), static_cast<float>(elapsed_time)));

	Particle::Update(elapsed_time);
}

ParticleVisual DeathSmokeParticle::GetVisual() const
{
	ParticleVisual v{};
	XMStoreFloat3(&v.position, GetPosition());
	v.scale = m_scale;
	v.color = XMFLOAT4{ 0.35f, 0.28f, 0.2f, m_alpha * 0.5f }; // dark brown, half-alpha
	return v;
}

Particle* DeathSmokeEmitter::createParticle()
{
	std::uniform_real_distribution<float> offset_dist{ -0.5f, 0.5f };
	std::uniform_real_distribution<float> speed_dist{ 0.3f, 0.8f };
	std::uniform_real_distribution<float> drift_dist{ -0.15f, 0.15f };
	std::uniform_real_distribution<float> lifetime_dist{ 1.0f, 1.5f };

	XMVECTOR spawnPos = XMVectorAdd(
		GetPosition(),
		XMVectorSet(offset_dist(m_mt), 0.0f, offset_dist(m_mt), 0.0f)
	);

	float upSpeed = speed_dist(m_mt);
	XMVECTOR velocity = XMVectorSet(
		drift_dist(m_mt),
		upSpeed,
		drift_dist(m_mt),
		0.0f
	);

	return new DeathSmokeParticle(spawnPos, velocity, lifetime_dist(m_mt));
}

// ============================================================================
// AttackSwingParticle — cyan-white, fast, short-lived arc in front of player
// ============================================================================

void AttackSwingParticle::Update(double elapsed_time)
{
	float ratio = static_cast<float>(std::min(GetAccumulatedTime() / GetLifeTime(), 1.0));

	m_scale = 0.35f - ratio * 0.28f; // 0.35 -> 0.07
	m_alpha = 1.0f - ratio * ratio;  // quadratic fade (stays bright longer)

	AddPosition(XMVectorScale(GetVelocity(), static_cast<float>(elapsed_time)));
	// Light drag to slow particles
	XMVECTOR vel = GetVelocity();
	SetVelocity(XMVectorScale(vel, 1.0f - 3.0f * static_cast<float>(elapsed_time)));

	Particle::Update(elapsed_time);
}

ParticleVisual AttackSwingParticle::GetVisual() const
{
	ParticleVisual v{};
	XMStoreFloat3(&v.position, GetPosition());
	v.scale = m_scale;
	// Cyan-white shifting brighter
	float ratio = static_cast<float>(std::min(GetAccumulatedTime() / GetLifeTime(), 1.0));
	float r = 0.7f + ratio * 0.3f;
	float g = 0.9f;
	float b = 1.0f;
	v.color = XMFLOAT4{ r, g, b, m_alpha };
	return v;
}

Particle* AttackSwingEmitter::createParticle()
{
	std::uniform_real_distribution<float> spread_dist{ -1.0f, 1.0f }; // +/-57 deg wider arc
	std::uniform_real_distribution<float> speed_dist{ 5.0f, 10.0f };
	std::uniform_real_distribution<float> elev_dist{ -0.3f, 0.6f };
	std::uniform_real_distribution<float> lifetime_dist{ 0.12f, 0.25f };
	std::uniform_real_distribution<float> offset_dist{ -0.4f, 0.4f };

	float angle = m_playerYaw + spread_dist(m_mt);
	float speed = speed_dist(m_mt);
	float elev = elev_dist(m_mt);

	XMVECTOR spawnPos = XMVectorAdd(
		GetPosition(),
		XMVectorSet(sinf(m_playerYaw) * 0.8f + offset_dist(m_mt),
		            0.8f + offset_dist(m_mt),
		            cosf(m_playerYaw) * 0.8f + offset_dist(m_mt), 0.0f)
	);

	XMVECTOR velocity = XMVectorSet(
		sinf(angle) * speed,
		elev * speed * 0.3f,
		cosf(angle) * speed,
		0.0f
	);

	return new AttackSwingParticle(spawnPos, velocity, lifetime_dist(m_mt));
}

// ============================================================================
// HitImpactParticle — white-yellow sparks, fast outward, short-lived
// ============================================================================

void HitImpactParticle::Update(double elapsed_time)
{
	float ratio = static_cast<float>(std::min(GetAccumulatedTime() / GetLifeTime(), 1.0));

	m_scale = 0.25f - ratio * 0.2f; // 0.25 -> 0.05
	m_alpha = 1.0f - ratio;

	AddPosition(XMVectorScale(GetVelocity(), static_cast<float>(elapsed_time)));
	AddVelocity(XMVectorScale(XMVectorSet(0.0f, -6.0f, 0.0f, 0.0f), static_cast<float>(elapsed_time)));

	Particle::Update(elapsed_time);
}

ParticleVisual HitImpactParticle::GetVisual() const
{
	ParticleVisual v{};
	XMStoreFloat3(&v.position, GetPosition());
	v.scale = m_scale;
	// White -> warm yellow
	float ratio = static_cast<float>(std::min(GetAccumulatedTime() / GetLifeTime(), 1.0));
	float r = 1.0f;
	float g = 1.0f - ratio * 0.2f;
	float b = 0.9f - ratio * 0.6f;
	v.color = XMFLOAT4{ r, g, b, m_alpha };
	return v;
}

Particle* HitImpactEmitter::createParticle()
{
	std::uniform_real_distribution<float> angle_dist{ -XM_PI, XM_PI };
	std::uniform_real_distribution<float> elev_dist{ 0.3f, 1.0f };
	std::uniform_real_distribution<float> speed_dist{ 4.0f, 8.0f };
	std::uniform_real_distribution<float> lifetime_dist{ 0.15f, 0.3f };

	float angle = angle_dist(m_mt);
	float elevation = elev_dist(m_mt);
	float speed = speed_dist(m_mt);

	XMVECTOR spawnPos = GetPosition();

	XMVECTOR velocity = XMVectorSet(
		cosf(angle) * speed * (1.0f - elevation),
		speed * elevation,
		sinf(angle) * speed * (1.0f - elevation),
		0.0f
	);

	return new HitImpactParticle(spawnPos, velocity, lifetime_dist(m_mt));
}

// ============================================================================
// FootstepDustParticle — brown, small, gentle upward, short-lived
// ============================================================================

void FootstepDustParticle::Update(double elapsed_time)
{
	float ratio = static_cast<float>(std::min(GetAccumulatedTime() / GetLifeTime(), 1.0));

	m_scale = 0.08f + ratio * 0.17f; // 0.08 -> 0.25
	m_alpha = 1.0f - ratio;

	AddPosition(XMVectorScale(GetVelocity(), static_cast<float>(elapsed_time)));

	Particle::Update(elapsed_time);
}

ParticleVisual FootstepDustParticle::GetVisual() const
{
	ParticleVisual v{};
	XMStoreFloat3(&v.position, GetPosition());
	v.scale = m_scale;
	v.color = XMFLOAT4{ 0.6f, 0.5f, 0.35f, m_alpha * 0.4f }; // brown, low alpha
	return v;
}

Particle* FootstepDustEmitter::createParticle()
{
	std::uniform_real_distribution<float> offset_dist{ -0.3f, 0.3f };
	std::uniform_real_distribution<float> up_dist{ 0.2f, 0.5f };
	std::uniform_real_distribution<float> drift_dist{ -0.1f, 0.1f };
	std::uniform_real_distribution<float> lifetime_dist{ 0.5f, 0.8f };

	XMVECTOR spawnPos = XMVectorAdd(
		GetPosition(),
		XMVectorSet(offset_dist(m_mt), 0.0f, offset_dist(m_mt), 0.0f)
	);

	XMVECTOR velocity = XMVectorSet(
		drift_dist(m_mt),
		up_dist(m_mt),
		drift_dist(m_mt),
		0.0f
	);

	return new FootstepDustParticle(spawnPos, velocity, lifetime_dist(m_mt));
}

// ============================================================================
// SprintTrailParticle — blue-white, subtle, short-lived
// ============================================================================

void SprintTrailParticle::Update(double elapsed_time)
{
	float ratio = static_cast<float>(std::min(GetAccumulatedTime() / GetLifeTime(), 1.0));

	m_scale = 0.18f - ratio * 0.14f; // 0.18 -> 0.04
	m_alpha = (1.0f - ratio) * 0.85f; // more visible

	AddPosition(XMVectorScale(GetVelocity(), static_cast<float>(elapsed_time)));

	Particle::Update(elapsed_time);
}

ParticleVisual SprintTrailParticle::GetVisual() const
{
	ParticleVisual v{};
	XMStoreFloat3(&v.position, GetPosition());
	v.scale = m_scale;
	v.color = XMFLOAT4{ 0.6f, 0.85f, 1.0f, m_alpha }; // blue-white
	return v;
}

Particle* SprintTrailEmitter::createParticle()
{
	std::uniform_real_distribution<float> offset_dist{ -0.5f, 0.5f };
	std::uniform_real_distribution<float> up_dist{ 0.1f, 0.6f };
	std::uniform_real_distribution<float> drift_dist{ -0.15f, 0.15f };
	std::uniform_real_distribution<float> lifetime_dist{ 0.35f, 0.6f };

	XMVECTOR spawnPos = XMVectorAdd(
		GetPosition(),
		XMVectorSet(offset_dist(m_mt), 0.0f, offset_dist(m_mt), 0.0f)
	);

	XMVECTOR velocity = XMVectorSet(
		drift_dist(m_mt),
		up_dist(m_mt),
		drift_dist(m_mt),
		0.0f
	);

	return new SprintTrailParticle(spawnPos, velocity, lifetime_dist(m_mt));
}

// ============================================================================
// LandingDustEmitter — reuses FootstepDustParticle but wider, faster spread
// ============================================================================

Particle* LandingDustEmitter::createParticle()
{
	std::uniform_real_distribution<float> angle_dist{ -XM_PI, XM_PI };
	std::uniform_real_distribution<float> speed_dist{ 0.5f, 1.5f };
	std::uniform_real_distribution<float> up_dist{ 0.3f, 0.8f };
	std::uniform_real_distribution<float> lifetime_dist{ 0.4f, 0.7f };

	float angle = angle_dist(m_mt);
	float speed = speed_dist(m_mt);

	XMVECTOR spawnPos = GetPosition();

	XMVECTOR velocity = XMVectorSet(
		cosf(angle) * speed,
		up_dist(m_mt),
		sinf(angle) * speed,
		0.0f
	);

	return new FootstepDustParticle(spawnPos, velocity, lifetime_dist(m_mt));
}

// ============================================================================
// AmbientDustParticle — very subtle, warm brown/gold, slow drift
// ============================================================================

void AmbientDustParticle::Update(double elapsed_time)
{
	float ratio = static_cast<float>(std::min(GetAccumulatedTime() / GetLifeTime(), 1.0));

	if (ratio < 0.2f)
		m_alpha = ratio * 5.0f * 0.25f;
	else
		m_alpha = 0.25f * (1.0f - (ratio - 0.2f) / 0.8f);

	m_scale = 0.03f;

	AddPosition(XMVectorScale(GetVelocity(), static_cast<float>(elapsed_time)));

	Particle::Update(elapsed_time);
}

ParticleVisual AmbientDustParticle::GetVisual() const
{
	ParticleVisual v{};
	XMStoreFloat3(&v.position, GetPosition());
	v.scale = m_scale;
	v.color = XMFLOAT4{ 0.8f, 0.7f, 0.5f, m_alpha };
	return v;
}

Particle* AmbientDustEmitter::createParticle()
{
	std::uniform_real_distribution<float> offset_dist{ -15.0f, 15.0f };
	std::uniform_real_distribution<float> height_dist{ 0.5f, 8.0f };
	std::uniform_real_distribution<float> drift_dist{ -0.05f, 0.05f };
	std::uniform_real_distribution<float> lifetime_dist{ 4.0f, 6.0f };

	XMVECTOR spawnPos = XMVectorAdd(
		GetPosition(),
		XMVectorSet(offset_dist(m_mt), height_dist(m_mt), offset_dist(m_mt), 0.0f)
	);

	XMVECTOR velocity = XMVectorSet(
		drift_dist(m_mt),
		drift_dist(m_mt) * 0.5f,
		drift_dist(m_mt),
		0.0f
	);

	return new AmbientDustParticle(spawnPos, velocity, lifetime_dist(m_mt));
}

// ============================================================================
// LightningSparkParticle — blue-white, fast upward + radial, gravity pull
// ============================================================================

void LightningSparkParticle::Update(double elapsed_time)
{
	float ratio = static_cast<float>(std::min(GetAccumulatedTime() / GetLifeTime(), 1.0));

	m_scale = 0.2f - ratio * 0.15f;
	m_alpha = 1.0f - ratio * ratio;

	AddPosition(XMVectorScale(GetVelocity(), static_cast<float>(elapsed_time)));
	// Gravity pull
	AddVelocity(XMVectorScale(XMVectorSet(0.0f, -12.0f, 0.0f, 0.0f), static_cast<float>(elapsed_time)));

	Particle::Update(elapsed_time);
}

ParticleVisual LightningSparkParticle::GetVisual() const
{
	ParticleVisual v{};
	XMStoreFloat3(&v.position, GetPosition());
	v.scale = m_scale;
	float ratio = static_cast<float>(std::min(GetAccumulatedTime() / GetLifeTime(), 1.0));
	float r = 0.6f + ratio * 0.4f;
	float g = 0.7f + ratio * 0.3f;
	float b = 1.0f;
	v.color = XMFLOAT4{ r, g, b, m_alpha };
	return v;
}

Particle* LightningSparkEmitter::createParticle()
{
	std::uniform_real_distribution<float> angle_dist{ -XM_PI, XM_PI };
	std::uniform_real_distribution<float> speed_dist{ 4.0f, 10.0f };
	std::uniform_real_distribution<float> up_dist{ 4.0f, 12.0f };
	std::uniform_real_distribution<float> lifetime_dist{ 0.2f, 0.5f };
	std::uniform_real_distribution<float> offset_dist{ -0.5f, 0.5f };

	float angle = angle_dist(m_mt);
	float speed = speed_dist(m_mt);

	XMVECTOR spawnPos = XMVectorAdd(
		GetPosition(),
		XMVectorSet(offset_dist(m_mt), 0.0f, offset_dist(m_mt), 0.0f)
	);

	XMVECTOR velocity = XMVectorSet(
		cosf(angle) * speed,
		up_dist(m_mt),
		sinf(angle) * speed,
		0.0f
	);

	return new LightningSparkParticle(spawnPos, velocity, lifetime_dist(m_mt));
}

// ============================================================================
// DashBurstEmitter — radial blue-white burst, reuses SprintTrailParticle
// ============================================================================

Particle* DashBurstEmitter::createParticle()
{
	std::uniform_real_distribution<float> angle_dist{ -XM_PI, XM_PI };
	std::uniform_real_distribution<float> speed_dist{ 3.0f, 7.0f };
	std::uniform_real_distribution<float> up_dist{ 0.2f, 1.0f };
	std::uniform_real_distribution<float> lifetime_dist{ 0.2f, 0.45f };
	std::uniform_real_distribution<float> offset_dist{ -0.3f, 0.3f };

	float angle = angle_dist(m_mt);
	float speed = speed_dist(m_mt);

	XMVECTOR spawnPos = XMVectorAdd(
		GetPosition(),
		XMVectorSet(offset_dist(m_mt), offset_dist(m_mt), offset_dist(m_mt), 0.0f)
	);

	XMVECTOR velocity = XMVectorSet(
		cosf(angle) * speed,
		up_dist(m_mt),
		sinf(angle) * speed,
		0.0f
	);

	return new SprintTrailParticle(spawnPos, velocity, lifetime_dist(m_mt));
}

// ============================================================================
// HealthPickupBurstParticle — green sparks
// ============================================================================

void HealthPickupBurstParticle::Update(double elapsed_time)
{
	float ratio = static_cast<float>(std::min(GetAccumulatedTime() / GetLifeTime(), 1.0));

	m_scale = 0.15f - ratio * 0.12f;
	m_alpha = 1.0f - ratio;

	AddPosition(XMVectorScale(GetVelocity(), static_cast<float>(elapsed_time)));
	AddVelocity(XMVectorScale(XMVectorSet(0.0f, -4.0f, 0.0f, 0.0f), static_cast<float>(elapsed_time)));

	Particle::Update(elapsed_time);
}

ParticleVisual HealthPickupBurstParticle::GetVisual() const
{
	ParticleVisual v{};
	XMStoreFloat3(&v.position, GetPosition());
	v.scale = m_scale;
	float ratio = static_cast<float>(std::min(GetAccumulatedTime() / GetLifeTime(), 1.0));
	float r = 0.3f - ratio * 0.2f;
	float g = 1.0f - ratio * 0.2f;
	float b = 0.3f + ratio * 0.3f;
	v.color = XMFLOAT4{ r, g, b, m_alpha };
	return v;
}

Particle* HealthPickupBurstEmitter::createParticle()
{
	std::uniform_real_distribution<float> angle_dist{ -XM_PI, XM_PI };
	std::uniform_real_distribution<float> elev_dist{ 0.3f, 1.0f };
	std::uniform_real_distribution<float> speed_dist{ 2.0f, 5.0f };
	std::uniform_real_distribution<float> lifetime_dist{ 0.3f, 0.6f };

	float angle = angle_dist(m_mt);
	float elevation = elev_dist(m_mt);
	float speed = speed_dist(m_mt);

	XMVECTOR spawnPos = GetPosition();

	XMVECTOR velocity = XMVectorSet(
		cosf(angle) * speed * (1.0f - elevation),
		speed * elevation,
		sinf(angle) * speed * (1.0f - elevation),
		0.0f
	);

	return new HealthPickupBurstParticle(spawnPos, velocity, lifetime_dist(m_mt));
}
