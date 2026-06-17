#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SDMutableColorPreset.h"
#include "SDMutableTypes.h"
#include "SDMutableSidekickRecipeAsset.generated.h"

class UCustomizableObjectInstance;
class UTexture2D;

/** Versioned payload used for JSON import/export without requiring the target asset to be present. */
USTRUCT(BlueprintType)
struct FSDMutableRecipeJsonExchange
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable|Exchange")
	int32 ExchangeVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable|Exchange")
	FString SourceAssetName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable|Exchange")
	FSDMutableSidekickRecipe Recipe;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable|Exchange")
	FSDMutableColorPalette ColorPalette;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable|Exchange")
	FString ColorTexturePath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable|Exchange")
	FString CustomizableObjectInstancePath;
};

/** Authoritative saved Sidekick preset: recipe and palette are source data, COI/texture refs are generated companions. */
UCLASS(BlueprintType)
class MUTABLESIDEKICKSPLUGIN_API USDMutableSidekickRecipeAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	USDMutableSidekickRecipeAsset();

	/** Reconstructable mesh, morph, and material state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable")
	FSDMutableSidekickRecipe Recipe;

	/** Reconstructable palette state used to rebuild the BaseColor texture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable")
	FSDMutableColorPalette ColorPalette;

	/** Derived texture asset that can be regenerated from ColorPalette. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable")
	TSoftObjectPtr<UTexture2D> ColorTexture;

	/** Companion Mutable COI that receives the recipe during editor saves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable")
	TSoftObjectPtr<UCustomizableObjectInstance> CustomizableObjectInstance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable|Exchange", meta=(FilePathFilter="JSON files (*.json)|*.json"))
	FString JsonExchangeFilePath;

	UFUNCTION(CallInEditor, BlueprintCallable, Category="SDMutable|Exchange")
	void ExportRecipeToJson();

	UFUNCTION(CallInEditor, BlueprintCallable, Category="SDMutable|Exchange")
	void ImportRecipeFromJson();

	UFUNCTION(BlueprintPure, Category="SDMutable|Exchange")
	FSDMutableRecipeJsonExchange MakeRecipeJsonExchange() const;

	UFUNCTION(BlueprintCallable, Category="SDMutable|Exchange")
	void ApplyRecipeJsonExchange(const FSDMutableRecipeJsonExchange& Exchange);
};
