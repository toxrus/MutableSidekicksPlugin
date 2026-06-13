#include "SDMutable/SDMutableSidekickRecipeAsset.h"

#include "Engine/Texture2D.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MuCO/CustomizableObjectInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogSDMutableRecipeExchange, Log, All);

namespace
{
	FString MakeDefaultJsonExchangePath(const UObject& Asset)
	{
		return FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("SidekicksMutable/RecipeExports"),
			Asset.GetName() + TEXT(".json"));
	}

	FString ResolveJsonExchangePath(const USDMutableSidekickRecipeAsset& Asset)
	{
		return Asset.JsonExchangeFilePath.IsEmpty() ? MakeDefaultJsonExchangePath(Asset) : Asset.JsonExchangeFilePath;
	}
}

USDMutableSidekickRecipeAsset::USDMutableSidekickRecipeAsset()
{
	ColorPalette.EnsureColorSlotCount();
}

FSDMutableRecipeJsonExchange USDMutableSidekickRecipeAsset::MakeRecipeJsonExchange() const
{
	FSDMutableRecipeJsonExchange Exchange;
	Exchange.ExchangeVersion = 1;
	Exchange.SourceAssetName = GetName();
	Exchange.Recipe = Recipe;
	Exchange.ColorPalette = ColorPalette;
	Exchange.ColorPalette.EnsureColorSlotCount();
	Exchange.ColorTexturePath = ColorTexture.ToSoftObjectPath().ToString();
	Exchange.CustomizableObjectInstancePath = CustomizableObjectInstance.ToSoftObjectPath().ToString();
	return Exchange;
}

void USDMutableSidekickRecipeAsset::ApplyRecipeJsonExchange(const FSDMutableRecipeJsonExchange& Exchange)
{
	Modify();
	Recipe = Exchange.Recipe;
	ColorPalette = Exchange.ColorPalette;
	ColorPalette.EnsureColorSlotCount();
	ColorTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(Exchange.ColorTexturePath));
	CustomizableObjectInstance = TSoftObjectPtr<UCustomizableObjectInstance>(FSoftObjectPath(Exchange.CustomizableObjectInstancePath));
	MarkPackageDirty();
}

void USDMutableSidekickRecipeAsset::ExportRecipeToJson()
{
	const FSDMutableRecipeJsonExchange Exchange = MakeRecipeJsonExchange();

	FString Json;
	if (!FJsonObjectConverter::UStructToJsonObjectString(
		FSDMutableRecipeJsonExchange::StaticStruct(),
		&Exchange,
		Json,
		0,
		0,
		0,
		nullptr,
		true))
	{
		UE_LOG(LogSDMutableRecipeExchange, Warning, TEXT("Failed to serialize Sidekicks recipe JSON for %s."), *GetName());
		return;
	}

	const FString OutputPath = ResolveJsonExchangePath(*this);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
	if (!FFileHelper::SaveStringToFile(Json, *OutputPath))
	{
		UE_LOG(LogSDMutableRecipeExchange, Warning, TEXT("Failed to write Sidekicks recipe JSON: %s"), *OutputPath);
		return;
	}

	JsonExchangeFilePath = OutputPath;
	UE_LOG(LogSDMutableRecipeExchange, Log, TEXT("Exported Sidekicks recipe JSON: %s"), *OutputPath);
}

void USDMutableSidekickRecipeAsset::ImportRecipeFromJson()
{
	const FString InputPath = ResolveJsonExchangePath(*this);

	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *InputPath))
	{
		UE_LOG(LogSDMutableRecipeExchange, Warning, TEXT("Failed to read Sidekicks recipe JSON: %s"), *InputPath);
		return;
	}

	FSDMutableRecipeJsonExchange Exchange;
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(
		Json,
		&Exchange,
		0,
		0))
	{
		UE_LOG(LogSDMutableRecipeExchange, Warning, TEXT("Failed to parse Sidekicks recipe JSON: %s"), *InputPath);
		return;
	}

	ApplyRecipeJsonExchange(Exchange);
	JsonExchangeFilePath = InputPath;
	UE_LOG(LogSDMutableRecipeExchange, Log, TEXT("Imported Sidekicks recipe JSON: %s"), *InputPath);
}
