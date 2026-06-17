#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SDMutableTypes.h"
#include "SDMutableCatalog.generated.h"

class USkeletalMesh;
class UTexture2D;
class USDMutableCatalogPack;

/** Coarse pack grouping used by the root catalog to expose species, outfit, and shared content separately. */
UENUM(BlueprintType)
enum class ESDMutableCatalogPackType : uint8
{
	Unknown,
	Species,
	Outfit,
	Shared
};

/** Maps a plugin slot to the Mutable skeletal mesh parameter name written on a COI. */
USTRUCT(BlueprintType)
struct FSDMutableSlotTable
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable")
	ESDMutablePartSlot Slot = ESDMutablePartSlot::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable")
	FName MutableParameterName = NAME_None;
};

/** Filename token rule used by catalog rebuilds to infer which Sidekicks slot a mesh belongs to. */
USTRUCT(BlueprintType)
struct FSDMutableSlotNameRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable")
	FName Token = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable")
	ESDMutablePartSlot Slot = ESDMutablePartSlot::None;
};

/** Palette slots sampled from mesh UVs so the editor can highlight which color cells a part uses. */
USTRUCT(BlueprintType)
struct FSDMutablePartColorProperty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable|Color")
	int32 PaletteIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable|Color")
	FName ColorName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable|Color")
	FIntPoint PaletteCell = FIntPoint::ZeroValue;
};

/** One selectable Sidekicks mesh entry; mesh and thumbnail references stay soft until the user selects them. */
USTRUCT(BlueprintType)
struct FSDMutableCatalogPartEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable")
	FName PartId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable")
	FName MutableOptionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable")
	FName PackId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable")
	FGameplayTagContainer Tags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable")
	TSoftObjectPtr<USkeletalMesh> SkeletalMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable|UI")
	TSoftObjectPtr<UTexture2D> UIThumbnail;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable|Color")
	TArray<FSDMutablePartColorProperty> ColorProperties;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable")
	FString SourceAssetPath;
};

/** All selectable parts for one slot inside a pack catalog. */
USTRUCT(BlueprintType)
struct FSDMutableCatalogSlotParts
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable")
	ESDMutablePartSlot Slot = ESDMutablePartSlot::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable")
	TArray<FSDMutableCatalogPartEntry> Parts;
};

