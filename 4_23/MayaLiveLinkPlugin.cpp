// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RequiredProgramMainCPPInclude.h"
#include "Misc/CommandLine.h"
#include "Async/TaskGraphInterfaces.h"
#include "Modules/ModuleManager.h"
#include "UObject/Object.h"
#include "Misc/ConfigCacheIni.h"

#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "Roles/LiveLinkCameraRole.h"
#include "Roles/LiveLinkCameraTypes.h"
#include "Roles/LiveLinkTransformRole.h"
#include "Roles/LiveLinkTransformTypes.h"
#include "LiveLinkProvider.h"
#include "LiveLinkRefSkeleton.h"
#include "LiveLinkTypes.h"
#include "Misc/OutputDevice.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlankMayaPlugin, Log, All);

IMPLEMENT_APPLICATION(MayaLiveLinkPlugin, "MayaLiveLinkPlugin");


// Maya includes
#ifndef BananaFritters
#define BananaFritters unsigned int
#endif

#define DWORD BananaFritters
#include <maya/MObject.h>
#include <maya/MGlobal.h>
#include <maya/MFnPlugin.h>
#include <maya/MPxCommand.h> //command
#include <maya/MCommandResult.h> //command
#include <maya/MPxNode.h> //node
#include <maya/MFnNumericAttribute.h>
#include <maya/MCallbackIdArray.h>
#include <maya/MEventMessage.h>
#include <maya/MDagMessage.h>
#include <maya/MItDag.h>
#include <maya/MDagPath.h>
#include <maya/MFnDagNode.h>
#include <maya/MMatrix.h>
#include <maya/MTransformationMatrix.h>
#include <maya/MQuaternion.h>
#include <maya/MVector.h>
#include <maya/MFnTransform.h>
#include <maya/MFnIkJoint.h>
#include <maya/MFnCamera.h>
#include <maya/MEulerRotation.h>
#include <maya/MSelectionList.h>
#include <maya/MAnimControl.h>
#include <maya/MTimerMessage.h>
#include <maya/MDGMessage.h>
#include <maya/MNodeMessage.h>
#include <maya/MSceneMessage.h>
#include <maya/M3dView.h>
#include <maya/MUiMessage.h>
#include <maya/MSyntax.h>
#include <maya/MArgDatabase.h>
#undef DWORD

#define MCHECKERROR(STAT,MSG)                   \
    if (!STAT) {                                \
        perror(MSG);                            \
        return MS::kFailure;                    \
    }

#define MREPORTERROR(STAT,MSG)                  \
    if (!STAT) {                                \
        perror(MSG);                            \
    }

class FLiveLinkStreamedSubjectManager;

TSharedPtr<ILiveLinkProvider> LiveLinkProvider;
TSharedPtr<FLiveLinkStreamedSubjectManager> LiveLinkStreamManager;
FDelegateHandle ConnectionStatusChangedHandle;

MCallbackIdArray myCallbackIds;

MSpace::Space G_TransformSpace = MSpace::kTransform;

bool bUEInitialized = false;

// Execute the python command to refresh our UI
void RefreshUI()
{
	MGlobal::executeCommand("MayaLiveLinkRefreshUI");
}

void SetMatrixRow(double* Row, MVector Vec)
{
	Row[0] = Vec.x;
	Row[1] = Vec.y;
	Row[2] = Vec.z;
}

double RadToDeg(double Rad)
{
	const double E_PI = 3.1415926535897932384626433832795028841971693993751058209749445923078164062;
	return (Rad * 180.0) / E_PI;
}

MMatrix GetScale(const MFnIkJoint& Joint)
{
	double Scale[3];
	Joint.getScale(Scale);
	MTransformationMatrix M;
	M.setScale(Scale, G_TransformSpace);
	return M.asMatrix();
}

MMatrix GetRotationOrientation(const MFnIkJoint& Joint, MTransformationMatrix::RotationOrder& RotOrder)
{
	double ScaleOrientation[3];
	Joint.getScaleOrientation(ScaleOrientation, RotOrder);
	MTransformationMatrix M;
	M.setRotation(ScaleOrientation, RotOrder);
	return M.asMatrix();
}

MMatrix GetRotation(const MFnIkJoint& Joint, MTransformationMatrix::RotationOrder& RotOrder)
{
	double Rotation[3];
	Joint.getRotation(Rotation, RotOrder);
	MTransformationMatrix M;
	M.setRotation(Rotation, RotOrder);
	return M.asMatrix();
}

MMatrix GetJointOrientation(const MFnIkJoint& Joint, MTransformationMatrix::RotationOrder& RotOrder)
{
	double JointOrientation[3];
	Joint.getOrientation(JointOrientation, RotOrder);
	MTransformationMatrix M;
	M.setRotation(JointOrientation, RotOrder);
	return M.asMatrix();
}

MMatrix GetTranslation(const MFnIkJoint& Joint)
{
	MVector Translation = Joint.getTranslation(G_TransformSpace);
	MTransformationMatrix M;
	M.setTranslation(Translation, G_TransformSpace);
	return M.asMatrix();
}

