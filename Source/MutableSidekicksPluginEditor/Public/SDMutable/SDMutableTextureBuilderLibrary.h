#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SDMutable/SDMutableColorPreset.h"
#include "SDMutableTextureBuilderLibrary.generated.h"

class USDMutableColorPreset;
class UTexture2D;

UCLASS()
class MUTABLESIDEKICKSPLUGINEDITOR_API USDMutableTextureBuilderLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="SDMutable|Texture")
	static UTexture2D* BuildSidekicksColorTextureAsset(
		USDMutableColorPreset* ColorPreset,
		const FString& PackagePath = TEXT("/Game/SidekicksMutable/Generated/ColorTextures"),
		const FString& AssetName = TEXT("T_SidekicksColor"),
		bool bOverwriteExisting = true);

	UFUNCTION(BlueprintCallable, Category="SDMutable|Texture")
	static bool WriteSidekicksColorTexture(
		UTexture2D* Texture,
		const USDMutableColorPreset* ColorPreset);

	UFUNCTION(BlueprintCallable, Category="SDMutable|Texture")
	static bool WriteSidekicksColorTextureFromPalette(
		UTexture2D* Texture,
		const FSDMutableColorPalette& Palette);
};
