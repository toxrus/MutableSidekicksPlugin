#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SDMutableColorPreset.generated.h"

class UTexture2D;

/** One Sidekicks palette cell override. Disabled slots resolve to the palette default color. */
USTRUCT(BlueprintType)
struct FSDMutableColorSlot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable|Color")
	bool bOverride = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable|Color")
	FLinearColor Color = FLinearColor::Black;
};

/** Reconstructable 256-slot Sidekicks palette used to derive the 32x32 BaseColor control texture. */
USTRUCT(BlueprintType)
struct MUTABLESIDEKICKSPLUGIN_API FSDMutableColorPalette
{
	GENERATED_BODY()

	static constexpr int32 NumSidekicksColorSlots = 256;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable|Color")
	FLinearColor DefaultColor = FLinearColor::Black;

	/** Fixed-size Sidekicks color slots; each slot becomes one 2x2 patch in generated textures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable|Color", meta=(TitleProperty="Color"))
	TArray<FSDMutableColorSlot> ColorSlots;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable|Color|Random")
	bool bUseRandomSeed = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable|Color|Random", meta=(EditCondition="!bUseRandomSeed"))
	int32 RandomSeed = 12345;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable|Color|Random", meta=(ClampMin="0.0", ClampMax="1.0"))
	float RandomMinSaturation = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable|Color|Random", meta=(ClampMin="0.0", ClampMax="1.0"))
	float RandomMinValue = 0.35f;

	void EnsureColorSlotCount();
	bool SetColorSlot(int32 Index, FLinearColor Color, bool bOverride = true);
	FLinearColor GetResolvedColorSlot(int32 Index) const;
	void GenerateRandomColorPalette();
	void ClearColorPaletteOverrides();
};

/** Utility DataAsset for authoring palettes and generating persistent or transient Sidekicks color textures. */
UCLASS(BlueprintType)
class MUTABLESIDEKICKSPLUGIN_API USDMutableColorPreset : public UDataAsset
{
	GENERATED_BODY()

public:
	USDMutableColorPreset();

	static constexpr int32 NumSidekicksColorSlots = 256;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable|Color")
	FSDMutableColorPalette Palette;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable|Texture")
	FString TexturePackagePath = TEXT("/Game/SidekicksMutable/Generated/ColorTextures");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable|Texture")
	FString TextureAssetName = TEXT("T_SidekicksColor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable|Texture")
	bool bOverwriteExistingTexture = true;

	/** Preview output only; durable recipes store palette values and soft texture references separately. */
	UPROPERTY(VisibleAnywhere, Transient, BlueprintReadOnly, Category="SDMutable|Texture")
	TObjectPtr<UTexture2D> LastGeneratedTexture;

	UFUNCTION(BlueprintCallable, Category="SDMutable|Color")
	void EnsureColorSlotCount();

	UFUNCTION(BlueprintCallable, Category="SDMutable|Color")
	bool SetColorSlot(int32 Index, FLinearColor Color, bool bOverride = true);

	UFUNCTION(CallInEditor, Category="SDMutable|Color")
	void GenerateRandomColorPalette();

	UFUNCTION(CallInEditor, Category="SDMutable|Color")
	void ClearColorPaletteOverrides();

	UFUNCTION(CallInEditor, Category="SDMutable|Texture")
	void BuildColorTextureAsset();

	UFUNCTION(BlueprintCallable, Category="SDMutable|Texture")
	UTexture2D* BuildTransientColorTexture();

	UFUNCTION(CallInEditor, Category="SDMutable|Texture", meta=(DisplayName="Build Transient Color Texture"))
	void BuildTransientColorTextureForPreview();

	UFUNCTION(BlueprintCallable, Category="SDMutable|Texture")
	static UTexture2D* BuildTransientColorTextureFromPalette(const FSDMutableColorPalette& InPalette, UObject* Outer = nullptr);

	UFUNCTION(BlueprintPure, Category="SDMutable|Color")
	FLinearColor GetResolvedColorSlot(int32 Index) const;
};