FTransform BuildUETransformFromMayaTransform(MMatrix& InMatrix)
{
	MMatrix UnrealSpaceJointMatrix;

	// from FFbxDataConverter::ConvertMatrix
	for (int i = 0; i < 4; ++i)
	{
		double* Row = InMatrix[i];
		if (i == 1)
		{
			UnrealSpaceJointMatrix[i][0] = -Row[0];
			UnrealSpaceJointMatrix[i][1] = Row[1];
			UnrealSpaceJointMatrix[i][2] = -Row[2];
			UnrealSpaceJointMatrix[i][3] = -Row[3];
		}
		else
		{
			UnrealSpaceJointMatrix[i][0] = Row[0];
			UnrealSpaceJointMatrix[i][1] = -Row[1];
			UnrealSpaceJointMatrix[i][2] = Row[2];
			UnrealSpaceJointMatrix[i][3] = Row[3];
		}
	}

	//OutputRotation(FinalJointMatrix);

	MTransformationMatrix UnrealSpaceJointTransform(UnrealSpaceJointMatrix);


	// getRotation is MSpace::kTransform
	double tx, ty, tz, tw;
	UnrealSpaceJointTransform.getRotationQuaternion(tx, ty, tz, tw, MSpace::kWorld);

	FTransform UETrans;
	UETrans.SetRotation(FQuat(tx, ty, tz, tw));

	MVector Translation = UnrealSpaceJointTransform.getTranslation(MSpace::kWorld);
	UETrans.SetTranslation(FVector(Translation.x, Translation.y, Translation.z));

	double Scale[3];
	UnrealSpaceJointTransform.getScale(Scale, MSpace::kWorld);
	UETrans.SetScale3D(FVector((float)Scale[0], (float)Scale[1], (float)Scale[2]));
	return UETrans;
}

void OutputRotation(const MMatrix& M)
{
	MTransformationMatrix TM(M);

	MEulerRotation Euler = TM.eulerRotation();

	FVector V;

	V.X = RadToDeg(Euler[0]);
	V.Y = RadToDeg(Euler[1]);
	V.Z = RadToDeg(Euler[2]);
	MGlobal::displayInfo(*V.ToString());
}

struct IStreamedEntity
{
public:
	virtual ~IStreamedEntity() {};

	virtual bool ShouldDisplayInUI() const { return false; }
	virtual MString GetDisplayText() const = 0;
	virtual bool ValidateSubject() const = 0;
	virtual void RebuildSubjectData() = 0;
	virtual void OnStream(double StreamTime, int32 FrameNumber) = 0;
};

struct FStreamHierarchy
{
	FName JointName;
	MFnIkJoint JointObject;
	int32 ParentIndex;

	FStreamHierarchy() {}

	FStreamHierarchy(const FStreamHierarchy& Other)
		: JointName(Other.JointName)
		, JointObject(Other.JointObject.dagPath())
		, ParentIndex(Other.ParentIndex)
	{}

	FStreamHierarchy(FName InJointName, const MDagPath& InJointPath, int32 InParentIndex)
		: JointName(InJointName)
		, JointObject(InJointPath)
		, ParentIndex(InParentIndex)
	{}
};

/*
*	Utility class for keeping track of user defined attributes from the
*	channel box in Maya.
*/
class FStreamedUserDefinedAttributes
{
protected:
	/*
	*	This struct is used to cache each attribute MPlug for the streamed 
	* 	user defined attributes. The struct contains member variables to
	*	allow dirty-checking to help trigger a refresh of static data
	*	whenever an attribute property is modified in the channel box 
	*	in Maya.
	*/
	struct FAttributeInfo
	{
		// An MPlug is a connection which supports reading values and properties from object attributes
		MPlug Plug;
		inline float Value() { return Plug.asFloat(); }

		// The cached partialName of the attribute
		MString PartialName = "";

		// Determines if an attribute should be streamed via Live Link (see IsPlugRelevantForStream)
		bool bRelevantForStream = false;

		// Determines if an attribute property has been modified by the user
		bool HasDirtyProperties() const
		{
			return (PartialName != Plug.partialName()) || (bRelevantForStream != IsPlugRelevantForStream(Plug));
		}
	};

	// The total number of attributes available on the root joint of the streamed hierarchy
	int NumAttributesOnRootJoint = 0;

	// The sub-set of attributes added by the user to the root joint
	TArray<FAttributeInfo> UserDefinedAttributes;

public:
	/*
	*	Determines if a user defined attribute should be streamed via Live Link.
	*	Note that MPlug is used for querying properties and not MFnAttribute. 
	*	Even though both has the same functions defined, only MPlug replicates the 
	*	behavior of getAttr in MEL. MFnAttribute behaves as attributeQuery in MEL,
	*	which do not report the state of the attribute that is needed.
	*/
	static bool IsPlugRelevantForStream(MPlug Plug)
	{
		if (Plug.isLocked())
		{
			return false;
		}
		else
		{
			return Plug.isKeyable() || Plug.isChannelBoxFlagSet();
		}
	}

	/*
	*	Returns the number of custom user defined attributes on the joint.
	*	Attributes are index-based and custom attributes are added at the
	*	end of the range. User defined attributes are marked isDynamic().
	*/
	int NumUserDefinedAttributesOnJoint(MFnIkJoint& Joint)
	{
		int UserDefinedAttributeCount = 0;
		int LastAttributeIndex = Joint.attributeCount() - 1;
		for (int i = LastAttributeIndex; i >= 0; i--)
		{
			bool bIsUserDefined = static_cast<MFnAttribute>(Joint.attribute(i)).isDynamic();
			if (!bIsUserDefined) 
			{
				// There are no more dynamic / user defined attributes in the range
				break;
			}

			UserDefinedAttributeCount++;
		}

		return UserDefinedAttributeCount;
	}

	bool HasAnyAttributeChangedProperties()
	{
		for (auto& Attribute : UserDefinedAttributes)
		{
			if (Attribute.HasDirtyProperties())
			{
				return true;
			}
		}

		return false;
	}

