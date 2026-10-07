// SPDX-FileCopyrightText: 2022 Jorrit Rouwe
// SPDX-License-Identifier: MIT

#include "Jolt/Jolt.h"
#include "Jolt/RegisterTypes.h"
#include "Jolt/Core/Factory.h"
#include "Jolt/Core/JobSystemThreadPool.h"
#include "Jolt/Math/Vec3.h"
#include "Jolt/Math/Quat.h"
#include "Jolt/Geometry/OrientedBox.h"
#include "Jolt/Physics/PhysicsSystem.h"
#include "Jolt/Physics/StateRecorderImpl.h"
#include "Jolt/Physics/Collision/RayCast.h"
#include "Jolt/Physics/Collision/CastResult.h"
#include "Jolt/Physics/Collision/AABoxCast.h"
#include "Jolt/Physics/Collision/ShapeCast.h"
#include "Jolt/Physics/Collision/CollidePointResult.h"
#include "Jolt/Physics/Collision/Shape/SphereShape.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Jolt/Physics/Collision/Shape/CapsuleShape.h"
#include "Jolt/Physics/Collision/Shape/TaperedCapsuleShape.h"
#include "Jolt/Physics/Collision/Shape/CylinderShape.h"
#include "Jolt/Physics/Collision/Shape/TaperedCylinderShape.h"
#include "Jolt/Physics/Collision/Shape/ConvexHullShape.h"
#include "Jolt/Physics/Collision/Shape/StaticCompoundShape.h"
#include "Jolt/Physics/Collision/Shape/MutableCompoundShape.h"
#include "Jolt/Physics/Collision/Shape/ScaledShape.h"
#include "Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h"
#include "Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h"
#include "Jolt/Physics/Collision/Shape/MeshShape.h"
#include "Jolt/Physics/Collision/Shape/HeightFieldShape.h"
#include "Jolt/Physics/Collision/Shape/PlaneShape.h"
#include "Jolt/Physics/Collision/Shape/EmptyShape.h"
#include "Jolt/Physics/Collision/CollisionCollectorImpl.h"
#include "Jolt/Physics/Collision/GroupFilterTable.h"
#include "Jolt/Physics/Collision/CollideShape.h"
#include "Jolt/Physics/Collision/SimShapeFilter.h"
#include "Jolt/Physics/Constraints/FixedConstraint.h"
#include "Jolt/Physics/Constraints/PointConstraint.h"
#include "Jolt/Physics/Constraints/DistanceConstraint.h"
#include "Jolt/Physics/Constraints/HingeConstraint.h"
#include "Jolt/Physics/Constraints/ConeConstraint.h"
#include "Jolt/Physics/Constraints/PathConstraint.h"
#include "Jolt/Physics/Constraints/PathConstraintPathHermite.h"
#include "Jolt/Physics/Constraints/PulleyConstraint.h"
#include "Jolt/Physics/Constraints/SliderConstraint.h"
#include "Jolt/Physics/Constraints/SwingTwistConstraint.h"
#include "Jolt/Physics/Constraints/SixDOFConstraint.h"
#include "Jolt/Physics/Constraints/GearConstraint.h"
#include "Jolt/Physics/Constraints/RackAndPinionConstraint.h"
#include "Jolt/Physics/Body/BodyInterface.h"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Ragdoll/Ragdoll.h"
#include "Jolt/Physics/SoftBody/SoftBodyCreationSettings.h"
#include "Jolt/Physics/SoftBody/SoftBodySharedSettings.h"
#include "Jolt/Physics/SoftBody/SoftBodyShape.h"
#include "Jolt/Physics/SoftBody/SoftBodyMotionProperties.h"
#include "Jolt/Physics/SoftBody/SoftBodyContactListener.h"
#include "Jolt/Physics/SoftBody/SoftBodyManifold.h"
#include "Jolt/Physics/Character/CharacterVirtual.h"
#include "Jolt/Physics/Vehicle/VehicleConstraint.h"
#include "Jolt/Physics/Vehicle/MotorcycleController.h"
#include "Jolt/Physics/Vehicle/TrackedVehicleController.h"
#include "Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h"
#include "Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h"
#include "Jolt/Physics/Collision/ObjectLayerPairFilterTable.h"
#include "Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceMask.h"
#include "Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterMask.h"
#include "Jolt/Physics/Collision/ObjectLayerPairFilterMask.h"
#include "Jolt/Physics/Body/BodyActivationListener.h"
#include "Jolt/Skeleton/SkeletalAnimation.h"
#include "Jolt/Skeleton/SkeletonPose.h"
#include "Jolt/Skeleton/Skeleton.h"

