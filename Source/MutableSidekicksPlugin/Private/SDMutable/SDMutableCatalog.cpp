#include "SDMutable/SDMutableCatalog.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetNotifications.h"
#include "BoneWeights.h"
#include "Animation/Skeleton.h"
#include "GameplayTagContainer.h"
#include "MeshDescription.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/StaticMeshVertexBuffer.h"
#include "RenderingThread.h"
#include "ScopedTransaction.h"
#include "SkeletalMeshAttributes.h"
#include "SkeletonModifier.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogSDMutableCatalog, Log, All);

namespace
{
	template <typename FuncType>
	bool ForEachCatalogPack(const USDMutableCatalog& Catalog, FuncType&& Func)
	{
		// Runtime queries span every pack group, but each pack still stays soft-referenced on the root catalog.
		const TArray<TSoftObjectPtr<USDMutableCatalogPack>>* PackGroups[] =
		{
			&Catalog.SpeciesPackCatalogs,
			&Catalog.OutfitPackCatalogs,
			&Catalog.SharedPackCatalogs,
			&Catalog.UnknownPackCatalogs
		};

		for (const TArray<TSoftObjectPtr<USDMutableCatalogPack>>* PackGroup : PackGroups)
		{
			for (const TSoftObjectPtr<USDMutableCatalogPack>& PackPtr : *PackGroup)
			{
				const USDMutableCatalogPack* Pack = PackPtr.LoadSynchronous();
				if (Pack && Func(*Pack))
				{
					return true;
				}
			}
		}

		return false;
	}

#if WITH_EDITOR
	struct FSDMutableCatalogScanStats
	{
		int32 NumAssetsScanned = 0;
		int32 NumEntriesAdded = 0;
		int32 NumDuplicatePartIds = 0;
		int32 NumUnresolvedSlots = 0;
		int32 NumColorPropertiesAdded = 0;
		int32 NumThumbnailsMatched = 0;
	};

	struct FSDMutableSkeletonFixStats
	{
		int32 NumAssetsScanned = 0;
		int32 NumMeshesLoaded = 0;
		int32 NumMeshesWithTransformBones = 0;
		int32 NumMeshesChanged = 0;
		int32 NumMeshesSaved = 0;
		int32 NumSkeletonsSaved = 0;
		int32 NumFailures = 0;
	};

	const TArray<FName>& GetSyntyTransformBreakerBoneNames()
	{
		// Some imported Sidekicks meshes include intermediate transform bones that need to be removed in depth order.
		static const TArray<FName> BadBoneNames = {
			TEXT("transform1"),
			TEXT("transform2"),
			TEXT("transform3")
		};
		return BadBoneNames;
	}