	/*
	*	Determine if any of the attributes on the root joint has changed since last update.
	*/
	bool HasAttributeListChanged(TArray<FStreamHierarchy>& JointsToStream)
	{
		if (JointsToStream.Num() == 0)
		{
			// We have defined attributes, but no joints to stream. 
			// Something is off, trigger "changed".
			return (UserDefinedAttributes.Num() > 0);
		}

		MFnIkJoint& RootInStream = JointsToStream[0].JointObject;
		if (NumAttributesOnRootJoint != RootInStream.attributeCount())
		{
			return true;
		}
		else
		{
			bool bNumUserAttributesChanged = (UserDefinedAttributes.Num() != NumUserDefinedAttributesOnJoint(RootInStream));
			return bNumUserAttributesChanged || HasAnyAttributeChangedProperties();
		}
	}

	void CopyValuesToFrameData(FLiveLinkAnimationFrameData& AnimationFrameData)
	{
		AnimationFrameData.PropertyValues.SetNum(0);
		for (auto& Attribute : UserDefinedAttributes)
		{
			if (Attribute.bRelevantForStream)
			{
				AnimationFrameData.PropertyValues.Add(Attribute.Value());
				//MGlobal::displayInfo(MString("LiveLink stream: ") + Attribute.PartialName + ": " + AnimationFrameData.PropertyValues.Last());
			}
		}
	}

	/*
	*	Finds all 
	*/
	void UpdateStaticData(TArray<FStreamHierarchy>& JointsToStream, FLiveLinkSkeletonStaticData& SkeletonStaticData)
	{
		NumAttributesOnRootJoint = 0;
		UserDefinedAttributes.SetNum(0);
		SkeletonStaticData.PropertyNames.SetNum(0);

		if (JointsToStream.Num() > 0)
		{
			MFnIkJoint& RootInStream = JointsToStream[0].JointObject;
			int NumUserAttributes = NumUserDefinedAttributesOnJoint(RootInStream);
			if (NumUserAttributes == 0)
			{
				return; // because there is nothing to stream
			}

			// Determine range for user defined attributes
			NumAttributesOnRootJoint = RootInStream.attributeCount();
			int LastAttributeId = NumAttributesOnRootJoint - 1;
			int FirstAttributeId = NumAttributesOnRootJoint - NumUserAttributes;

			// Attempt to access each user defined attribute and add each to the stream
			MStatus FindPlugStatus;
			for (int i = FirstAttributeId; i <= LastAttributeId; i++)
			{
				MFnAttribute Attribute = RootInStream.attribute(i);
				MPlug NewPlug = RootInStream.findPlug(Attribute.object(), FindPlugStatus);
				if (FindPlugStatus == MStatus::kSuccess)
				{
					FAttributeInfo NewAttribute{NewPlug, NewPlug.partialName(), IsPlugRelevantForStream(NewPlug)};
					UserDefinedAttributes.Add(NewAttribute);
					if (NewAttribute.bRelevantForStream)
					{
						SkeletonStaticData.PropertyNames.Add(NewAttribute.PartialName.asChar());
					}
				}
			}
		}

		// Enable for print debug
		//for (auto& Attribute : Attributes)
		//{
		//	MGlobal::displayInfo(MString("LiveLink UpdateStaticData: ") + Attribute.PartialName + (Attribute.bRelevantForStream? "" : " (not streamed)"));
		//}
	}
};

struct FLiveLinkStreamedJointHeirarchySubject : IStreamedEntity
{
	FStreamedUserDefinedAttributes StreamedAttributes;

	FLiveLinkStreamedJointHeirarchySubject(FName InSubjectName, MDagPath InRootPath)
		: SubjectName(InSubjectName)
		, RootDagPath(InRootPath)
	{}

	~FLiveLinkStreamedJointHeirarchySubject()
	{
		if (LiveLinkProvider.IsValid())
		{
			LiveLinkProvider->RemoveSubject(SubjectName);
		}
	}

	virtual bool ShouldDisplayInUI() const { return true; }
	virtual MString GetDisplayText() const { return MString("Character: ") + MString(*SubjectName.ToString()) + " ( " + RootDagPath.fullPathName() + " )"; }