#include <iostream>
#include <malloc.h>
#include <unistd.h>
#include <emscripten/em_asm.h>

using namespace JPH;
using namespace std;

#ifdef JPH_DEBUG_RENDERER
	#include "JoltJS-DebugRenderer.h"
#endif

// Ensure that we use 32-bit object layers
static_assert(sizeof(ObjectLayer) == 4);

// Flat names for nested Jolt types, used by the bindings
using EGroundState = CharacterBase::EGroundState;
using SkeletalAnimationJointState = SkeletalAnimation::JointState;
using SkeletalAnimationKeyframe = SkeletalAnimation::Keyframe;
using SkeletalAnimationAnimatedJoint = SkeletalAnimation::AnimatedJoint;

// Defining ETireFrictionDirection since we cannot pass references to float
enum ETireFrictionDirection
{
	ETireFrictionDirection_Longitudinal,
	ETireFrictionDirection_Lateral
};

// Callback for traces
static void TraceImpl(const char *inFMT, ...)
{
	// Format the message
	va_list list;
	va_start(list, inFMT);
	char buffer[1024];
	vsnprintf(buffer, sizeof(buffer), inFMT, list);

	// Print to the TTY
	cout << buffer << endl;
}

/// A wrapper around the assert failed handler that is compatible with JavaScript
class AssertFailedHandler
{
public:
	virtual					~AssertFailedHandler() = default;
	virtual void			OnAssertFailed(const char *inExpression, const char *inMessage, const char *inFile, uint inLine) = 0;
};

/// Settings to pass to constructor
class JoltSettings
{
public:
	uint					mMaxBodies = 10240;
	uint					mMaxBodyPairs = 65536;
	uint					mMaxContactConstraints = 10240;
	uint					mTempAllocatorSize = 10 * 1024 * 1024;
	uint					mMaxWorkerThreads = 16;
	BroadPhaseLayerInterface *mBroadPhaseLayerInterface = nullptr;
	ObjectVsBroadPhaseLayerFilter *mObjectVsBroadPhaseLayerFilter = nullptr;
	ObjectLayerPairFilter *	mObjectLayerPairFilter = nullptr;
	AssertFailedHandler *	mAssertFailedHandler = nullptr;
};

/// Main API for JavaScript
class JoltInterface
{
public:
	/// Constructor
							JoltInterface(const JoltSettings &inSettings)
	{
		// Install trace handler
		Trace = TraceImpl;

		// Install assert handler
#ifdef JPH_ENABLE_ASSERTS
		sAssertFailedHandler = inSettings.mAssertFailedHandler;
		AssertFailed = [](const char *inExpression, const char *inMessage, const char *inFile, uint inLine)
		{
			// Log the assert
			if (sAssertFailedHandler != nullptr)
				sAssertFailedHandler->OnAssertFailed(inExpression, inMessage != nullptr ? inMessage : "", inFile, inLine);
			else
				cout << inFile << ":" << inLine << ": (" << inExpression << ") " << (inMessage != nullptr? inMessage : "") << endl;

			// No breakpoint
			return false;
		};
#endif // JPH_ENABLE_ASSERTS

		// Create a factory
		Factory::sInstance = new Factory();

		// Register all Jolt physics types
		RegisterTypes();

		// Init temp allocator
		mTempAllocator = new TempAllocatorImpl(inSettings.mTempAllocatorSize);

		// Limit to 16 threads since we limit the webworker thread pool size to this as well
		int num_workers = min<int>(thread::hardware_concurrency() - 1, min<int>(inSettings.mMaxWorkerThreads, 16));
		mJobSystem = new JobSystemThreadPool(cMaxPhysicsJobs, cMaxPhysicsBarriers, num_workers);

		// Check required objects
		if (inSettings.mBroadPhaseLayerInterface == nullptr || inSettings.mObjectVsBroadPhaseLayerFilter == nullptr || inSettings.mObjectLayerPairFilter == nullptr)
			Trace("Error: BroadPhaseLayerInterface, ObjectVsBroadPhaseLayerFilter and ObjectLayerPairFilter must be provided");

		// Store interfaces
		mBroadPhaseLayerInterface = inSettings.mBroadPhaseLayerInterface;
		mObjectVsBroadPhaseLayerFilter = inSettings.mObjectVsBroadPhaseLayerFilter;
		mObjectLayerPairFilter = inSettings.mObjectLayerPairFilter;

		// Init the physics system
		constexpr uint cNumBodyMutexes = 0;
		mPhysicsSystem = new PhysicsSystem();
		mPhysicsSystem->Init(inSettings.mMaxBodies, cNumBodyMutexes, inSettings.mMaxBodyPairs, inSettings.mMaxContactConstraints, *inSettings.mBroadPhaseLayerInterface, *inSettings.mObjectVsBroadPhaseLayerFilter, *inSettings.mObjectLayerPairFilter);
	}