	bool IsSyntyTransformBreakerBone(const FName BoneName)
	{
		for (const FName BadBoneName : GetSyntyTransformBreakerBoneNames())
		{
			if (BoneName.IsEqual(BadBoneName, ENameCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	}

	int32 GetBoneDepth(const FReferenceSkeleton& ReferenceSkeleton, int32 BoneIndex)
	{
		int32 Depth = 0;
		while (ReferenceSkeleton.IsValidIndex(BoneIndex))
		{
			BoneIndex = ReferenceSkeleton.GetParentIndex(BoneIndex);
			if (ReferenceSkeleton.IsValidIndex(BoneIndex))
			{
				++Depth;
			}
		}

		return Depth;
	}

	TArray<FName> FindSyntyTransformBreakerBones(const USkeletalMesh& SkeletalMesh)
	{
		TArray<TPair<FName, int32>> BadBonesWithDepth;

		const FReferenceSkeleton& ReferenceSkeleton = SkeletalMesh.GetRefSkeleton();
		for (int32 BoneIndex = 0; BoneIndex < ReferenceSkeleton.GetNum(); ++BoneIndex)
		{
			const FName BoneName = ReferenceSkeleton.GetBoneName(BoneIndex);
			if (IsSyntyTransformBreakerBone(BoneName))
			{
				BadBonesWithDepth.Emplace(BoneName, GetBoneDepth(ReferenceSkeleton, BoneIndex));
			}
		}

		BadBonesWithDepth.Sort([](const TPair<FName, int32>& Left, const TPair<FName, int32>& Right)
		{
			return Left.Value > Right.Value;
		});

		TArray<FName> BadBones;
		BadBones.Reserve(BadBonesWithDepth.Num());
		for (const TPair<FName, int32>& BadBoneWithDepth : BadBonesWithDepth)
		{
			BadBones.Add(BadBoneWithDepth.Key);
		}

		return BadBones;
	}

	bool SaveAssetPackage(UObject& Asset)
	{
		UPackage* Package = Asset.GetPackage();
		if (!Package)
		{
			UE_LOG(LogSDMutableCatalog, Warning, TEXT("Cannot save %s because it has no package."), *Asset.GetName());
			return false;
		}

		const FString PackageFileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(),
			FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;

		const bool bSaved = UPackage::SavePackage(Package, &Asset, *PackageFileName, SaveArgs);
		if (!bSaved)
		{
			UE_LOG(LogSDMutableCatalog, Warning, TEXT("Failed to save repaired asset package: %s"), *Package->GetName());
		}

		return bSaved;
	}

	bool CommitSyntyTransformBoneFixWithoutPrompt(USkeletalMesh& SkeletalMesh, const USkeletonModifier& SkeletonModifier)
	{
		USkeleton* Skeleton = SkeletalMesh.GetSkeleton();
		if (!Skeleton)
		{
			UE_LOG(LogSDMutableCatalog, Warning, TEXT("%s: cannot commit transform-bone fix because the mesh has no assigned skeleton."), *SkeletalMesh.GetName());
			return false;
		}

		constexpr int32 LODIndex = 0;
		const FReferenceSkeleton& NewReferenceSkeleton = SkeletonModifier.GetReferenceSkeleton();
		const TArray<int32>& BoneIndexTracker = SkeletonModifier.GetBoneIndexTracker();
		const TArray<FMeshBoneInfo>& BoneInfos = NewReferenceSkeleton.GetRawRefBoneInfo();
		const TArray<FTransform>& Transforms = NewReferenceSkeleton.GetRawRefBonePose();

		// Keep the LOD0 mesh description bone table and skin-weight indices aligned with the edited reference skeleton.
		FMeshDescription MeshDescription;
		SkeletalMesh.CloneMeshDescription(LODIndex, MeshDescription);
		if (MeshDescription.IsEmpty())
		{
			UE_LOG(LogSDMutableCatalog, Warning, TEXT("%s: cannot commit transform-bone fix because LOD0 mesh description is empty."), *SkeletalMesh.GetName());
			return false;
		}

		FSkeletalMeshAttributes MeshAttributes(MeshDescription);
		if (!MeshAttributes.HasBones())
		{
			MeshAttributes.Register(true);
		}

		MeshAttributes.Bones().Reset(BoneInfos.Num());
		FSkeletalMeshAttributes::FBoneNameAttributesRef BoneNames = MeshAttributes.GetBoneNames();
		FSkeletalMeshAttributes::FBoneParentIndexAttributesRef BoneParentIndices = MeshAttributes.GetBoneParentIndices();
		FSkeletalMeshAttributes::FBonePoseAttributesRef BonePoses = MeshAttributes.GetBonePoses();

		for (int32 BoneIndex = 0; BoneIndex < BoneInfos.Num(); ++BoneIndex)
		{
			const FMeshBoneInfo& BoneInfo = BoneInfos[BoneIndex];
			const FBoneID BoneID = MeshAttributes.CreateBone();
			BoneNames.Set(BoneID, BoneInfo.Name);
			BoneParentIndices.Set(BoneID, BoneInfo.ParentIndex);
			BonePoses.Set(BoneID, Transforms[BoneIndex]);
		}

		using namespace UE::AnimationCore;
		FBoneWeightsSettings BoneSettings;
		BoneSettings.SetNormalizeType(EBoneWeightNormalizeType::None);

		for (const FName SkinWeightProfile : MeshAttributes.GetSkinWeightProfileNames())
		{
			FSkinWeightsVertexAttributesRef SkinWeights = MeshAttributes.GetVertexSkinWeights(SkinWeightProfile);
			if (!SkinWeights.IsValid())
			{
				continue;
			}

			for (const FVertexID& VertexID : MeshDescription.Vertices().GetElementIDs())
			{
				const FVertexBoneWeights OldBoneWeights = SkinWeights.Get(VertexID);
				if (OldBoneWeights.Num() == 0)
				{
					continue;
				}

				TArray<FBoneWeight> NewWeights;
				NewWeights.Reserve(OldBoneWeights.Num());
				for (int32 WeightIndex = 0; WeightIndex < OldBoneWeights.Num(); ++WeightIndex)
				{
					const FBoneWeight& OldBoneWeight = OldBoneWeights[WeightIndex];
					const int32 OldBoneIndex = OldBoneWeight.GetBoneIndex();
					if (!BoneIndexTracker.IsValidIndex(OldBoneIndex))
					{
						UE_LOG(LogSDMutableCatalog, Warning, TEXT("%s: invalid skin-weight bone index %d; skipping weight."), *SkeletalMesh.GetName(), OldBoneIndex);
						continue;
					}

					const int32 NewBoneIndex = BoneIndexTracker[OldBoneIndex];
					if (NewBoneIndex != INDEX_NONE)
					{
						NewWeights.Add(FBoneWeight(NewBoneIndex, OldBoneWeight.GetRawWeight()));
					}
				}

				SkinWeights.Set(VertexID, FBoneWeights::Create(NewWeights, BoneSettings));
			}
		}

		TArray<EBoneTranslationRetargetingMode::Type> RetargetingModes;
		RetargetingModes.Init(EBoneTranslationRetargetingMode::Animation, BoneInfos.Num());
		for (int32 OldBoneIndex = 0; OldBoneIndex < BoneIndexTracker.Num(); ++OldBoneIndex)
		{
			const int32 NewBoneIndex = BoneIndexTracker[OldBoneIndex];
			if (RetargetingModes.IsValidIndex(NewBoneIndex))
			{
				RetargetingModes[NewBoneIndex] = Skeleton->GetBoneTranslationRetargetingMode(OldBoneIndex);
			}
		}

		FlushRenderingCommands();

		Skeleton->Modify();
		SkeletalMesh.SetFlags(RF_Transactional);
		SkeletalMesh.Modify();
		SkeletalMesh.SetRefSkeleton(NewReferenceSkeleton);
		SkeletalMesh.GetRefBasesInvMatrix().Reset();
		SkeletalMesh.CalculateInvRefMatrices();
		SkeletalMesh.ModifyMeshDescription(LODIndex);
		SkeletalMesh.CreateMeshDescription(LODIndex, MoveTemp(MeshDescription));
		SkeletalMesh.CommitMeshDescription(LODIndex);

		if (!Skeleton->RecreateBoneTree(&SkeletalMesh))
		{
			UE_LOG(LogSDMutableCatalog, Warning, TEXT("%s: failed to recreate assigned skeleton bone tree."), *SkeletalMesh.GetName());
			return false;
		}

		for (int32 BoneIndex = 0; BoneIndex < RetargetingModes.Num(); ++BoneIndex)
		{
			Skeleton->SetBoneTranslationRetargetingMode(BoneIndex, RetargetingModes[BoneIndex]);
		}

		Skeleton->MarkPackageDirty();
		FAssetNotifications::SkeletonNeedsToBeSaved(Skeleton);
		SkeletalMesh.PostEditChange();
		SkeletalMesh.MarkPackageDirty();
		return true;
	}

	bool FixSyntyTransformBreakerBones(USkeletalMesh& SkeletalMesh, const TArray<FName>& BadBones, USkeleton*& OutModifiedSkeleton)
	{
		OutModifiedSkeleton = nullptr;
		if (BadBones.IsEmpty())
		{
			return false;
		}

		OutModifiedSkeleton = SkeletalMesh.GetSkeleton();
		USkeletonModifier* SkeletonModifier = NewObject<USkeletonModifier>();
		if (!SkeletonModifier || !SkeletonModifier->SetSkeletalMesh(&SkeletalMesh))
		{
			UE_LOG(LogSDMutableCatalog, Warning, TEXT("%s: failed to initialize SkeletonModifier."), *SkeletalMesh.GetName());
			return false;
		}

		TSet<FName> BadBoneSet;
		BadBoneSet.Reserve(BadBones.Num());
		for (const FName BadBone : BadBones)
		{
			BadBoneSet.Add(BadBone);
		}

		bool bChanged = false;
		for (const FName BadBone : BadBones)
		{
			const FName ParentBone = SkeletonModifier->GetParentName(BadBone);
			if (ParentBone.IsNone())
			{
				UE_LOG(LogSDMutableCatalog, Warning, TEXT("%s: %s has no parent; skipping."), *SkeletalMesh.GetName(), *BadBone.ToString());
				continue;
			}

			const FTransform BadLocalTransform = SkeletonModifier->GetBoneTransform(BadBone, false);
			const TArray<FName> Children = SkeletonModifier->GetChildrenNames(BadBone, false);
			TArray<FString> ReparentedChildren;

			for (const FName Child : Children)
			{
				if (BadBoneSet.Contains(Child))
				{
					continue;
				}

				const FTransform ChildLocalTransform = SkeletonModifier->GetBoneTransform(Child, false);
				const FTransform NewLocalTransform = ChildLocalTransform * BadLocalTransform;

				const bool bReparented = SkeletonModifier->ParentBone(Child, ParentBone);
				const bool bTransformUpdated = bReparented && SkeletonModifier->SetBoneTransform(Child, NewLocalTransform, true);
				if (bTransformUpdated)
				{
					ReparentedChildren.Add(Child.ToString());
					bChanged = true;
				}
				else
				{
					UE_LOG(LogSDMutableCatalog, Warning, TEXT("%s: failed to reparent %s from %s to %s."), *SkeletalMesh.GetName(), *Child.ToString(), *BadBone.ToString(), *ParentBone.ToString());
				}
			}

			if (SkeletonModifier->RemoveBone(BadBone, true))
			{
				UE_LOG(
					LogSDMutableCatalog,
					Log,
					TEXT("%s: removed %s, reparented [%s] to %s."),
					*SkeletalMesh.GetName(),
					*BadBone.ToString(),
					*FString::Join(ReparentedChildren, TEXT(", ")),
					*ParentBone.ToString());
				bChanged = true;
			}
			else
			{
				UE_LOG(LogSDMutableCatalog, Warning, TEXT("%s: failed to remove %s."), *SkeletalMesh.GetName(), *BadBone.ToString());
			}
		}

		if (!bChanged)
		{
			return false;
		}

		SkeletalMesh.Modify();
		if (!CommitSyntyTransformBoneFixWithoutPrompt(SkeletalMesh, *SkeletonModifier))
		{
			UE_LOG(LogSDMutableCatalog, Warning, TEXT("%s: failed to commit repaired skeleton to skeletal mesh."), *SkeletalMesh.GetName());
			return false;
		}

		return true;
	}

	constexpr int32 SidekicksPaletteGridSize = 16;

	FIntPoint PaletteIndexToCell(const int32 PaletteIndex)
	{
		return FIntPoint(PaletteIndex % SidekicksPaletteGridSize, PaletteIndex / SidekicksPaletteGridSize);
	}

	int32 PaletteCellToIndex(const FIntPoint Cell)
	{
		if (Cell.X < 0 || Cell.X >= SidekicksPaletteGridSize || Cell.Y < 0 || Cell.Y >= SidekicksPaletteGridSize)
		{
			return INDEX_NONE;
		}

		return Cell.Y * SidekicksPaletteGridSize + Cell.X;
	}

	FIntPoint UVToPaletteCell(const FVector2f& UV)
	{
		const float ClampedU = FMath::Clamp(UV.X, 0.0f, 1.0f - UE_KINDA_SMALL_NUMBER);
		const float ClampedV = FMath::Clamp(UV.Y, 0.0f, 1.0f - UE_KINDA_SMALL_NUMBER);

		return FIntPoint(
			FMath::Clamp(FMath::FloorToInt(ClampedU * SidekicksPaletteGridSize), 0, SidekicksPaletteGridSize - 1),
			FMath::Clamp(FMath::FloorToInt(ClampedV * SidekicksPaletteGridSize), 0, SidekicksPaletteGridSize - 1));
	}

	FName GetFallbackColorName(const int32 PaletteIndex)
	{
		return FName(*FString::Printf(TEXT("Color_%03d"), PaletteIndex));
	}

	TMap<int32, FName> LoadSidekicksColorNameMap()
	{
		TMap<int32, FName> ColorNames;

		// The CSV is editor metadata only; missing rows simply fall back to Color_### display names.
		const FString ColorListPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Raw/Sidekicks_ColorList.csv"));
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *ColorListPath))
		{
			UE_LOG(LogSDMutableCatalog, Warning, TEXT("Could not load Sidekicks color list: %s"), *ColorListPath);
			return ColorNames;
		}

		for (int32 LineIndex = 1; LineIndex < Lines.Num(); ++LineIndex)
		{
			TArray<FString> Columns;
			Lines[LineIndex].ParseIntoArray(Columns, TEXT(","), false);
			if (Columns.Num() < 3)
			{
				continue;
			}

			const int32 PaletteIndex = FCString::Atoi(*Columns[1].TrimStartAndEnd());
			FString ColorName = Columns[2].TrimStartAndEnd();
			if (PaletteIndex < 0 || PaletteIndex >= 256 || ColorName.IsEmpty() || ColorName == TEXT("-"))
			{
				continue;
			}

			ColorNames.Add(PaletteIndex, FName(*ColorName));
		}

		return ColorNames;
	}

	bool IsPointInsideUVTriangle(const FVector2f& Point, const FVector2f& A, const FVector2f& B, const FVector2f& C)
	{
		const FVector2f V0 = C - A;
		const FVector2f V1 = B - A;
		const FVector2f V2 = Point - A;
		const float Dot00 = FVector2f::DotProduct(V0, V0);
		const float Dot01 = FVector2f::DotProduct(V0, V1);
		const float Dot02 = FVector2f::DotProduct(V0, V2);
		const float Dot11 = FVector2f::DotProduct(V1, V1);
		const float Dot12 = FVector2f::DotProduct(V1, V2);
		const float Denominator = Dot00 * Dot11 - Dot01 * Dot01;

		if (FMath::IsNearlyZero(Denominator))
		{
			return false;
		}

		const float InvDenominator = 1.0f / Denominator;
		const float U = (Dot11 * Dot02 - Dot01 * Dot12) * InvDenominator;
		const float V = (Dot00 * Dot12 - Dot01 * Dot02) * InvDenominator;
		return U >= -UE_KINDA_SMALL_NUMBER && V >= -UE_KINDA_SMALL_NUMBER && (U + V) <= 1.0f + UE_KINDA_SMALL_NUMBER;
	}

	void AddPaletteCellIfValid(const FIntPoint Cell, TSet<int32>& OutPaletteIndices)
	{
		const int32 PaletteIndex = PaletteCellToIndex(Cell);
		if (PaletteIndex != INDEX_NONE)
		{
			OutPaletteIndices.Add(PaletteIndex);
		}
	}

	void AddTrianglePaletteCells(const FVector2f& UV0, const FVector2f& UV1, const FVector2f& UV2, TSet<int32>& OutPaletteIndices)
	{
		AddPaletteCellIfValid(UVToPaletteCell(UV0), OutPaletteIndices);
		AddPaletteCellIfValid(UVToPaletteCell(UV1), OutPaletteIndices);
		AddPaletteCellIfValid(UVToPaletteCell(UV2), OutPaletteIndices);

		// Triangles can cross palette-cell boundaries, so sample the covered bounding cells as well as vertices.
		const float MinU = FMath::Min3(UV0.X, UV1.X, UV2.X);
		const float MaxU = FMath::Max3(UV0.X, UV1.X, UV2.X);
		const float MinV = FMath::Min3(UV0.Y, UV1.Y, UV2.Y);
		const float MaxV = FMath::Max3(UV0.Y, UV1.Y, UV2.Y);
		const FIntPoint MinCell = UVToPaletteCell(FVector2f(MinU, MinV));
		const FIntPoint MaxCell = UVToPaletteCell(FVector2f(MaxU, MaxV));

		for (int32 CellY = MinCell.Y; CellY <= MaxCell.Y; ++CellY)
		{
			for (int32 CellX = MinCell.X; CellX <= MaxCell.X; ++CellX)
			{
				const float CellMinU = static_cast<float>(CellX) / SidekicksPaletteGridSize;
				const float CellMinV = static_cast<float>(CellY) / SidekicksPaletteGridSize;
				const float CellMaxU = static_cast<float>(CellX + 1) / SidekicksPaletteGridSize;
				const float CellMaxV = static_cast<float>(CellY + 1) / SidekicksPaletteGridSize;

				const FVector2f Samples[] =
				{
					FVector2f((CellMinU + CellMaxU) * 0.5f, (CellMinV + CellMaxV) * 0.5f),
					FVector2f(CellMinU, CellMinV),
					FVector2f(CellMaxU, CellMinV),
					FVector2f(CellMinU, CellMaxV),
					FVector2f(CellMaxU, CellMaxV)
				};

				for (const FVector2f& Sample : Samples)
				{
					if (IsPointInsideUVTriangle(Sample, UV0, UV1, UV2))
					{
						AddPaletteCellIfValid(FIntPoint(CellX, CellY), OutPaletteIndices);
						break;
					}
				}
			}
		}
	}

	TArray<FSDMutablePartColorProperty> BuildColorPropertiesFromMesh(const USkeletalMesh* SkeletalMesh, const TMap<int32, FName>& ColorNamesByIndex)
	{
		TArray<FSDMutablePartColorProperty> ColorProperties;
		if (!SkeletalMesh)
		{
			return ColorProperties;
		}

		const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
		if (!RenderData || RenderData->LODRenderData.IsEmpty())
		{
			return ColorProperties;
		}

		const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[0];
		const FStaticMeshVertexBuffer& VertexBuffer = LODData.StaticVertexBuffers.StaticMeshVertexBuffer;
		if (VertexBuffer.GetNumTexCoords() == 0)
		{
			return ColorProperties;
		}

		// Sidekicks parts use UV0 against a 16x16 color palette grid; this records which cells each mesh touches.
		TArray<uint32> Indices;
		LODData.MultiSizeIndexContainer.GetIndexBuffer(Indices);

		TSet<int32> PaletteIndices;
		for (int32 Index = 0; Index + 2 < Indices.Num(); Index += 3)
		{
			const uint32 VertexIndex0 = Indices[Index];
			const uint32 VertexIndex1 = Indices[Index + 1];
			const uint32 VertexIndex2 = Indices[Index + 2];
			const FVector2f UV0 = VertexBuffer.GetVertexUV(VertexIndex0, 0);
			const FVector2f UV1 = VertexBuffer.GetVertexUV(VertexIndex1, 0);
			const FVector2f UV2 = VertexBuffer.GetVertexUV(VertexIndex2, 0);

			AddTrianglePaletteCells(UV0, UV1, UV2, PaletteIndices);
		}

		TArray<int32> SortedPaletteIndices = PaletteIndices.Array();
		SortedPaletteIndices.Sort();
		ColorProperties.Reserve(SortedPaletteIndices.Num());

		for (const int32 PaletteIndex : SortedPaletteIndices)
		{
			FSDMutablePartColorProperty& ColorProperty = ColorProperties.AddDefaulted_GetRef();
			ColorProperty.PaletteIndex = PaletteIndex;
			ColorProperty.PaletteCell = PaletteIndexToCell(PaletteIndex);
			if (const FName* ColorName = ColorNamesByIndex.Find(PaletteIndex))
			{
				ColorProperty.ColorName = *ColorName;
			}
			else
			{
				ColorProperty.ColorName = GetFallbackColorName(PaletteIndex);
			}
		}

		return ColorProperties;
	}

	FSDMutableSlotNameRule MakeRule(const TCHAR* Token, const ESDMutablePartSlot Slot)
	{
		FSDMutableSlotNameRule Rule;
		Rule.Token = Token;
		Rule.Slot = Slot;
		return Rule;
	}

	TArray<FSDMutableSlotNameRule> MakeDefaultSlotRules()
	{
		return {
			MakeRule(TEXT("01HEAD"), ESDMutablePartSlot::Head),
			MakeRule(TEXT("02HAIR"), ESDMutablePartSlot::Hair),
			MakeRule(TEXT("03EBRL"), ESDMutablePartSlot::EyebrowLeft),
			MakeRule(TEXT("04EBRR"), ESDMutablePartSlot::EyebrowRight),
			MakeRule(TEXT("05EYEL"), ESDMutablePartSlot::EyeLeft),
			MakeRule(TEXT("06EYER"), ESDMutablePartSlot::EyeRight),
			MakeRule(TEXT("07EARL"), ESDMutablePartSlot::EarLeft),
			MakeRule(TEXT("08EARR"), ESDMutablePartSlot::EarRight),
			MakeRule(TEXT("09FCHR"), ESDMutablePartSlot::FacialHair),
			MakeRule(TEXT("10TORS"), ESDMutablePartSlot::Torso),
			MakeRule(TEXT("11AUPL"), ESDMutablePartSlot::ArmUpperLeft),
			MakeRule(TEXT("12AUPR"), ESDMutablePartSlot::ArmUpperRight),
			MakeRule(TEXT("13ALWL"), ESDMutablePartSlot::ArmLowerLeft),
			MakeRule(TEXT("14ALWR"), ESDMutablePartSlot::ArmLowerRight),
			MakeRule(TEXT("15HNDL"), ESDMutablePartSlot::HandLeft),
			MakeRule(TEXT("16HNDR"), ESDMutablePartSlot::HandRight),
			MakeRule(TEXT("17HIPS"), ESDMutablePartSlot::Hips),
			MakeRule(TEXT("18LEGL"), ESDMutablePartSlot::LegLeft),
			MakeRule(TEXT("19LEGR"), ESDMutablePartSlot::LegRight),
			MakeRule(TEXT("20FOTL"), ESDMutablePartSlot::FootLeft),
			MakeRule(TEXT("21FOTR"), ESDMutablePartSlot::FootRight),
			MakeRule(TEXT("22AHED"), ESDMutablePartSlot::AttachmentHead),
			MakeRule(TEXT("23AFAC"), ESDMutablePartSlot::AttachmentFace),
			MakeRule(TEXT("24ABAC"), ESDMutablePartSlot::AttachmentBack),
			MakeRule(TEXT("25AHPF"), ESDMutablePartSlot::AttachmentHipsFront),
			MakeRule(TEXT("26AHPB"), ESDMutablePartSlot::AttachmentHipsBack),
			MakeRule(TEXT("27AHPL"), ESDMutablePartSlot::AttachmentHipsLeft),
			MakeRule(TEXT("28AHPR"), ESDMutablePartSlot::AttachmentHipsRight),
			MakeRule(TEXT("29ASHL"), ESDMutablePartSlot::AttachmentShoulderLeft),
			MakeRule(TEXT("30ASHR"), ESDMutablePartSlot::AttachmentShoulderRight),
			MakeRule(TEXT("31AEBL"), ESDMutablePartSlot::AttachmentElbowLeft),
			MakeRule(TEXT("32AEBR"), ESDMutablePartSlot::AttachmentElbowRight),
			MakeRule(TEXT("33AKNL"), ESDMutablePartSlot::AttachmentKneeLeft),
			MakeRule(TEXT("34AKNR"), ESDMutablePartSlot::AttachmentKneeRight),
			MakeRule(TEXT("35NOSE"), ESDMutablePartSlot::Nose),
			MakeRule(TEXT("36TETH"), ESDMutablePartSlot::Teeth),
			MakeRule(TEXT("TONG"), ESDMutablePartSlot::Tongue),
			MakeRule(TEXT("WRAP"), ESDMutablePartSlot::AttachmentWrap)
		};
	}

	ESDMutablePartSlot InferSlotFromAssetName(const FString& AssetName, const TArray<FSDMutableSlotNameRule>& Rules)
	{
		for (const FSDMutableSlotNameRule& Rule : Rules)
		{
			if (!Rule.Token.IsNone() && AssetName.Contains(Rule.Token.ToString(), ESearchCase::IgnoreCase))
			{
				return Rule.Slot;
			}
		}

		return ESDMutablePartSlot::None;
	}

	FString GetReadableSlotToken(const ESDMutablePartSlot Slot)
	{
		switch (Slot)
		{
		case ESDMutablePartSlot::Head: return TEXT("HEAD");
		case ESDMutablePartSlot::Hair: return TEXT("HAIR");
		case ESDMutablePartSlot::EyebrowLeft: return TEXT("EYEBROW_L");
		case ESDMutablePartSlot::EyebrowRight: return TEXT("EYEBROW_R");
		case ESDMutablePartSlot::EyeLeft: return TEXT("EYE_L");
		case ESDMutablePartSlot::EyeRight: return TEXT("EYE_R");
		case ESDMutablePartSlot::EarLeft: return TEXT("EAR_L");
		case ESDMutablePartSlot::EarRight: return TEXT("EAR_R");
		case ESDMutablePartSlot::FacialHair: return TEXT("FACIAL_HAIR");
		case ESDMutablePartSlot::Nose: return TEXT("NOSE");
		case ESDMutablePartSlot::Teeth: return TEXT("TEETH");
		case ESDMutablePartSlot::Tongue: return TEXT("TONGUE");
		case ESDMutablePartSlot::Torso: return TEXT("TORSO");
		case ESDMutablePartSlot::ArmUpperLeft: return TEXT("ARM_UPPER_L");
		case ESDMutablePartSlot::ArmUpperRight: return TEXT("ARM_UPPER_R");
		case ESDMutablePartSlot::ArmLowerLeft: return TEXT("ARM_LOWER_L");
		case ESDMutablePartSlot::ArmLowerRight: return TEXT("ARM_LOWER_R");
		case ESDMutablePartSlot::HandLeft: return TEXT("HAND_L");
		case ESDMutablePartSlot::HandRight: return TEXT("HAND_R");
		case ESDMutablePartSlot::Hips: return TEXT("HIPS");
		case ESDMutablePartSlot::LegLeft: return TEXT("LEG_L");
		case ESDMutablePartSlot::LegRight: return TEXT("LEG_R");
		case ESDMutablePartSlot::FootLeft: return TEXT("FOOT_L");
		case ESDMutablePartSlot::FootRight: return TEXT("FOOT_R");
		case ESDMutablePartSlot::AttachmentFace: return TEXT("ATTACH_FACE");
		case ESDMutablePartSlot::AttachmentHead: return TEXT("ATTACH_HEAD");
		case ESDMutablePartSlot::AttachmentBack: return TEXT("ATTACH_BACK");
		case ESDMutablePartSlot::AttachmentShoulderLeft: return TEXT("ATTACH_SHOULDER_L");
		case ESDMutablePartSlot::AttachmentShoulderRight: return TEXT("ATTACH_SHOULDER_R");
		case ESDMutablePartSlot::AttachmentElbowLeft: return TEXT("ATTACH_ELBOW_L");
		case ESDMutablePartSlot::AttachmentElbowRight: return TEXT("ATTACH_ELBOW_R");
		case ESDMutablePartSlot::AttachmentHipsBack: return TEXT("ATTACH_HIPS_BACK");
		case ESDMutablePartSlot::AttachmentHipsFront: return TEXT("ATTACH_HIPS_FRONT");
		case ESDMutablePartSlot::AttachmentHipsLeft: return TEXT("ATTACH_HIPS_L");
		case ESDMutablePartSlot::AttachmentHipsRight: return TEXT("ATTACH_HIPS_R");
		case ESDMutablePartSlot::AttachmentKneeLeft: return TEXT("ATTACH_KNEE_L");
		case ESDMutablePartSlot::AttachmentKneeRight: return TEXT("ATTACH_KNEE_R");
		case ESDMutablePartSlot::AttachmentWrap: return TEXT("ATTACH_WRAP");
		default: return TEXT("UNKNOWN");
		}
	}

	FString GetDisplaySlotName(const ESDMutablePartSlot Slot)
	{
		switch (Slot)
		{
		case ESDMutablePartSlot::Hair: return TEXT("Hair");
		case ESDMutablePartSlot::Head: return TEXT("Head");
		case ESDMutablePartSlot::EyebrowLeft: return TEXT("Eyebrow Left");
		case ESDMutablePartSlot::EyebrowRight: return TEXT("Eyebrow Right");
		case ESDMutablePartSlot::EyeLeft: return TEXT("Eye Left");
		case ESDMutablePartSlot::EyeRight: return TEXT("Eye Right");
		case ESDMutablePartSlot::EarLeft: return TEXT("Ear Left");
		case ESDMutablePartSlot::EarRight: return TEXT("Ear Right");
		case ESDMutablePartSlot::FacialHair: return TEXT("Facial Hair");
		case ESDMutablePartSlot::Nose: return TEXT("Nose");
		case ESDMutablePartSlot::Teeth: return TEXT("Teeth");
		case ESDMutablePartSlot::Tongue: return TEXT("Tongue");
		case ESDMutablePartSlot::Torso: return TEXT("Torso");
		case ESDMutablePartSlot::ArmUpperLeft: return TEXT("Upper Arm Left");
		case ESDMutablePartSlot::ArmUpperRight: return TEXT("Upper Arm Right");
		case ESDMutablePartSlot::ArmLowerLeft: return TEXT("Lower Arm Left");
		case ESDMutablePartSlot::ArmLowerRight: return TEXT("Lower Arm Right");
		case ESDMutablePartSlot::HandLeft: return TEXT("Hand Left");
		case ESDMutablePartSlot::HandRight: return TEXT("Hand Right");
		case ESDMutablePartSlot::Hips: return TEXT("Hips");
		case ESDMutablePartSlot::LegLeft: return TEXT("Leg Left");
		case ESDMutablePartSlot::LegRight: return TEXT("Leg Right");
		case ESDMutablePartSlot::FootLeft: return TEXT("Foot Left");
		case ESDMutablePartSlot::FootRight: return TEXT("Foot Right");
		case ESDMutablePartSlot::AttachmentHead: return TEXT("Head Attachment");
		case ESDMutablePartSlot::AttachmentFace: return TEXT("Face Attachment");
		case ESDMutablePartSlot::AttachmentBack: return TEXT("Back Attachment");
		case ESDMutablePartSlot::AttachmentShoulderLeft: return TEXT("Shoulder Attachment Left");
		case ESDMutablePartSlot::AttachmentShoulderRight: return TEXT("Shoulder Attachment Right");
		case ESDMutablePartSlot::AttachmentElbowLeft: return TEXT("Elbow Attachment Left");
		case ESDMutablePartSlot::AttachmentElbowRight: return TEXT("Elbow Attachment Right");
		case ESDMutablePartSlot::AttachmentWrap: return TEXT("Wrap Attachment");
		case ESDMutablePartSlot::AttachmentHipsFront: return TEXT("Hips Attachment Front");
		case ESDMutablePartSlot::AttachmentHipsBack: return TEXT("Hips Attachment Back");
		case ESDMutablePartSlot::AttachmentHipsLeft: return TEXT("Hips Attachment Left");
		case ESDMutablePartSlot::AttachmentHipsRight: return TEXT("Hips Attachment Right");
		case ESDMutablePartSlot::AttachmentKneeLeft: return TEXT("Knee Attachment Left");
		case ESDMutablePartSlot::AttachmentKneeRight: return TEXT("Knee Attachment Right");
		default: return TEXT("Unknown");
		}
	}

	FString GetDisplayPackName(const TArray<FString>& Tokens)
	{
		if (Tokens.Num() < 3)
		{
			return TEXT("Unknown");
		}

		const FString PackKey = Tokens[1].ToUpper() + TEXT("_") + Tokens[2].ToUpper();
		if (PackKey == TEXT("HUMN_BASE")) return TEXT("Human Base");
		if (PackKey == TEXT("ELVN_BASE")) return TEXT("Elven Base");
		if (PackKey == TEXT("GOBL_BASE")) return TEXT("Goblin Base");
		if (PackKey == TEXT("SKTN_BASE")) return TEXT("Skeleton Base");
		if (PackKey == TEXT("ZOMB_BASE")) return TEXT("Zombie Base");
		if (PackKey == TEXT("APOC_OUTL")) return TEXT("Apocalypse Outlaws");
		if (PackKey == TEXT("APOC_SURV")) return TEXT("Apocalypse Survivors");
		if (PackKey == TEXT("APOC_ZOMB")) return TEXT("Apocalypse Zombies");
		if (PackKey == TEXT("ELVN_WARR")) return TEXT("Elven Warriors");
		if (PackKey == TEXT("FANT_KNGT")) return TEXT("Fantasy Knights");
		if (PackKey == TEXT("FANT_SKTN")) return TEXT("Fantasy Skeletons");
		if (PackKey == TEXT("FANT_VILL")) return TEXT("Fantasy Villagers");
		if (PackKey == TEXT("GOBL_FIGT")) return TEXT("Goblin Fighters");
		if (PackKey == TEXT("HORR_VILN")) return TEXT("Horror Villains");
		if (PackKey == TEXT("MDRN_CIVL")) return TEXT("Modern Civilians");
		if (PackKey == TEXT("MDRN_POLC")) return TEXT("Modern Police");
		if (PackKey == TEXT("PIRT_CAPT")) return TEXT("Pirate Captains");
		if (PackKey == TEXT("SAMR_WARR")) return TEXT("Samurai Warriors");
		if (PackKey == TEXT("SCFI_CIVL")) return TEXT("Scifi Civilians");
		if (PackKey == TEXT("SCFI_ROBO")) return TEXT("Scifi Robots");
		if (PackKey == TEXT("SCFI_SOLD")) return TEXT("Scifi Soldiers");
		if (PackKey == TEXT("VIKG_WARR")) return TEXT("Viking Warriors");

		FString Fallback = Tokens[1] + TEXT(" ") + Tokens[2];
		Fallback.ToLowerInline();
		return Fallback;
	}

	FString SanitizeIdToken(FString Token)
	{
		Token.ToUpperInline();

		for (TCHAR& Character : Token)
		{
			if (!FChar::IsAlnum(Character))
			{
				Character = TEXT('_');
			}
		}

		while (Token.Contains(TEXT("__")))
		{
			Token.ReplaceInline(TEXT("__"), TEXT("_"));
		}

		Token.RemoveFromStart(TEXT("_"));
		Token.RemoveFromEnd(TEXT("_"));
		return Token;
	}

	FName MakeReadablePartId(const FString& AssetName, const ESDMutablePartSlot Slot)
	{
		TArray<FString> Tokens;
		AssetName.ParseIntoArray(Tokens, TEXT("_"), true);

		if (Tokens.Num() >= 5 && Tokens[0].Equals(TEXT("SK"), ESearchCase::IgnoreCase))
		{
			const FString PackToken = SanitizeIdToken(Tokens[1] + TEXT("_") + Tokens[2]);
			const FString VariantToken = SanitizeIdToken(Tokens[3]);
			const FString SlotToken = GetReadableSlotToken(Slot);

			if (!PackToken.IsEmpty() && !VariantToken.IsEmpty())
			{
				return FName(*FString::Printf(TEXT("%s_%s_%s"), *PackToken, *SlotToken, *VariantToken));
			}
		}

		return FName(*SanitizeIdToken(AssetName));
	}

	FText MakeReadableDisplayName(const FString& AssetName, const ESDMutablePartSlot Slot)
	{
		TArray<FString> Tokens;
		AssetName.ParseIntoArray(Tokens, TEXT("_"), true);

		if (Tokens.Num() >= 5 && Tokens[0].Equals(TEXT("SK"), ESearchCase::IgnoreCase))
		{
			return FText::FromString(FString::Printf(
				TEXT("%s %s %s"),
				*GetDisplayPackName(Tokens),
				*GetDisplaySlotName(Slot),
				*Tokens[3]));
		}

		return FText::FromString(AssetName);
	}

	TArray<FName> GetPackAssetNameTokens(const USDMutableCatalogPack& PackCatalog)
	{
		TArray<FName> Tokens = PackCatalog.AssetNameTokens;

		auto AddTokenIfHasTag = [&Tokens, &PackCatalog](const TCHAR* TagName, const TCHAR* Token)
		{
			const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TagName), false);
			if (PackCatalog.PackTags.HasTagExact(Tag))
			{
				Tokens.AddUnique(FName(Token));
			}
		};