	virtual bool ValidateSubject() const
	{
		MStatus Status;
		bool bIsValid = RootDagPath.isValid(&Status);

		TCHAR* StatusMessage = TEXT("Unset");

		if (Status == MS::kSuccess)
		{
			StatusMessage = TEXT("Success");
		}
		else if (Status == MS::kFailure)
		{
			StatusMessage = TEXT("Failure");
		}
		else
		{
			StatusMessage = TEXT("Other");
		}

		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Testing %s for removal Path:%s Valid:%s Status:%s\n"), *SubjectName.ToString(), RootDagPath.fullPathName().asWChar(), bIsValid ? TEXT("true") : TEXT("false"), StatusMessage);
		if (Status != MS::kFailure && bIsValid)
		{
			//Path checks out as valid
			MFnIkJoint Joint(RootDagPath, &Status);

			MVector returnvec = Joint.getTranslation(MSpace::kWorld, &Status);
			if (Status == MS::kSuccess)
			{
				StatusMessage = TEXT("Success");
			}
			else if (Status == MS::kFailure)
			{
				StatusMessage = TEXT("Failure");
			}
			else
			{
				StatusMessage = TEXT("Other");
			}

			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("\tTesting %s for removal Path:%s Valid:%s Status:%s\n"), *SubjectName.ToString(), RootDagPath.fullPathName().asWChar(), bIsValid ? TEXT("true") : TEXT("false"), StatusMessage);
		}
		return bIsValid;
	}

	virtual void RebuildSubjectData()
	{
		JointsToStream.Reset();

		FLiveLinkStaticDataStruct StaticData(FLiveLinkSkeletonStaticData::StaticStruct());
		FLiveLinkSkeletonStaticData& AnimationData = *StaticData.Cast<FLiveLinkSkeletonStaticData>();

		MItDag::TraversalType traversalType = MItDag::kBreadthFirst;
		MFn::Type filter = MFn::kJoint;

		MStatus status;
		MItDag JointIterator;
		JointIterator.reset(RootDagPath, MItDag::kDepthFirst, MFn::kJoint);

		//Build Hierarchy
		TArray<int32> ParentIndexStack;
		ParentIndexStack.SetNum(100, false);

		int32 Index = 0;

		for (; !JointIterator.isDone(); JointIterator.next())
		{
			uint32 Depth = JointIterator.depth();
			if (Depth >= (uint32)ParentIndexStack.Num())
			{
				ParentIndexStack.SetNum(Depth + 1);
			}
			ParentIndexStack[Depth] = Index++;

			int32 ParentIndex = Depth == 0 ? -1 : ParentIndexStack[Depth - 1];

			MDagPath JointPath;
			status = JointIterator.getPath(JointPath);
			MFnIkJoint JointObject(JointPath);

			//MGlobal::displayInfo(MString("Iter: ") + JointPath.fullPathName() + JointIterator.depth());

			FName JointName(JointObject.name().asChar());

			JointsToStream.Add(FStreamHierarchy(JointName, JointPath, ParentIndex));
			AnimationData.BoneNames.Add(JointName);
			AnimationData.BoneParents.Add(ParentIndex);
		}

		StreamedAttributes.UpdateStaticData(JointsToStream, AnimationData);
		LiveLinkProvider->UpdateSubjectStaticData(SubjectName, ULiveLinkAnimationRole::StaticClass(), MoveTemp(StaticData));
	}

	virtual void OnStream(double StreamTime, int32 FrameNumber)
	{
		// The user can add/remove/lock attributes in the channel box
		// after adding the subject. Trigger a rebuild if that happens.
		if (StreamedAttributes.HasAttributeListChanged(JointsToStream))
		{
			RebuildSubjectData();
		}

		FLiveLinkFrameDataStruct FrameData(FLiveLinkAnimationFrameData::StaticStruct());
		FLiveLinkAnimationFrameData& AnimationData = *FrameData.Cast<FLiveLinkAnimationFrameData>();

		AnimationData.Transforms.Reserve(JointsToStream.Num());

		TArray<MMatrix> InverseScales;
		InverseScales.Reserve(JointsToStream.Num());

		for (int32 Idx = 0; Idx < JointsToStream.Num(); ++Idx)
		{
			const FStreamHierarchy& H = JointsToStream[Idx];

			MTransformationMatrix::RotationOrder RotOrder = H.JointObject.rotationOrder();

			MMatrix JointScale = GetScale(H.JointObject);
			InverseScales.Add(JointScale.inverse());

			MMatrix ParentInverseScale = (H.ParentIndex == -1) ? MMatrix::identity : InverseScales[H.ParentIndex];

			MMatrix MayaSpaceJointMatrix = JointScale *
				GetRotationOrientation(H.JointObject, RotOrder) *
				GetRotation(H.JointObject, RotOrder) *
				GetJointOrientation(H.JointObject, RotOrder) *
				ParentInverseScale *
				GetTranslation(H.JointObject);

			AnimationData.Transforms.Add(BuildUETransformFromMayaTransform(MayaSpaceJointMatrix));
		}

		AnimationData.WorldTime = StreamTime;
		StreamedAttributes.CopyValuesToFrameData(AnimationData);
		LiveLinkProvider->UpdateSubjectFrameData(SubjectName, MoveTemp(FrameData));
	}

private:
	FName SubjectName;
	MDagPath RootDagPath;

	TArray<FStreamHierarchy> JointsToStream;
};

struct FLiveLinkBaseCameraStreamedSubject : public IStreamedEntity
{
public:
	FLiveLinkBaseCameraStreamedSubject(FName InSubjectName) : SubjectName(InSubjectName) {}

	~FLiveLinkBaseCameraStreamedSubject()
	{
		if (LiveLinkProvider.IsValid())
		{
			LiveLinkProvider->RemoveSubject(SubjectName);
		}
	}

	virtual bool ValidateSubject() const { return true; }

	virtual void RebuildSubjectData()
	{
		FLiveLinkStaticDataStruct StaticData(FLiveLinkCameraStaticData::StaticStruct());
		FLiveLinkCameraStaticData& CameraData = *StaticData.Cast<FLiveLinkCameraStaticData>();
		CameraData.bIsFieldOfViewSupported = true;
		CameraData.bIsAspectRatioSupported = true;
		CameraData.bIsFocalLengthSupported = true;
		CameraData.bIsProjectionModeSupported = true;
		LiveLinkProvider->UpdateSubjectStaticData(SubjectName, ULiveLinkCameraRole::StaticClass(), MoveTemp(StaticData));
	}

