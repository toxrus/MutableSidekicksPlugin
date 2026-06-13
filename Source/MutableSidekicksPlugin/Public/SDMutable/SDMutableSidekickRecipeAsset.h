#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SDMutableColorPreset.h"
#include "SDMutableTypes.h"
#include "SDMutableSidekickRecipeAsset.generated.h"

class UCustomizableObjectInstance;
class UTexture2D;

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

UCLASS(BlueprintType)
class MUTABLESIDEKICKSPLUGIN_API USDMutableSidekickRecipeAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	USDMutableSidekickRecipeAsset();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable")
	FSDMutableSidekickRecipe Recipe;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable")
	FSDMutableColorPalette ColorPalette;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable")
	TSoftObjectPtr<UTexture2D> ColorTexture;

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