		AddTokenIfHasTag(TEXT("Sidekicks.Pack.Outfit.ApocalypseOutlaws"), TEXT("APOC_OUTL"));
		AddTokenIfHasTag(TEXT("Sidekicks.Pack.Outfit.ApocalypseSurvivors"), TEXT("APOC_SURV"));
		AddTokenIfHasTag(TEXT("Sidekicks.Pack.Outfit.ApocalypseZombies"), TEXT("APOC_ZOMB"));
		AddTokenIfHasTag(TEXT("Sidekicks.Pack.Outfit.ElvenWarriors"), TEXT("ELVN_WARR"));
		AddTokenIfHasTag(TEXT("Sidekicks.Pack.Outfit.FantasyKnights"), TEXT("FANT_KNGT"));
		AddTokenIfHasTag(TEXT("Sidekicks.Pack.Outfit.FantasySkeletons"), TEXT("FANT_SKTN"));
		AddTokenIfHasTag(TEXT("Sidekicks.Pack.Outfit.FantasyVillagers"), TEXT("FANT_VILL"));
		AddTokenIfHasTag(TEXT("Sidekicks.Pack.Outfit.GoblinFighters"), TEXT("GOBL_FIGT"));
		AddTokenIfHasTag(TEXT("Sidekicks.Pack.Outfit.HorrorVillains"), TEXT("HORR_VILN"));
		AddTokenIfHasTag(TEXT("Sidekicks.Pack.Outfit.ModernCivilians"), TEXT("MDRN_CIVL"));
		AddTokenIfHasTag(TEXT("Sidekicks.Pack.Outfit.ModernPolice"), TEXT("MDRN_POLC"));
		AddTokenIfHasTag(TEXT("Sidekicks.Pack.Outfit.PirateCaptains"), TEXT("PIRT_CAPT"));
		AddTokenIfHasTag(TEXT("Sidekicks.Pack.Outfit.SamuraiWarriors"), TEXT("SAMR_WARR"));
		AddTokenIfHasTag(TEXT("Sidekicks.Pack.Outfit.ScifiCivilians"), TEXT("SCFI_CIVL"));
		AddTokenIfHasTag(TEXT("Sidekicks.Pack.Outfit.ScifiRobots"), TEXT("SCFI_ROBO"));
		AddTokenIfHasTag(TEXT("Sidekicks.Pack.Outfit.ScifiSoldiers"), TEXT("SCFI_SOLD"));
		AddTokenIfHasTag(TEXT("Sidekicks.Pack.Outfit.VikingWarriors"), TEXT("VIKG_WARR"));
		AddTokenIfHasTag(TEXT("Sidekicks.Pack.Species.Elves"), TEXT("ELVN_BASE"));
		AddTokenIfHasTag(TEXT("Sidekicks.Pack.Species.Goblins"), TEXT("GOBL_BASE"));
		AddTokenIfHasTag(TEXT("Sidekicks.Pack.Species.Humans"), TEXT("HUMN_BASE"));
		AddTokenIfHasTag(TEXT("Sidekicks.Pack.Species.Skeletons"), TEXT("SKTN_BASE"));
		AddTokenIfHasTag(TEXT("Sidekicks.Pack.Species.Zombies"), TEXT("ZOMB_BASE"));