	void StreamCamera(MDagPath CameraPath, double StreamTime, int32 FrameNumber)
	{
		MStatus Status;
		bool bIsValid = CameraPath.isValid(&Status);

		if (bIsValid && Status == MStatus::kSuccess)
		{
			MFnCamera C(CameraPath);

			MPoint EyeLocation = C.eyePoint(MSpace::kWorld);

			MMatrix CameraTransformMatrix;
			SetMatrixRow(CameraTransformMatrix[0], C.rightDirection(MSpace::kWorld));
			SetMatrixRow(CameraTransformMatrix[1], C.viewDirection(MSpace::kWorld));
			SetMatrixRow(CameraTransformMatrix[2], C.upDirection(MSpace::kWorld));
			SetMatrixRow(CameraTransformMatrix[3], EyeLocation);

			FLiveLinkFrameDataStruct FrameData(FLiveLinkCameraFrameData::StaticStruct());
			FLiveLinkCameraFrameData& CameraData = *FrameData.Cast<FLiveLinkCameraFrameData>();

			CameraData.FieldOfView = C.horizontalFieldOfView();
			CameraData.AspectRatio = C.aspectRatio();
			CameraData.FocalLength = C.focalLength();
			CameraData.ProjectionMode = C.isOrtho() ? ELiveLinkCameraProjectionMode::Orthographic : ELiveLinkCameraProjectionMode::Perspective;

			CameraData.Transform = BuildUETransformFromMayaTransform(CameraTransformMatrix);
			// Convert Maya Camera orientation to Unreal
			CameraData.Transform.SetRotation(CameraData.Transform.GetRotation() * FRotator(0.f, -90.f, 0.f).Quaternion());
			CameraData.WorldTime = StreamTime;

			LiveLinkProvider->UpdateSubjectFrameData(SubjectName, MoveTemp(FrameData));
		}
	}

protected:
	FName  SubjectName;
	static TArray<FName> ActiveCameraBoneNames;
	static TArray<int32> ActiveCameraBoneParents;
};

TArray<FName> FLiveLinkBaseCameraStreamedSubject::ActiveCameraBoneNames = { FName("root") };
TArray<int32> FLiveLinkBaseCameraStreamedSubject::ActiveCameraBoneParents = { -1 };

struct FLiveLinkStreamedActiveCamera : public FLiveLinkBaseCameraStreamedSubject
{
public:
	FLiveLinkStreamedActiveCamera() : FLiveLinkBaseCameraStreamedSubject(ActiveCameraName) {}

	MDagPath CurrentActiveCameraDag;

	virtual MString GetDisplayText() const { return MString(); }

	virtual void OnStream(double StreamTime, int32 FrameNumber)
	{
		MStatus Status;
		M3dView ActiveView = M3dView::active3dView(&Status);
		if (Status == MStatus::kSuccess)
		{
			MDagPath CameraDag;
			if (ActiveView.getCamera(CameraDag) == MStatus::kSuccess)
			{
				CurrentActiveCameraDag = CameraDag;
			}
		}

		StreamCamera(CurrentActiveCameraDag, StreamTime, FrameNumber);
	}

private:
	static FName ActiveCameraName;
};

struct FLiveLinkStreamedCameraSubject : FLiveLinkBaseCameraStreamedSubject
{
public:
	FLiveLinkStreamedCameraSubject(FName InSubjectName, MDagPath InDagPath) : FLiveLinkBaseCameraStreamedSubject(InSubjectName), CameraPath(InDagPath) {}

	virtual bool ShouldDisplayInUI() const { return true; }
	virtual MString GetDisplayText() const { return MString("Camera: ") + *SubjectName.ToString() + " ( " + CameraPath.fullPathName() + " )"; }

	virtual void OnStream(double StreamTime, int32 FrameNumber)
	{
		StreamCamera(CameraPath, StreamTime, FrameNumber);
	}

private:
	MDagPath CameraPath;
};

FName FLiveLinkStreamedActiveCamera::ActiveCameraName("EditorActiveCamera");

struct FLiveLinkStreamedPropSubject : IStreamedEntity
{
public:
	FLiveLinkStreamedPropSubject(FName InSubjectName, MDagPath InRootPath)
		: SubjectName(InSubjectName)
		, RootDagPath(InRootPath)
	{}

	~FLiveLinkStreamedPropSubject()
	{
		if (LiveLinkProvider.IsValid())
		{
			LiveLinkProvider->RemoveSubject(SubjectName);
		}
	}

	virtual bool ShouldDisplayInUI() const { return true; }
	virtual MString GetDisplayText() const { return MString("Prop: ") + MString(*SubjectName.ToString()) + " ( " + RootDagPath.fullPathName() + " )"; }

	virtual bool ValidateSubject() const { return true; }

	virtual void RebuildSubjectData()
	{
		FLiveLinkStaticDataStruct StaticData(FLiveLinkTransformStaticData::StaticStruct());
		LiveLinkProvider->UpdateSubjectStaticData(SubjectName, ULiveLinkTransformRole::StaticClass(), MoveTemp(StaticData));
	}

	virtual void OnStream(double StreamTime, int32 FrameNumber)
	{
		MFnTransform TransformNode(RootDagPath);

		MMatrix Transform = TransformNode.transformation().asMatrix();

		FLiveLinkFrameDataStruct FrameData(FLiveLinkTransformFrameData::StaticStruct());
		FLiveLinkTransformFrameData& TransformData = *FrameData.Cast<FLiveLinkTransformFrameData>();

		// Convert Maya Camera orientation to Unreal
		TransformData.Transform = BuildUETransformFromMayaTransform(Transform);
		TransformData.WorldTime = StreamTime;
		LiveLinkProvider->UpdateSubjectFrameData(SubjectName, MoveTemp(FrameData));
	}

private:
	FName SubjectName;
	MDagPath RootDagPath;
	bool bHasBeenRebuild;