/** Per-pack DataAsset containing the scanned mesh entries for one species/outfit/shared content group. */
UCLASS(BlueprintType)
class MUTABLESIDEKICKSPLUGIN_API USDMutableCatalogPack : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable|Pack")
	FName PackId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable|Pack")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable|Pack")
	ESDMutableCatalogPackType PackType = ESDMutableCatalogPackType::Unknown;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable|Pack")
	FGameplayTagContainer PackTags;

	/** Optional scan roots for this pack; when empty, root catalog scan roots are used by rebuild-all. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable|Import")
	TArray<FString> ScanRootPaths;

	/** Additional filename tokens that identify meshes belonging to this pack. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable|Import")
	TArray<FName> AssetNameTokens;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable")
	TArray<FSDMutableCatalogSlotParts> Slots;

	UFUNCTION(CallInEditor, Category="SDMutable|Catalog")
	void RebuildPackCatalogFromAssetRegistry();
};

/** Root catalog that ties together scan settings, slot rules, empty mesh defaults, and all pack catalogs. */
UCLASS(BlueprintType)
class MUTABLESIDEKICKSPLUGIN_API USDMutableCatalog : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Mesh applied when a recipe clears a slot; Mutable skeletal mesh parameters need a valid mesh, not nullptr. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable|Defaults")
	TSoftObjectPtr<USkeletalMesh> EmptySkeletalMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Sidekicks/Base/SKM_Sidekicks_Empty.SKM_Sidekicks_Empty")));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable|Defaults")
	TSoftObjectPtr<UTexture2D> EmptySkeletalMeshUIThumbnail;

	/** Mesh scan roots for imported Synty assets. Empty shipped assets fall back to /Game/Sidekicks in code paths that resolve roots. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable|Import")
	TArray<FString> ScanRootPaths = { TEXT("/Game/Sidekicks") };

	/** Optional roots used only to discover pack DataAssets, not the meshes inside already referenced packs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable|Import")
	TArray<FString> PackCatalogScanRootPaths = { TEXT("/Game/Mutable/DataAssets") };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable|Import")
	TArray<FSDMutableSlotNameRule> SlotNameRules = {
		{ TEXT("01HEAD"), ESDMutablePartSlot::Head },
		{ TEXT("02HAIR"), ESDMutablePartSlot::Hair },
		{ TEXT("03EBRL"), ESDMutablePartSlot::EyebrowLeft },
		{ TEXT("04EBRR"), ESDMutablePartSlot::EyebrowRight },
		{ TEXT("05EYEL"), ESDMutablePartSlot::EyeLeft },
		{ TEXT("06EYER"), ESDMutablePartSlot::EyeRight },
		{ TEXT("07EARL"), ESDMutablePartSlot::EarLeft },
		{ TEXT("08EARR"), ESDMutablePartSlot::EarRight },
		{ TEXT("09FCHR"), ESDMutablePartSlot::FacialHair },
		{ TEXT("10TORS"), ESDMutablePartSlot::Torso },
		{ TEXT("11AUPL"), ESDMutablePartSlot::ArmUpperLeft },
		{ TEXT("12AUPR"), ESDMutablePartSlot::ArmUpperRight },
		{ TEXT("13ALWL"), ESDMutablePartSlot::ArmLowerLeft },
		{ TEXT("14ALWR"), ESDMutablePartSlot::ArmLowerRight },
		{ TEXT("15HNDL"), ESDMutablePartSlot::HandLeft },
		{ TEXT("16HNDR"), ESDMutablePartSlot::HandRight },
		{ TEXT("17HIPS"), ESDMutablePartSlot::Hips },
		{ TEXT("18LEGL"), ESDMutablePartSlot::LegLeft },
		{ TEXT("19LEGR"), ESDMutablePartSlot::LegRight },
		{ TEXT("20FOTL"), ESDMutablePartSlot::FootLeft },
		{ TEXT("21FOTR"), ESDMutablePartSlot::FootRight },
		{ TEXT("22AHED"), ESDMutablePartSlot::AttachmentHead },
		{ TEXT("23AFAC"), ESDMutablePartSlot::AttachmentFace },
		{ TEXT("24ABAC"), ESDMutablePartSlot::AttachmentBack },
		{ TEXT("25AHPF"), ESDMutablePartSlot::AttachmentHipsFront },
		{ TEXT("26AHPB"), ESDMutablePartSlot::AttachmentHipsBack },
		{ TEXT("27AHPL"), ESDMutablePartSlot::AttachmentHipsLeft },
		{ TEXT("28AHPR"), ESDMutablePartSlot::AttachmentHipsRight },
		{ TEXT("29ASHL"), ESDMutablePartSlot::AttachmentShoulderLeft },
		{ TEXT("30ASHR"), ESDMutablePartSlot::AttachmentShoulderRight },
		{ TEXT("31AEBL"), ESDMutablePartSlot::AttachmentElbowLeft },
		{ TEXT("32AEBR"), ESDMutablePartSlot::AttachmentElbowRight },
		{ TEXT("33AKNL"), ESDMutablePartSlot::AttachmentKneeLeft },
		{ TEXT("34AKNR"), ESDMutablePartSlot::AttachmentKneeRight },
		{ TEXT("35NOSE"), ESDMutablePartSlot::Nose },
		{ TEXT("36TETH"), ESDMutablePartSlot::Teeth },
		{ TEXT("TONG"), ESDMutablePartSlot::Tongue },
		{ TEXT("WRAP"), ESDMutablePartSlot::AttachmentWrap }
	};

	/** Mutable parameter table for every visible slot selector. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable", meta=(DisplayName="Slot Templates"))
	TArray<FSDMutableSlotTable> PartTables = {
		{ ESDMutablePartSlot::Hair, TEXT("Hair") },
		{ ESDMutablePartSlot::Head, TEXT("Head") },
		{ ESDMutablePartSlot::EyebrowLeft, TEXT("EyebrowLeft") },
		{ ESDMutablePartSlot::EyebrowRight, TEXT("EyebrowRight") },
		{ ESDMutablePartSlot::EyeLeft, TEXT("EyeLeft") },
		{ ESDMutablePartSlot::EyeRight, TEXT("EyeRight") },
		{ ESDMutablePartSlot::EarLeft, TEXT("EarLeft") },
		{ ESDMutablePartSlot::EarRight, TEXT("EarRight") },
		{ ESDMutablePartSlot::FacialHair, TEXT("FacialHair") },
		{ ESDMutablePartSlot::Nose, TEXT("Nose") },
		{ ESDMutablePartSlot::Teeth, TEXT("Teeth") },
		{ ESDMutablePartSlot::Tongue, TEXT("Tongue") },
		{ ESDMutablePartSlot::Torso, TEXT("Torso") },
		{ ESDMutablePartSlot::ArmUpperLeft, TEXT("ArmUpperLeft") },
		{ ESDMutablePartSlot::ArmUpperRight, TEXT("ArmUpperRight") },
		{ ESDMutablePartSlot::ArmLowerLeft, TEXT("ArmLowerLeft") },
		{ ESDMutablePartSlot::ArmLowerRight, TEXT("ArmLowerRight") },
		{ ESDMutablePartSlot::HandLeft, TEXT("HandLeft") },
		{ ESDMutablePartSlot::HandRight, TEXT("HandRight") },
		{ ESDMutablePartSlot::Hips, TEXT("Hips") },
		{ ESDMutablePartSlot::LegLeft, TEXT("LegLeft") },
		{ ESDMutablePartSlot::LegRight, TEXT("LegRight") },
		{ ESDMutablePartSlot::FootLeft, TEXT("FootLeft") },
		{ ESDMutablePartSlot::FootRight, TEXT("FootRight") },
		{ ESDMutablePartSlot::AttachmentHead, TEXT("AttachmentHead") },
		{ ESDMutablePartSlot::AttachmentFace, TEXT("AttachmentFace") },
		{ ESDMutablePartSlot::AttachmentBack, TEXT("AttachmentBack") },
		{ ESDMutablePartSlot::AttachmentShoulderLeft, TEXT("AttachmentShoulderLeft") },
		{ ESDMutablePartSlot::AttachmentShoulderRight, TEXT("AttachmentShoulderRight") },
		{ ESDMutablePartSlot::AttachmentElbowLeft, TEXT("AttachmentElbowLeft") },
		{ ESDMutablePartSlot::AttachmentElbowRight, TEXT("AttachmentElbowRight") },
		{ ESDMutablePartSlot::AttachmentWrap, TEXT("AttachmentWrap") },
		{ ESDMutablePartSlot::AttachmentHipsFront, TEXT("AttachmentHipsFront") },
		{ ESDMutablePartSlot::AttachmentHipsBack, TEXT("AttachmentHipsBack") },
		{ ESDMutablePartSlot::AttachmentHipsLeft, TEXT("AttachmentHipsLeft") },
		{ ESDMutablePartSlot::AttachmentHipsRight, TEXT("AttachmentHipsRight") },
		{ ESDMutablePartSlot::AttachmentKneeLeft, TEXT("AttachmentKneeLeft") },
		{ ESDMutablePartSlot::AttachmentKneeRight, TEXT("AttachmentKneeRight") }
	};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable|Packs")
	TArray<TSoftObjectPtr<USDMutableCatalogPack>> SpeciesPackCatalogs;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable|Packs")
	TArray<TSoftObjectPtr<USDMutableCatalogPack>> OutfitPackCatalogs;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable|Packs")
	TArray<TSoftObjectPtr<USDMutableCatalogPack>> SharedPackCatalogs;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable|Packs")
	TArray<TSoftObjectPtr<USDMutableCatalogPack>> UnknownPackCatalogs;

	/** Discover pack catalog DataAssets under PackCatalogScanRootPaths and sort them into pack groups. */
	UFUNCTION(CallInEditor, Category="SDMutable|Catalog")
	void RebuildCatalogFromAssetRegistry();

	/** Rebuild every referenced pack using this root catalog's scan roots and slot-name rules. */
	UFUNCTION(CallInEditor, Category="SDMutable|Catalog")
	void RebuildAllPackCatalogsFromAssetRegistry();

	/** Compare catalog mesh references against Asset Registry results under ScanRootPaths. */
	UFUNCTION(CallInEditor, Category="SDMutable|Catalog")
	void ValidateCatalogAgainstAssetRegistry();

	/** Repair imported Synty meshes that contain intermediary transform bones incompatible with the runtime skeleton. */
	UFUNCTION(CallInEditor, Category="SDMutable|Catalog")
	void FixSyntySidekickSkeletonTransformBones();

	UFUNCTION(BlueprintPure, Category="SDMutable")
	bool GetSlotTable(ESDMutablePartSlot Slot, FSDMutableSlotTable& OutSlotTable) const;

	UFUNCTION(BlueprintPure, Category="SDMutable")
	bool FindPartById(FName PartId, FSDMutableCatalogPartEntry& OutPart) const;

	UFUNCTION(BlueprintPure, Category="SDMutable")
	void GetPartsForSlot(ESDMutablePartSlot Slot, TArray<FSDMutableCatalogPartEntry>& OutParts) const;

	UFUNCTION(BlueprintCallable, Category="SDMutable")
	USkeletalMesh* LoadPartMesh(FName PartId) const;

	UFUNCTION(BlueprintCallable, Category="SDMutable")
	USkeletalMesh* LoadEmptySkeletalMesh() const;
};