		return Tokens;
	}

	bool AssetMatchesAnyToken(const FString& AssetName, const TArray<FName>& Tokens)
	{
		if (Tokens.IsEmpty())
		{
			return true;
		}

		for (const FName Token : Tokens)
		{
			if (!Token.IsNone() && AssetName.Contains(Token.ToString(), ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	}

	FSDMutableCatalogSlotParts& FindOrAddSlotParts(USDMutableCatalogPack& PackCatalog, const ESDMutablePartSlot Slot)
	{
		for (FSDMutableCatalogSlotParts& SlotParts : PackCatalog.Slots)
		{
			if (SlotParts.Slot == Slot)
			{
				return SlotParts;
			}
		}

		FSDMutableCatalogSlotParts& NewSlotParts = PackCatalog.Slots.AddDefaulted_GetRef();
		NewSlotParts.Slot = Slot;
		return NewSlotParts;
	}

	FName MakeUniquePartId(const FName BaseId, const TSet<FName>& ExistingIds, int32& DuplicateCount)
	{
		if (!ExistingIds.Contains(BaseId))
		{
			return BaseId;
		}

		++DuplicateCount;

		const FString BaseString = BaseId.ToString();
		for (int32 Suffix = 1; Suffix < MAX_int32; ++Suffix)
		{
			const FName Candidate(*FString::Printf(TEXT("%s_%d"), *BaseString, Suffix));
			if (!ExistingIds.Contains(Candidate))
			{
				return Candidate;
			}
		}

		return BaseId;
	}

	TArray<FString> ResolveRootPaths(const TArray<FString>& PrimaryRootPaths, const TArray<FString>& FallbackRootPaths)
	{
		if (!PrimaryRootPaths.IsEmpty())
		{
			return PrimaryRootPaths;
		}

		if (!FallbackRootPaths.IsEmpty())
		{
			return FallbackRootPaths;
		}

		return { TEXT("/Game/Sidekicks") };
	}

	void PopulateSkeletalMeshScanFilter(FARFilter& Filter, const TArray<FString>& RootPaths)
	{
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());

		for (const FString& RootPath : RootPaths)
		{
			if (!RootPath.IsEmpty())
			{
				Filter.PackagePaths.Add(FName(*RootPath));
			}
		}
	}

	TMap<FName, FSoftObjectPath> BuildIconThumbnailMap()
	{
		TMap<FName, FSoftObjectPath> ThumbnailsByAssetName;

		// Icons follow T_Icon_<SkeletalMeshAssetName>; store soft paths so catalog rows stay cheap to load.
		FARFilter Filter;
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(UTexture2D::StaticClass()->GetClassPathName());
		Filter.PackagePaths.Add(FName(TEXT("/Game/SidekicksMutable/Icons")));

		TArray<FAssetData> IconAssets;
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		AssetRegistryModule.Get().GetAssets(Filter, IconAssets);

		for (const FAssetData& IconAsset : IconAssets)
		{
			ThumbnailsByAssetName.Add(IconAsset.AssetName, IconAsset.GetSoftObjectPath());
		}

		return ThumbnailsByAssetName;
	}

	TSoftObjectPtr<UTexture2D> FindIconThumbnailForMesh(
		const FString& MeshAssetName,
		const TMap<FName, FSoftObjectPath>& ThumbnailsByAssetName)
	{
		const FName ExpectedIconName(*FString::Printf(TEXT("T_Icon_%s"), *MeshAssetName));
		if (const FSoftObjectPath* ThumbnailPath = ThumbnailsByAssetName.Find(ExpectedIconName))
		{
			return TSoftObjectPtr<UTexture2D>(*ThumbnailPath);
		}

		return TSoftObjectPtr<UTexture2D>();
	}

	void GetSkeletalMeshesFromAssetRegistry(const TArray<FString>& RootPaths, TSet<FSoftObjectPath>& OutMeshPaths)
	{
		FARFilter Filter;
		PopulateSkeletalMeshScanFilter(Filter, RootPaths);

		TArray<FAssetData> MeshAssets;
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		AssetRegistryModule.Get().GetAssets(Filter, MeshAssets);

		for (const FAssetData& MeshAsset : MeshAssets)
		{
			OutMeshPaths.Add(MeshAsset.GetSoftObjectPath());
		}
	}

	void GetSkeletalMeshesFromCatalog(const USDMutableCatalog& Catalog, TSet<FSoftObjectPath>& OutMeshPaths)
	{
		ForEachCatalogPack(Catalog, [&OutMeshPaths](const USDMutableCatalogPack& Pack)
		{
			for (const FSDMutableCatalogSlotParts& SlotParts : Pack.Slots)
			{
				for (const FSDMutableCatalogPartEntry& Part : SlotParts.Parts)
				{
					const FSoftObjectPath MeshPath = Part.SkeletalMesh.ToSoftObjectPath();
					if (MeshPath.IsValid())
					{
						OutMeshPaths.Add(MeshPath);
					}
				}
			}

			return false;
		});
	}

	TArray<FString> SortedPathStrings(const TSet<FSoftObjectPath>& Paths)
	{
		TArray<FString> Result;
		Result.Reserve(Paths.Num());

		for (const FSoftObjectPath& Path : Paths)
		{
			Result.Add(Path.ToString());
		}

		Result.Sort();
		return Result;
	}

	void LogPathList(const TCHAR* Header, const TArray<FString>& Paths)
	{
		UE_LOG(LogSDMutableCatalog, Warning, TEXT("%s (%d):"), Header, Paths.Num());

		for (const FString& Path : Paths)
		{
			UE_LOG(LogSDMutableCatalog, Warning, TEXT("  %s"), *Path);
		}
	}

	FString GetPartSortKey(const FSDMutableCatalogPartEntry& Part)
	{
		if (!Part.SourceAssetPath.IsEmpty())
		{
			return Part.SourceAssetPath;
		}

		const FString DisplayName = Part.DisplayName.ToString();
		if (!DisplayName.IsEmpty())
		{
			return DisplayName;
		}

		return Part.PartId.ToString();
	}

	int32 CompareNaturalAscending(const FString& Left, const FString& Right)
	{
		int32 LeftIndex = 0;
		int32 RightIndex = 0;

		while (LeftIndex < Left.Len() && RightIndex < Right.Len())
		{
			const TCHAR LeftChar = Left[LeftIndex];
			const TCHAR RightChar = Right[RightIndex];

			if (FChar::IsDigit(LeftChar) && FChar::IsDigit(RightChar))
			{
				int32 LeftNumberEnd = LeftIndex;
				while (LeftNumberEnd < Left.Len() && FChar::IsDigit(Left[LeftNumberEnd]))
				{
					++LeftNumberEnd;
				}

				int32 RightNumberEnd = RightIndex;
				while (RightNumberEnd < Right.Len() && FChar::IsDigit(Right[RightNumberEnd]))
				{
					++RightNumberEnd;
				}

				const int64 LeftValue = FCString::Atoi64(*Left.Mid(LeftIndex, LeftNumberEnd - LeftIndex));
				const int64 RightValue = FCString::Atoi64(*Right.Mid(RightIndex, RightNumberEnd - RightIndex));
				if (LeftValue != RightValue)
				{
					return LeftValue < RightValue ? -1 : 1;
				}

				const int32 LeftDigitCount = LeftNumberEnd - LeftIndex;
				const int32 RightDigitCount = RightNumberEnd - RightIndex;
				if (LeftDigitCount != RightDigitCount)
				{
					return LeftDigitCount < RightDigitCount ? -1 : 1;
				}

				LeftIndex = LeftNumberEnd;
				RightIndex = RightNumberEnd;
				continue;
			}

			const TCHAR LeftLower = FChar::ToLower(LeftChar);
			const TCHAR RightLower = FChar::ToLower(RightChar);
			if (LeftLower != RightLower)
			{
				return LeftLower < RightLower ? -1 : 1;
			}

			++LeftIndex;
			++RightIndex;
		}

		if (Left.Len() == Right.Len())
		{
			return 0;
		}

		return Left.Len() < Right.Len() ? -1 : 1;
	}

	bool CatalogPartLess(const FSDMutableCatalogPartEntry& Left, const FSDMutableCatalogPartEntry& Right)
	{
		const int32 KeyComparison = CompareNaturalAscending(GetPartSortKey(Left), GetPartSortKey(Right));
		if (KeyComparison != 0)
		{
			return KeyComparison < 0;
		}

		return CompareNaturalAscending(Left.PartId.ToString(), Right.PartId.ToString()) < 0;
	}

	void SortCatalogPartsWithinPack(USDMutableCatalogPack& PackCatalog)
	{
		for (FSDMutableCatalogSlotParts& SlotParts : PackCatalog.Slots)
		{
			SlotParts.Parts.Sort([](const FSDMutableCatalogPartEntry& Left, const FSDMutableCatalogPartEntry& Right)
			{
				return CatalogPartLess(Left, Right);
			});
		}
	}

	FSDMutableCatalogScanStats RebuildPackCatalogFromAssetRegistryInternal(
		USDMutableCatalogPack& PackCatalog,
		const TArray<FString>& RootPaths,
		const TArray<FSDMutableSlotNameRule>& SlotNameRules,
		const bool bClearExisting)
	{
		FSDMutableCatalogScanStats Stats;

		if (bClearExisting)
		{
			PackCatalog.Modify();
			PackCatalog.Slots.Reset();
		}

		TSet<FName> ExistingPartIds;
		for (const FSDMutableCatalogSlotParts& SlotParts : PackCatalog.Slots)
		{
			for (const FSDMutableCatalogPartEntry& ExistingPart : SlotParts.Parts)
			{
				ExistingPartIds.Add(ExistingPart.PartId);
			}
		}

		TArray<FSDMutableSlotNameRule> ResolvedSlotRules = SlotNameRules;
		if (ResolvedSlotRules.IsEmpty())
		{
			ResolvedSlotRules = MakeDefaultSlotRules();
		}

		FARFilter Filter;
		PopulateSkeletalMeshScanFilter(Filter, ResolveRootPaths(RootPaths, PackCatalog.ScanRootPaths));

		TArray<FAssetData> MeshAssets;
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		AssetRegistryModule.Get().GetAssets(Filter, MeshAssets);

		Stats.NumAssetsScanned = MeshAssets.Num();
		PackCatalog.Modify();

		const FName PackId = PackCatalog.PackId.IsNone() ? FName(*PackCatalog.GetName()) : PackCatalog.PackId;
		const TArray<FName> PackAssetNameTokens = GetPackAssetNameTokens(PackCatalog);
		const TMap<int32, FName> ColorNamesByIndex = LoadSidekicksColorNameMap();
		const TMap<FName, FSoftObjectPath> ThumbnailsByAssetName = BuildIconThumbnailMap();

		// Pack rebuilds filter by pack tokens, infer slots from filename tokens, and keep mesh references soft.
		for (const FAssetData& MeshAsset : MeshAssets)
		{
			const FString AssetName = MeshAsset.AssetName.ToString();
			if (!AssetMatchesAnyToken(AssetName, PackAssetNameTokens))
			{
				continue;
			}

			const ESDMutablePartSlot Slot = InferSlotFromAssetName(AssetName, ResolvedSlotRules);
			const FName BasePartId = MakeReadablePartId(AssetName, Slot);
			const FName PartId = MakeUniquePartId(BasePartId, ExistingPartIds, Stats.NumDuplicatePartIds);
			ExistingPartIds.Add(PartId);

			FSDMutableCatalogSlotParts& SlotParts = FindOrAddSlotParts(PackCatalog, Slot);
			FSDMutableCatalogPartEntry& Entry = SlotParts.Parts.AddDefaulted_GetRef();
			Entry.PartId = PartId;
			Entry.MutableOptionId = PartId;
			Entry.PackId = PackId;
			Entry.DisplayName = MakeReadableDisplayName(AssetName, Slot);
			Entry.Tags.AppendTags(PackCatalog.PackTags);
			Entry.SkeletalMesh = TSoftObjectPtr<USkeletalMesh>(MeshAsset.GetSoftObjectPath());
			Entry.UIThumbnail = FindIconThumbnailForMesh(AssetName, ThumbnailsByAssetName);
			Entry.SourceAssetPath = MeshAsset.GetSoftObjectPath().ToString();
			if (!Entry.UIThumbnail.IsNull())
			{
				++Stats.NumThumbnailsMatched;
			}

			if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(MeshAsset.GetAsset()))
			{
				// Loading the mesh here is intentional so the catalog can expose color-slot metadata in the UI.
				Entry.ColorProperties = BuildColorPropertiesFromMesh(Mesh, ColorNamesByIndex);
				Stats.NumColorPropertiesAdded += Entry.ColorProperties.Num();
			}

			if (Slot == ESDMutablePartSlot::None)
			{
				++Stats.NumUnresolvedSlots;
			}

			++Stats.NumEntriesAdded;
		}

		SortCatalogPartsWithinPack(PackCatalog);
		PackCatalog.MarkPackageDirty();
		return Stats;
	}
#endif
}