	static TArray<FName> PropBoneNames;
	static TArray<int32> PropBoneParents;

};

TArray<FName> FLiveLinkStreamedPropSubject::PropBoneNames = { FName("root") };
TArray<int32> FLiveLinkStreamedPropSubject::PropBoneParents = { -1 };

class FLiveLinkStreamedSubjectManager
{
private:
	TArray<TSharedPtr<IStreamedEntity>> Subjects;

	void ValidateSubjects()
	{
		Subjects.RemoveAll([](const TSharedPtr<IStreamedEntity>& Item)
			{
				return !Item->ValidateSubject();
			});
		RefreshUI();
	}

public:

	FLiveLinkStreamedSubjectManager()
	{
		Reset();
	}

	void GetSubjectEntries(TArray<MString>& Entries) const
	{
		for (const TSharedPtr<IStreamedEntity>& Subject : Subjects)
		{
			if (Subject->ShouldDisplayInUI())
			{
				Entries.Add(Subject->GetDisplayText());
			}
		}
	}

	template<class SubjectType, typename... ArgsType>
	TSharedPtr<SubjectType> AddSubjectOfType(ArgsType&& ... Args)
	{
		TSharedPtr<SubjectType> Subject = MakeShareable(new SubjectType(Args...));

		Subject->RebuildSubjectData();

		int32 FrameNumber = MAnimControl::currentTime().value();
		Subject->OnStream(FPlatformTime::Seconds(), FrameNumber);

		Subjects.Add(Subject);
		return Subject;
	}

	void AddJointHeirarchySubject(FName SubjectName, MDagPath RootPath)
	{
		AddSubjectOfType<FLiveLinkStreamedJointHeirarchySubject>(SubjectName, RootPath);
	}

	void AddCameraSubject(FName SubjectName, MDagPath RootPath)
	{
		AddSubjectOfType<FLiveLinkStreamedCameraSubject>(SubjectName, RootPath);
	}

	void AddPropSubject(FName SubjectName, MDagPath RootPath)
	{
		AddSubjectOfType<FLiveLinkStreamedPropSubject>(SubjectName, RootPath);
	}

	void RemoveSubject(MString SubjectToRemove)
	{
		for (int32 Index = Subjects.Num() - 1; Index >= 0; --Index)
		{
			if (Subjects[Index]->ShouldDisplayInUI())
			{
				if (Subjects[Index]->GetDisplayText() == SubjectToRemove)
				{
					Subjects.RemoveAt(Index);
					break;
				}
			}
		}
	}

	void Reset()
	{
		Subjects.Reset();
		AddSubjectOfType<FLiveLinkStreamedActiveCamera>();
	}

	void RebuildSubjects()
	{
		ValidateSubjects();
		for (const TSharedPtr<IStreamedEntity>& Subject : Subjects)
		{
			Subject->RebuildSubjectData();
		}
	}

	void StreamSubjects() const
	{
		double StreamTime = FPlatformTime::Seconds();
		int32 FrameNumber = MAnimControl::currentTime().value();

		for (const TSharedPtr<IStreamedEntity>& Subject : Subjects)
		{
			Subject->OnStream(StreamTime, FrameNumber);
		}
	}
};

const MString LiveLinkSubjectsCommandName("LiveLinkSubjects");

class LiveLinkSubjectsCommand : public MPxCommand
{
public:
	static void		cleanup() {}
	static void* creator() { return new LiveLinkSubjectsCommand(); }

	MStatus			doIt(const MArgList& args)
	{
		TArray<MString> SubjectEntries;
		LiveLinkStreamManager->GetSubjectEntries(SubjectEntries);

		for (const MString& Entry : SubjectEntries)
		{
			appendToResult(Entry);
		}

		return MS::kSuccess;
	}
};

const MString LiveLinkAddSubjectCommandName("LiveLinkAddSubject");

class LiveLinkAddSubjectCommand : public MPxCommand
{
public:
	static void		cleanup() {}
	static void* creator() { return new LiveLinkAddSubjectCommand(); }

	MStatus			doIt(const MArgList& args)
	{
		MSyntax Syntax;
		Syntax.addArg(MSyntax::kString);

		MArgDatabase argData(Syntax, args);

		MString Name;
		argData.getCommandArgument(0, Name);

		FName SubjectFName(Name.asChar());

		MSelectionList selected;
		MGlobal::getActiveSelectionList(selected);

		// Find selected joint
		for (unsigned int i = 0; i < selected.length(); ++i)
		{
			MObject obj;

			selected.getDependNode(i, obj);

			if (obj.hasFn(MFn::kJoint))
			{
				MFnIkJoint JointObject(obj);
				MDagPath Path;
				JointObject.getPath(Path);
				LiveLinkStreamManager->AddJointHeirarchySubject(SubjectFName, Path);
			}
			else if (obj.hasFn(MFn::kCamera))
			{
				MFnCamera CameraObject(obj);
				MDagPath Path;
				CameraObject.getPath(Path);
				LiveLinkStreamManager->AddCameraSubject(SubjectFName, Path);
			}
			else if (obj.hasFn(MFn::kTransform))
			{
				MFnTransform TransformNode(obj);
				MDagPath Path;
				TransformNode.getPath(Path);
				LiveLinkStreamManager->AddPropSubject(SubjectFName, Path);
			}
		}

		MGlobal::displayInfo(MString("LiveLinkAddSubjectCommand ") + Name);
		return MS::kSuccess;
	}
};