	/// Destructor
							~JoltInterface()
	{
		// Destroy subsystems
		delete mPhysicsSystem;
		delete mBroadPhaseLayerInterface;
		delete mObjectVsBroadPhaseLayerFilter;
		delete mObjectLayerPairFilter;
		delete mJobSystem;
		delete mTempAllocator;
		delete Factory::sInstance;
		Factory::sInstance = nullptr;
		UnregisterTypes();
#ifdef JPH_ENABLE_ASSERTS
		sAssertFailedHandler = nullptr;
#endif
	}

	/// Step the world
	void					Step(float inDeltaTime, int inCollisionSteps)
	{
		mPhysicsSystem->Update(inDeltaTime, inCollisionSteps, mTempAllocator, mJobSystem);
	}

	/// Access to the physics system
	PhysicsSystem *			GetPhysicsSystem()
	{
		return mPhysicsSystem;
	}

	/// Access to the temp allocator
	TempAllocator *			GetTempAllocator()
	{
		return mTempAllocator;
	}

	/// Access the default object layer pair filter
	ObjectLayerPairFilter *GetObjectLayerPairFilter()
	{
		return mObjectLayerPairFilter;
	}

	/// Access the default broadphase layer interface
	BroadPhaseLayerInterface *GetBroadPhaseLayerInterface()
	{
		return mBroadPhaseLayerInterface;
	}

	/// Access the default object vs broadphase layer filter
	ObjectVsBroadPhaseLayerFilter *GetObjectVsBroadPhaseLayerFilter()
	{
		return mObjectVsBroadPhaseLayerFilter;
	}

	/// Get the total reserved memory in bytes
	/// See: https://github.com/emscripten-core/emscripten/blob/7459cab167138419168b5ac5eacf74702d5a3dae/test/core/test_mallinfo.c#L16-L18
	static size_t			sGetTotalMemory()
	{
		return (size_t)EM_ASM_PTR(return HEAP8.length);
	}

	/// Get the amount of free memory in bytes
	/// See: https://github.com/emscripten-core/emscripten/blob/7459cab167138419168b5ac5eacf74702d5a3dae/test/core/test_mallinfo.c#L20-L25
	static size_t			sGetFreeMemory()
	{
		struct mallinfo i = mallinfo();
		uintptr_t total_memory = sGetTotalMemory();
		uintptr_t dynamic_top = (uintptr_t)sbrk(0);
		return total_memory - dynamic_top + i.fordblks;
	}

	/// Access to Body::sFixedToWorld, we can't expose it in a different way
	static Body *			sGetFixedToWorldBody()
	{
		return &Body::sFixedToWorld;
	}

private:
	TempAllocatorImpl *		mTempAllocator = nullptr;
	JobSystemThreadPool *	mJobSystem = nullptr;
	BroadPhaseLayerInterface *mBroadPhaseLayerInterface = nullptr;
	ObjectVsBroadPhaseLayerFilter *mObjectVsBroadPhaseLayerFilter = nullptr;
	ObjectLayerPairFilter *	mObjectLayerPairFilter = nullptr;
	PhysicsSystem *			mPhysicsSystem = nullptr;
#ifdef JPH_ENABLE_ASSERTS
	inline static AssertFailedHandler *sAssertFailedHandler = nullptr;
#endif
};