void USDMutableCatalogPack::RebuildPackCatalogFromAssetRegistry()
{
#if WITH_EDITOR
	const FSDMutableCatalogScanStats Stats = RebuildPackCatalogFromAssetRegistryInternal(*this, ScanRootPaths, MakeDefaultSlotRules(), true);
	UE_LOG(
		LogSDMutableCatalog,
		Log,
		TEXT("Rebuilt pack catalog %s. Scanned=%d Added=%d ColorProperties=%d Thumbnails=%d Duplicates=%d UnresolvedSlots=%d"),
		*GetName(),
		Stats.NumAssetsScanned,
		Stats.NumEntriesAdded,
		Stats.NumColorPropertiesAdded,
		Stats.NumThumbnailsMatched,
		Stats.NumDuplicatePartIds,
		Stats.NumUnresolvedSlots);
#else
	UE_LOG(LogSDMutableCatalog, Warning, TEXT("RebuildPackCatalogFromAssetRegistry is editor-only."));
#endif
}

void USDMutableCatalog::RebuildCatalogFromAssetRegistry()
{
#if WITH_EDITOR
	Modify();
	SpeciesPackCatalogs.Reset();
	OutfitPackCatalogs.Reset();
	SharedPackCatalogs.Reset();
	UnknownPackCatalogs.Reset();

	FARFilter Filter;
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(USDMutableCatalogPack::StaticClass()->GetClassPathName());

	for (const FString& RootPath : ResolveRootPaths(PackCatalogScanRootPaths, {}))
	{
		if (!RootPath.IsEmpty())
		{
			Filter.PackagePaths.Add(FName(*RootPath));
		}
	}

	TArray<FAssetData> PackAssets;
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetRegistryModule.Get().GetAssets(Filter, PackAssets);

	int32 NumPacksAdded = 0;
	for (const FAssetData& PackAsset : PackAssets)
	{
		USDMutableCatalogPack* Pack = Cast<USDMutableCatalogPack>(PackAsset.GetAsset());
		if (!Pack)
		{
			continue;
		}

		TSoftObjectPtr<USDMutableCatalogPack> PackPtr(PackAsset.GetSoftObjectPath());
		switch (Pack->PackType)
		{
		case ESDMutableCatalogPackType::Species:
			SpeciesPackCatalogs.AddUnique(PackPtr);
			break;
		case ESDMutableCatalogPackType::Outfit:
			OutfitPackCatalogs.AddUnique(PackPtr);
			break;
		case ESDMutableCatalogPackType::Shared:
			SharedPackCatalogs.AddUnique(PackPtr);
			break;
		default:
			UnknownPackCatalogs.AddUnique(PackPtr);
			break;
		}

		++NumPacksAdded;
	}

	MarkPackageDirty();
	UE_LOG(LogSDMutableCatalog, Log, TEXT("Rebuilt root catalog %s. PackScanRoots=%d Scanned=%d Added=%d"), *GetName(), ResolveRootPaths(PackCatalogScanRootPaths, {}).Num(), PackAssets.Num(), NumPacksAdded);
#else
	UE_LOG(LogSDMutableCatalog, Warning, TEXT("RebuildCatalogFromAssetRegistry is editor-only."));
#endif
}