const MString LiveLinkRemoveSubjectCommandName("LiveLinkRemoveSubject");

class LiveLinkRemoveSubjectCommand : public MPxCommand
{
public:
	static void		cleanup() {}
	static void* creator() { return new LiveLinkRemoveSubjectCommand(); }

	MStatus			doIt(const MArgList& args)
	{
		MSyntax Syntax;
		Syntax.addArg(MSyntax::kString);

		MArgDatabase argData(Syntax, args);

		MString SubjectToRemove;
		argData.getCommandArgument(0, SubjectToRemove);

		LiveLinkStreamManager->RemoveSubject(SubjectToRemove);

		return MS::kSuccess;
	}
};

const MString LiveLinkConnectionStatusCommandName("LiveLinkConnectionStatus");

class LiveLinkConnectionStatusCommand : public MPxCommand
{
public:
	static void		cleanup() {}
	static void* creator() { return new LiveLinkConnectionStatusCommand(); }

	MStatus			doIt(const MArgList& args)
	{
		MString ConnectionStatus("No Provider (internal error)");
		bool bConnection = false;

		if (LiveLinkProvider.IsValid())
		{
			if (LiveLinkProvider->HasConnection())
			{
				ConnectionStatus = "Connected";
				bConnection = true;
			}
			else
			{
				ConnectionStatus = "No Connection";
			}
		}

		appendToResult(ConnectionStatus);
		appendToResult(bConnection);

		return MS::kSuccess;
	}
};

void OnForceChange(MTime& time, void* clientData)
{
	LiveLinkStreamManager->StreamSubjects();
}

class FMayaOutputDevice : public FOutputDevice
{
public:
	FMayaOutputDevice() : bAllowLogVerbosity(false) {}

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category) override
	{
		if ((bAllowLogVerbosity && Verbosity <= ELogVerbosity::Log) || (Verbosity <= ELogVerbosity::Display))
		{
			MGlobal::displayInfo(V);
		}
	}

private:

	bool bAllowLogVerbosity;

};

void OnScenePreOpen(void* client)
{
	LiveLinkStreamManager->Reset();
	RefreshUI();
}

void OnSceneOpen(void* client)
{
	//BuildStreamHierarchyData();
}

void AllDagChangesCallback(
	MDagMessage::DagMessage msgType,
	MDagPath& child,
	MDagPath& parent,
	void* clientData)
{
	LiveLinkStreamManager->RebuildSubjects();
}

