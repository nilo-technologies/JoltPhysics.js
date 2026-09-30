// Nilo-specific contact capture. Everything here runs inside PhysicsSystem::Update and
// CharacterVirtual::ExtendedUpdate without calling into JS:
//   - NiloContactListener packs exactly the contact data Nilo consumes into a
//     NiloContactsBuffer, read in bulk from the wasm heap after the step (post.js).
//   - NiloCharacterContactListener applies Nilo's slide prevention natively, from a flag
//     JS sets before the update.
// Kept in its own header (and its own sections of bindings.cpp / post.js) so upstream
// merges rarely touch it. Single-threaded only: Jolt fires contact callbacks from job
// threads in the MT build, which would need the buffer guarded by a mutex.
#pragma once

#include "Jolt/Physics/Collision/EstimateCollisionResponse.h"

#include <cstdint>
#include <vector>

namespace nilo {

using namespace JPH;

// Packed-buffer strides for NiloContactsBuffer. Exposed via _layoutMeta() and baked into
// post.js at build time (__JOLT_LAYOUT__). The per-field read order in post.js still has to
// match the packing order below by hand; the static_assert is a tripwire for stride changes.
namespace layout {
    constexpr int addedI32   = 5; // body1, body2, objectLayer1, objectLayer2, isTrigger
    constexpr int addedF32   = 7; // point(xyz), normal(xyz), impulse
    constexpr int removedI32 = 2; // body1, body2
    static_assert(addedI32 == 5 && addedF32 == 7 && removedI32 == 2,
                  "nilo layout strides changed — update NiloContactListener packing and post.js readers");
}

// Flat wasm-heap tiers of the contacts added and removed during a step. Vectors keep
// their capacity across Clear(), so there is no allocation after warm-up.
class NiloContactsBuffer {
public:
    std::vector<int32_t> mAddedI32, mRemovedI32;
    std::vector<float>   mAddedF32;
    int mAddedCount = 0, mRemovedCount = 0;

    void Clear() {
        mAddedI32.clear(); mAddedF32.clear(); mAddedCount = 0;
        mRemovedI32.clear(); mRemovedCount = 0;
    }
    int GetAddedCount()   const { return mAddedCount; }
    int GetRemovedCount() const { return mRemovedCount; }
    uintptr_t AddedI32Ptr()   const { return (uintptr_t)mAddedI32.data(); }
    uintptr_t AddedF32Ptr()   const { return (uintptr_t)mAddedF32.data(); }
    uintptr_t RemovedI32Ptr() const { return (uintptr_t)mRemovedI32.data(); }
};

// Records each added contact with its averaged world point, normal, object layers, trigger
// flag and Jolt's estimated collision impulse, and each removed contact by its body pair.
// Persisted contacts are not recorded. The buffer and physics system must outlive the listener.
class NiloContactListener : public ContactListener {
public:
    NiloContactListener(NiloContactsBuffer *inBuffer, const PhysicsSystem *inSystem, uint inNumIterations)
        : mBuffer(inBuffer), mSystem(inSystem), mNumIterations(inNumIterations) {}

    void OnContactAdded(const Body &b1, const Body &b2, const ContactManifold &m, ContactSettings &s) override {
        const bool isTrigger = b1.IsSensor() || b2.IsSensor();
        const RVec3 point = AverageContactPoint(m);
        const Vec3 normal = m.mWorldSpaceNormal;
        const float impulse = isTrigger ? 0.0f : EstimateImpulse(b1, b2, m, s);

        mBuffer->mAddedI32.insert(mBuffer->mAddedI32.end(), {
            (int32_t)b1.GetID().GetIndexAndSequenceNumber(),
            (int32_t)b2.GetID().GetIndexAndSequenceNumber(),
            (int32_t)b1.GetObjectLayer(), (int32_t)b2.GetObjectLayer(),
            isTrigger ? 1 : 0 });
        mBuffer->mAddedF32.insert(mBuffer->mAddedF32.end(), {
            (float)point.GetX(), (float)point.GetY(), (float)point.GetZ(),
            normal.GetX(), normal.GetY(), normal.GetZ(),
            impulse });
        mBuffer->mAddedCount++;
    }

    void OnContactRemoved(const SubShapeIDPair &p) override {
        mBuffer->mRemovedI32.insert(mBuffer->mRemovedI32.end(), {
            (int32_t)p.GetBody1ID().GetIndexAndSequenceNumber(),
            (int32_t)p.GetBody2ID().GetIndexAndSequenceNumber() });
        mBuffer->mRemovedCount++;
    }

private:
    static RVec3 AverageContactPoint(const ContactManifold &m) {
        const uint count = (uint)m.mRelativeContactPointsOn1.size();
        if (count == 0) return m.mBaseOffset;
        Vec3 sum = Vec3::sZero();
        for (uint i = 0; i < count; i++) sum += m.mRelativeContactPointsOn1[i];
        return m.mBaseOffset + sum / (float)count;
    }

    // Total normal impulse over the manifold's points (kg m/s); 0 when neither body is dynamic.
    float EstimateImpulse(const Body &b1, const Body &b2, const ContactManifold &m, const ContactSettings &s) const {
        if (!b1.IsDynamic() && !b2.IsDynamic()) return 0.0f;
        CollisionEstimationResult result;
        EstimateCollisionResponse(b1, b2, m, result, s.mCombinedFriction, s.mCombinedRestitution,
                                  mSystem->GetPhysicsSettings().mMinVelocityForRestitution, mNumIterations);
        float total = 0.0f;
        for (const CollisionEstimationResult::Impulse &i : result.mImpulses) total += i.mContactImpulse;
        return total;
    }

    NiloContactsBuffer *mBuffer;
    const PhysicsSystem *mSystem;
    uint mNumIterations;
};

// Slide prevention for one CharacterVirtual: while sliding is not allowed, a contact that is
// not moving and not too steep cancels the character's velocity, so it stands still on gentle
// slopes. JS sets the flag before each ExtendedUpdate (e.g. allowed while moving or airborne).
class NiloCharacterContactListener : public CharacterContactListener {
public:
    void SetAllowSliding(bool inAllowSliding) { mAllowSliding = inAllowSliding; }
    bool GetAllowSliding() const { return mAllowSliding; }

    void OnContactSolve(const CharacterVirtual *c, const BodyID &, const SubShapeID &, RVec3Arg,
                        Vec3Arg normal, Vec3Arg contactVelocity, const PhysicsMaterial *,
                        Vec3Arg, Vec3 &ioNewCharacterVelocity) override {
        if (!mAllowSliding && contactVelocity.LengthSq() < 1.0e-6f && !c->IsSlopeTooSteep(normal))
            ioNewCharacterVelocity = Vec3::sZero();
    }

private:
    bool mAllowSliding = false;
};

} // namespace nilo