void USDMutableCatalog::RebuildAllPackCatalogsFromAssetRegistry()
{
#if WITH_EDITOR
	int32 NumPacksProcessed = 0;
	FSDMutableCatalogScanStats TotalStats;
	TArray<FSDMutableSlotNameRule> Rules = SlotNameRules;
	if (Rules.IsEmpty())
	{
		Rules = MakeDefaultSlotRules();
	}

	auto RebuildPackGroup = [this, &NumPacksProcessed, &TotalStats, &Rules](const TArray<TSoftObjectPtr<USDMutableCatalogPack>>& PackGroup)
	{
		for (const TSoftObjectPtr<USDMutableCatalogPack>& PackPtr : PackGroup)
		{
			USDMutableCatalogPack* Pack = PackPtr.LoadSynchronous();
			if (!Pack)
			{
				continue;
			}

			const FSDMutableCatalogScanStats Stats = RebuildPackCatalogFromAssetRegistryInternal(
				*Pack,
				ResolveRootPaths(Pack->ScanRootPaths, ScanRootPaths),
				Rules,
				true);

			TotalStats.NumAssetsScanned += Stats.NumAssetsScanned;
			TotalStats.NumEntriesAdded += Stats.NumEntriesAdded;
			TotalStats.NumDuplicatePartIds += Stats.NumDuplicatePartIds;
			TotalStats.NumUnresolvedSlots += Stats.NumUnresolvedSlots;
			TotalStats.NumColorPropertiesAdded += Stats.NumColorPropertiesAdded;
			TotalStats.NumThumbnailsMatched += Stats.NumThumbnailsMatched;
			++NumPacksProcessed;
		}
	};

	RebuildPackGroup(SpeciesPackCatalogs);
	RebuildPackGroup(OutfitPackCatalogs);
	RebuildPackGroup(SharedPackCatalogs);
	RebuildPackGroup(UnknownPackCatalogs);

	UE_LOG(
		LogSDMutableCatalog,
		Log,
		TEXT("Rebuilt all pack catalogs from root %s. Packs=%d Scanned=%d Added=%d ColorProperties=%d Thumbnails=%d Duplicates=%d UnresolvedSlots=%d"),
		*GetName(),
		NumPacksProcessed,
		TotalStats.NumAssetsScanned,
		TotalStats.NumEntriesAdded,
		TotalStats.NumColorPropertiesAdded,
		TotalStats.NumThumbnailsMatched,
		TotalStats.NumDuplicatePartIds,
		TotalStats.NumUnresolvedSlots);
#else
	UE_LOG(LogSDMutableCatalog, Warning, TEXT("RebuildAllPackCatalogsFromAssetRegistry is editor-only."));
#endif
}