void OnConnectionStatusChanged()
{
	MGlobal::executeCommand("MayaLiveLinkRefreshConnectionUI");
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TMap<uintptr_t, MCallbackId> PostRenderCallbackIds;
TMap<uintptr_t, MCallbackId> ViewportDeletedCallbackIds;

void OnPostRenderViewport(const MString& str, void* ClientData)
{
	LiveLinkStreamManager->StreamSubjects();
}

void OnViewportClosed(void* ClientData)
{
	uintptr_t ViewIndex = reinterpret_cast<uintptr_t>(ClientData);

	MMessage::removeCallback(PostRenderCallbackIds[ViewIndex]);
	PostRenderCallbackIds.Remove(ViewIndex);

	MMessage::removeCallback(ViewportDeletedCallbackIds[ViewIndex]);
	ViewportDeletedCallbackIds.Remove(ViewIndex);
}

void ClearViewportCallbacks()
{
	for (TPair<uintptr_t, MCallbackId>& Pair : PostRenderCallbackIds)
	{
		MMessage::removeCallback(Pair.Value);
	}
	PostRenderCallbackIds.Reset();

	for (TPair<uintptr_t, MCallbackId>& Pair : ViewportDeletedCallbackIds)
	{
		MMessage::removeCallback(Pair.Value);
	}
	ViewportDeletedCallbackIds.Reset();
}

MStatus RefreshViewportCallbacks()
{
	MStatus ExitStatus;

	if (int(M3dView::numberOf3dViews()) != PostRenderCallbackIds.Num())
	{
		ClearViewportCallbacks();

		static MString ListEditorPanelsCmd = "gpuCacheListModelEditorPanels";

		MStringArray EditorPanels;
		ExitStatus = MGlobal::executeCommand(ListEditorPanelsCmd, EditorPanels);
		MCHECKERROR(ExitStatus, "gpuCacheListModelEditorPanels");

		if (ExitStatus == MStatus::kSuccess)
		{
			for (uintptr_t i = 0; i < EditorPanels.length(); ++i)
			{
				MStatus Status;
				MCallbackId CallbackId = MUiMessage::add3dViewPostRenderMsgCallback(EditorPanels[i], OnPostRenderViewport, NULL, &Status);

				MREPORTERROR(Status, "MUiMessage::add3dViewPostRenderMsgCallback()");

				if (Status != MStatus::kSuccess)
				{
					ExitStatus = MStatus::kFailure;
					continue;
				}

				PostRenderCallbackIds.Add(i, CallbackId);

				CallbackId = MUiMessage::addUiDeletedCallback(EditorPanels[i], OnViewportClosed, reinterpret_cast<void*>(i), &Status);

				MREPORTERROR(Status, "MUiMessage::addUiDeletedCallback()");

				if (Status != MStatus::kSuccess)
				{
					ExitStatus = MStatus::kFailure;
					continue;
				}
				ViewportDeletedCallbackIds.Add(i, CallbackId);
			}
		}
	}

	return ExitStatus;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void OnInterval(float elapsedTime, float lastTime, void* clientData)
{
	//No good way to check for new views being created, so just periodically refresh our list
	RefreshViewportCallbacks();

	OnConnectionStatusChanged();

	FTicker::GetCoreTicker().Tick(elapsedTime);
}

/**
* This function is called by Maya when the plugin becomes loaded
*
* @param	MayaPluginObject	The Maya object that represents our plugin
*
* @return	MS::kSuccess if everything went OK and the plugin is ready to use
*/
DLLEXPORT MStatus initializePlugin(MObject MayaPluginObject)
{
	if (!bUEInitialized)
	{
		GEngineLoop.PreInit(TEXT("MayaLiveLinkPlugin -Messaging"));
		ProcessNewlyLoadedUObjects();

		// ensure target platform manager is referenced early as it must be created on the main thread
		GetTargetPlatformManager();

		// Tell the module manager is may now process newly-loaded UObjects when new C++ modules are loaded
		FModuleManager::Get().StartProcessingNewlyLoadedObjects();

		FModuleManager::Get().LoadModule(TEXT("UdpMessaging"));

		GLog->TearDown(); //clean up existing output devices
		GLog->AddOutputDevice(new FMayaOutputDevice()); //Add Maya output device

		bUEInitialized = true; // Dont redo this part if someone unloads and reloads our plugin
	}

	// Tell Maya about our plugin
	MFnPlugin MayaPlugin(
		MayaPluginObject,
		"MayaLiveLinkPlugin",
		"v1.0");

	LiveLinkProvider = ILiveLinkProvider::CreateLiveLinkProvider(TEXT("Maya Live Link"));
	ConnectionStatusChangedHandle = LiveLinkProvider->RegisterConnStatusChangedHandle(FLiveLinkProviderConnectionStatusChanged::FDelegate::CreateStatic(&OnConnectionStatusChanged));

	// We do not tick the core engine but we need to tick the ticker to make sure the message bus endpoint in LiveLinkProvider is
	// up to date
	FTicker::GetCoreTicker().Tick(1.f);

	LiveLinkStreamManager = MakeShareable(new FLiveLinkStreamedSubjectManager());



	MCallbackId forceUpdateCallbackId = MDGMessage::addForceUpdateCallback((MMessage::MTimeFunction)OnForceChange);
	myCallbackIds.append(forceUpdateCallbackId);

	MCallbackId ScenePreOpenedCallbackID = MSceneMessage::addCallback(MSceneMessage::kBeforeOpen, (MMessage::MBasicFunction)OnScenePreOpen);
	myCallbackIds.append(ScenePreOpenedCallbackID);

	MCallbackId SceneOpenedCallbackId = MSceneMessage::addCallback(MSceneMessage::kAfterOpen, (MMessage::MBasicFunction)OnSceneOpen);
	myCallbackIds.append(SceneOpenedCallbackId);

	MCallbackId dagChangedCallbackId = MDagMessage::addAllDagChangesCallback(AllDagChangesCallback);
	myCallbackIds.append(dagChangedCallbackId);

	// Update function every 5 seconds
	MCallbackId timerCallback = MTimerMessage::addTimerCallback(5.f, (MMessage::MElapsedTimeFunction)OnInterval);
	myCallbackIds.append(timerCallback);

	MayaPlugin.registerCommand(LiveLinkSubjectsCommandName, LiveLinkSubjectsCommand::creator);
	MayaPlugin.registerCommand(LiveLinkAddSubjectCommandName, LiveLinkAddSubjectCommand::creator);
	MayaPlugin.registerCommand(LiveLinkRemoveSubjectCommandName, LiveLinkRemoveSubjectCommand::creator);
	MayaPlugin.registerCommand(LiveLinkConnectionStatusCommandName, LiveLinkConnectionStatusCommand::creator);

	// Print to Maya's output window, too!
	UE_LOG(LogBlankMayaPlugin, Display, TEXT("MayaLiveLinkPlugin initialized"));

	RefreshViewportCallbacks();

	const MStatus MayaStatusResult = MS::kSuccess;
	return MayaStatusResult;
}


/**
* Called by Maya either at shutdown, or when the user opts to unload the plugin through the Plugin Manager
*
* @param	MayaPluginObject	The Maya object that represents our plugin
*
* @return	MS::kSuccess if everything went OK and the plugin was fully shut down
*/
DLLEXPORT MStatus uninitializePlugin(MObject MayaPluginObject)
{
	// Get the plugin API for the plugin object
	MFnPlugin MayaPlugin(MayaPluginObject);

	// ... do stuff here ...

	if (myCallbackIds.length() != 0)
	{
		// Make sure we remove all the callbacks we added
		MMessage::removeCallbacks(myCallbackIds);
	}

	MayaPlugin.deregisterCommand(LiveLinkSubjectsCommandName);
	MayaPlugin.deregisterCommand(LiveLinkAddSubjectCommandName);
	MayaPlugin.deregisterCommand(LiveLinkRemoveSubjectCommandName);
	MayaPlugin.deregisterCommand(LiveLinkConnectionStatusCommandName);

	if (ConnectionStatusChangedHandle.IsValid())
	{
		LiveLinkProvider->UnregisterConnStatusChangedHandle(ConnectionStatusChangedHandle);
		ConnectionStatusChangedHandle.Reset();
	}

	FTicker::GetCoreTicker().Tick(1.f);

	LiveLinkProvider = nullptr;

	const MStatus MayaStatusResult = MS::kSuccess;
	return MayaStatusResult;
}
