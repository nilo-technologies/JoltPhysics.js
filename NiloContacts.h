// Nilo's contact listeners, which run without calling into JS. The contacts buffer is
// single-threaded only: the MT build fires contact callbacks from job threads.
#pragma once

#include "Jolt/Physics/Collision/EstimateCollisionResponse.h"

#include <cstdint>
#include <vector>

namespace nilo {

using namespace JPH;

// Strides baked into post.js via _layoutMeta(); its read order must match this packing.
namespace layout {
    constexpr int addedI32   = 5; // body1, body2, objectLayer1, objectLayer2, isTrigger
    constexpr int addedF32   = 7; // point(xyz), normal(xyz), impulse
    constexpr int removedI32 = 2; // body1, body2
    static_assert(addedI32 == 5 && addedF32 == 7 && removedI32 == 2,
                  "nilo layout strides changed — update NiloContactListener packing and post.js readers");
}

#ifndef __EMSCRIPTEN_PTHREADS__
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

// Persisted contacts are not recorded. The buffer and system must outlive the listener.
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

    // Total normal impulse over the manifold's points (kg m/s).
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
#endif // __EMSCRIPTEN_PTHREADS__

// Unless sliding is allowed, a still, walkable contact stops the character on gentle slopes.
// Set the flag before each ExtendedUpdate, which is when OnContactSolve reads it.
class NiloCharacterContactListener : public CharacterContactListener {
public:
    void SetAllowSliding(bool inAllowSliding) { mAllowSliding = inAllowSliding; }
    bool GetAllowSliding() const { return mAllowSliding; }

    void OnContactSolve(const CharacterVirtual *c, const BodyID &, const SubShapeID &, RVec3Arg,
                        Vec3Arg normal, Vec3Arg contactVelocity, const PhysicsMaterial *,
                        Vec3Arg, Vec3 &ioNewCharacterVelocity) override {
        // ~1 mm/s dead zone, looser than IsNearZero on purpose.
        if (!mAllowSliding && contactVelocity.LengthSq() < 1.0e-6f && !c->IsSlopeTooSteep(normal))
            ioNewCharacterVelocity = Vec3::sZero();
    }

private:
    bool mAllowSliding = false;
};

} // namespace nilo