void USDMutableCatalog::ValidateCatalogAgainstAssetRegistry()
{
#if WITH_EDITOR
	TSet<FSoftObjectPath> AssetRegistryMeshPaths;
	TSet<FSoftObjectPath> CatalogMeshPaths;

	GetSkeletalMeshesFromAssetRegistry(ResolveRootPaths(ScanRootPaths, {}), AssetRegistryMeshPaths);
	GetSkeletalMeshesFromCatalog(*this, CatalogMeshPaths);

	TSet<FSoftObjectPath> MissingFromCatalog = AssetRegistryMeshPaths.Difference(CatalogMeshPaths);
	TSet<FSoftObjectPath> StaleInCatalog = CatalogMeshPaths.Difference(AssetRegistryMeshPaths);

	const TArray<FString> MissingFromCatalogPaths = SortedPathStrings(MissingFromCatalog);
	const TArray<FString> StaleInCatalogPaths = SortedPathStrings(StaleInCatalog);

	UE_LOG(
		LogSDMutableCatalog,
		Log,
		TEXT("Catalog validation for %s. AssetRegistryMeshes=%d CatalogMeshRefs=%d MissingFromCatalog=%d StaleInCatalog=%d"),
		*GetName(),
		AssetRegistryMeshPaths.Num(),
		CatalogMeshPaths.Num(),
		MissingFromCatalogPaths.Num(),
		StaleInCatalogPaths.Num());

	if (MissingFromCatalogPaths.IsEmpty() && StaleInCatalogPaths.IsEmpty())
	{
		UE_LOG(LogSDMutableCatalog, Log, TEXT("Catalog validation passed. Catalog mesh refs match skeletal meshes under ScanRootPaths."));
		return;
	}

	if (!MissingFromCatalogPaths.IsEmpty())
	{
		LogPathList(TEXT("Skeletal meshes under ScanRootPaths that are missing from catalog"), MissingFromCatalogPaths);
	}

	if (!StaleInCatalogPaths.IsEmpty())
	{
		LogPathList(TEXT("Catalog skeletal mesh refs that are not under ScanRootPaths / not found by Asset Registry"), StaleInCatalogPaths);
	}
#else
	UE_LOG(LogSDMutableCatalog, Warning, TEXT("ValidateCatalogAgainstAssetRegistry is editor-only."));
#endif
}