/// Turns VehicleConstraint's std::function callbacks into virtuals that JS can implement
class VehicleConstraintCallbacksEm
{
public:
	virtual					~VehicleConstraintCallbacksEm() = default;

	void					SetVehicleConstraint(VehicleConstraint &inConstraint)
	{
		inConstraint.SetCombineFriction([this](uint inWheelIndex, float &ioLongitudinalFriction, float &ioLateralFriction, const Body &inBody2, const SubShapeID &inSubShapeID2) {
			ioLongitudinalFriction = GetCombinedFriction(inWheelIndex, ETireFrictionDirection_Longitudinal, ioLongitudinalFriction, inBody2, inSubShapeID2);
			ioLateralFriction = GetCombinedFriction(inWheelIndex, ETireFrictionDirection_Lateral, ioLateralFriction, inBody2, inSubShapeID2);
		});
		inConstraint.SetPreStepCallback([this](VehicleConstraint &inVehicle, const PhysicsStepListenerContext &inContext) {
			OnPreStepCallback(inVehicle, inContext);
		});
		inConstraint.SetPostCollideCallback([this](VehicleConstraint &inVehicle, const PhysicsStepListenerContext &inContext) {
			OnPostCollideCallback(inVehicle, inContext);
		});
		inConstraint.SetPostStepCallback([this](VehicleConstraint &inVehicle, const PhysicsStepListenerContext &inContext) {
			OnPostStepCallback(inVehicle, inContext);
		});
	}

	virtual float			GetCombinedFriction(unsigned int inWheelIndex, ETireFrictionDirection inTireFrictionDirection, float inTireFriction, const Body &inBody2, const SubShapeID &inSubShapeID2) = 0;
	virtual void			OnPreStepCallback(VehicleConstraint &inVehicle, const PhysicsStepListenerContext &inContext) = 0;
	virtual void			OnPostCollideCallback(VehicleConstraint &inVehicle, const PhysicsStepListenerContext &inContext) = 0;
	virtual void			OnPostStepCallback(VehicleConstraint &inVehicle, const PhysicsStepListenerContext &inContext) = 0;
};

/// The tire max impulse callback returns multiple parameters, so we need to store them in a class
class TireMaxImpulseCallbackResult
{
public:
	float					mLongitudinalImpulse;
	float					mLateralImpulse;
};

/// Turns WheeledVehicleController's std::function tire callback into a virtual that JS can implement
class WheeledVehicleControllerCallbacksEm
{
public:
	virtual					~WheeledVehicleControllerCallbacksEm() = default;

	void					SetWheeledVehicleController(WheeledVehicleController &inController)
	{
		inController.SetTireMaxImpulseCallback([this](uint inWheelIndex, float &outLongitudinalImpulse, float &outLateralImpulse, float inSuspensionImpulse, float inLongitudinalFriction, float inLateralFriction, float inLongitudinalSlip, float inLateralSlip, float inDeltaTime) {
			// Pre-fill the structure with default calculated values
			TireMaxImpulseCallbackResult result;
			result.mLongitudinalImpulse = inLongitudinalFriction * inSuspensionImpulse;
			result.mLateralImpulse = inLateralFriction * inSuspensionImpulse;

			OnTireMaxImpulseCallback(inWheelIndex, &result, inSuspensionImpulse, inLongitudinalFriction, inLateralFriction, inLongitudinalSlip, inLateralSlip, inDeltaTime);

			// Read the results
			outLongitudinalImpulse = result.mLongitudinalImpulse;
			outLateralImpulse = result.mLateralImpulse;
		});
	}

	virtual void			OnTireMaxImpulseCallback(uint inWheelIndex, TireMaxImpulseCallbackResult *outResult, float inSuspensionImpulse, float inLongitudinalFriction, float inLateralFriction, float inLongitudinalSlip, float inLateralSlip, float inDeltaTime) = 0;
};