void USDMutableCatalog::FixSyntySidekickSkeletonTransformBones()
{
#if WITH_EDITOR
	FARFilter Filter;
	PopulateSkeletalMeshScanFilter(Filter, ResolveRootPaths(ScanRootPaths, {}));

	TArray<FAssetData> MeshAssets;
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetRegistryModule.Get().GetAssets(Filter, MeshAssets);

	FSDMutableSkeletonFixStats Stats;
	Stats.NumAssetsScanned = MeshAssets.Num();

	const FScopedTransaction Transaction(NSLOCTEXT("SDMutableCatalog", "FixSyntySidekickSkeletonTransformBones", "Fix Synty Sidekick transform bones"));

	for (const FAssetData& MeshAsset : MeshAssets)
	{
		USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(MeshAsset.GetAsset());
		if (!SkeletalMesh)
		{
			++Stats.NumFailures;
			UE_LOG(LogSDMutableCatalog, Warning, TEXT("Failed to load skeletal mesh for transform-bone scan: %s"), *MeshAsset.GetSoftObjectPath().ToString());
			continue;
		}

		++Stats.NumMeshesLoaded;

		const TArray<FName> BadBones = FindSyntyTransformBreakerBones(*SkeletalMesh);
		if (BadBones.IsEmpty())
		{
			UE_LOG(LogSDMutableCatalog, Verbose, TEXT("%s: no Synty transform breaker bones found; skipping edit."), *SkeletalMesh->GetName());
			continue;
		}

		++Stats.NumMeshesWithTransformBones;
		UE_LOG(
			LogSDMutableCatalog,
			Log,
			TEXT("%s: found Synty transform breaker bones [%s]; repairing."),
			*SkeletalMesh->GetName(),
			*FString::JoinBy(BadBones, TEXT(", "), [](const FName BoneName) { return BoneName.ToString(); }));

		USkeleton* ModifiedSkeleton = nullptr;
		if (!FixSyntyTransformBreakerBones(*SkeletalMesh, BadBones, ModifiedSkeleton))
		{
			++Stats.NumFailures;
			continue;
		}

		++Stats.NumMeshesChanged;
		if (SaveAssetPackage(*SkeletalMesh))
		{
			++Stats.NumMeshesSaved;
		}
		else
		{
			++Stats.NumFailures;
		}

		if (ModifiedSkeleton && ModifiedSkeleton->GetPackage() != SkeletalMesh->GetPackage())
		{
			if (SaveAssetPackage(*ModifiedSkeleton))
			{
				++Stats.NumSkeletonsSaved;
			}
			else
			{
				++Stats.NumFailures;
			}
		}
	}

	UE_LOG(
		LogSDMutableCatalog,
		Log,
		TEXT("Finished Synty Sidekick skeleton transform-bone repair from root catalog %s. Scanned=%d Loaded=%d WithTransformBones=%d Changed=%d MeshesSaved=%d SkeletonsSaved=%d Failures=%d"),
		*GetName(),
		Stats.NumAssetsScanned,
		Stats.NumMeshesLoaded,
		Stats.NumMeshesWithTransformBones,
		Stats.NumMeshesChanged,
		Stats.NumMeshesSaved,
		Stats.NumSkeletonsSaved,
		Stats.NumFailures);
#else
	UE_LOG(LogSDMutableCatalog, Warning, TEXT("FixSyntySidekickSkeletonTransformBones is editor-only."));
#endif
}

bool USDMutableCatalog::GetSlotTable(const ESDMutablePartSlot Slot, FSDMutableSlotTable& OutSlotTable) const
{
	for (const FSDMutableSlotTable& SlotTable : PartTables)
	{
		if (SlotTable.Slot == Slot)
		{
			OutSlotTable = SlotTable;
			return true;
		}
	}

	return false;
}

bool USDMutableCatalog::FindPartById(const FName PartId, FSDMutableCatalogPartEntry& OutPart) const
{
	return ForEachCatalogPack(*this, [&OutPart, PartId](const USDMutableCatalogPack& Pack)
	{
		for (const FSDMutableCatalogSlotParts& SlotParts : Pack.Slots)
		{
			for (const FSDMutableCatalogPartEntry& Part : SlotParts.Parts)
			{
				if (Part.PartId == PartId)
				{
					OutPart = Part;
					return true;
				}
			}
		}

		return false;
	});
}

void USDMutableCatalog::GetPartsForSlot(const ESDMutablePartSlot Slot, TArray<FSDMutableCatalogPartEntry>& OutParts) const
{
	OutParts.Reset();

	ForEachCatalogPack(*this, [&OutParts, Slot](const USDMutableCatalogPack& Pack)
	{
		for (const FSDMutableCatalogSlotParts& SlotParts : Pack.Slots)
		{
			if (SlotParts.Slot == Slot)
			{
				OutParts.Append(SlotParts.Parts);
			}
		}

		return false;
	});
}

USkeletalMesh* USDMutableCatalog::LoadPartMesh(const FName PartId) const
{
	FSDMutableCatalogPartEntry Part;
	if (!FindPartById(PartId, Part))
	{
		return nullptr;
	}

	return Part.SkeletalMesh.LoadSynchronous();
}

USkeletalMesh* USDMutableCatalog::LoadEmptySkeletalMesh() const
{
	return EmptySkeletalMesh.LoadSynchronous();
}
