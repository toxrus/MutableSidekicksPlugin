#include "SDMutable/SDMutableAssetFactories.h"

#include "IAssetTools.h"
#include "MuCO/CustomizableObject.h"
#include "MuCO/CustomizableObjectInstance.h"
#include "SDMutable/SDMutableCatalog.h"
#include "SDMutable/SDMutableDeveloperSettings.h"
#include "SDMutable/SDMutableSidekickRecipeAsset.h"

#define LOCTEXT_NAMESPACE "SDMutableAssetFactories"

namespace
{
	uint32 GSidekicksMutableAssetCategory = EAssetTypeCategories::Misc;
	const TCHAR* DefaultSidekicksCustomizableObjectPath = TEXT("/Game/SidekicksMutable/CO_Sidekicks.CO_Sidekicks");

	UCustomizableObject* LoadSidekicksCustomizableObject()
	{
		// Prefer explicit project settings; the hardcoded path is only a legacy fallback for local projects.
		const USDMutableDeveloperSettings* Settings = GetDefault<USDMutableDeveloperSettings>();
		if (Settings && !Settings->SidekicksCustomizableObject.IsNull())
		{
			if (UCustomizableObject* CustomizableObject = Settings->SidekicksCustomizableObject.LoadSynchronous())
			{
				return CustomizableObject;
			}
		}

		return LoadObject<UCustomizableObject>(nullptr, DefaultSidekicksCustomizableObjectPath);
	}
}

namespace SDMutableEditorAssetCategory
{
	void Set(const uint32 InCategory)
	{
		GSidekicksMutableAssetCategory = InCategory;
	}

	uint32 Get()
	{
		return GSidekicksMutableAssetCategory;
	}
}

USDMutableSidekickRecipeAssetFactory::USDMutableSidekickRecipeAssetFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = USDMutableSidekickRecipeAsset::StaticClass();
}

UObject* USDMutableSidekickRecipeAssetFactory::FactoryCreateNew(
	UClass* InClass,
	UObject* InParent,
	const FName InName,
	const EObjectFlags Flags,
	UObject* Context,
	FFeedbackContext* Warn)
{
	USDMutableSidekickRecipeAsset* RecipeAsset = NewObject<USDMutableSidekickRecipeAsset>(InParent, InClass, InName, Flags | RF_Transactional);
	if (RecipeAsset)
	{
		RecipeAsset->ColorPalette.EnsureColorSlotCount();
	}
	return RecipeAsset;
}

FText USDMutableSidekickRecipeAssetFactory::GetDisplayName() const
{
	return LOCTEXT("SidekickRecipeAssetFactoryDisplayName", "Sidekick Recipe DataAsset");
}

uint32 USDMutableSidekickRecipeAssetFactory::GetMenuCategories() const
{
	return SDMutableEditorAssetCategory::Get();
}

FString USDMutableSidekickRecipeAssetFactory::GetDefaultNewAssetName() const
{
	return TEXT("DA_SidekickRecipe");
}

USDMutableCatalogPackFactory::USDMutableCatalogPackFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = USDMutableCatalogPack::StaticClass();
}

UObject* USDMutableCatalogPackFactory::FactoryCreateNew(
	UClass* InClass,
	UObject* InParent,
	const FName InName,
	const EObjectFlags Flags,
	UObject* Context,
	FFeedbackContext* Warn)
{
	USDMutableCatalogPack* PackCatalog = NewObject<USDMutableCatalogPack>(InParent, InClass, InName, Flags | RF_Transactional);
	if (PackCatalog)
	{
		PackCatalog->PackId = InName;
		PackCatalog->DisplayName = FText::FromName(InName);
		PackCatalog->PackType = ESDMutableCatalogPackType::Shared;
	}
	return PackCatalog;
}

FText USDMutableCatalogPackFactory::GetDisplayName() const
{
	return LOCTEXT("CatalogPackFactoryDisplayName", "Sidekick Pack Catalog");
}

uint32 USDMutableCatalogPackFactory::GetMenuCategories() const
{
	return SDMutableEditorAssetCategory::Get();
}

FString USDMutableCatalogPackFactory::GetDefaultNewAssetName() const
{
	return TEXT("DA_SidekickPackCatalog");
}

USDMutableSidekickCustomizableObjectInstanceFactory::USDMutableSidekickCustomizableObjectInstanceFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UCustomizableObjectInstance::StaticClass();
}

UObject* USDMutableSidekickCustomizableObjectInstanceFactory::FactoryCreateNew(
	UClass* InClass,
	UObject* InParent,
	const FName InName,
	const EObjectFlags Flags,
	UObject* Context,
	FFeedbackContext* Warn)
{
	UCustomizableObject* CustomizableObject = LoadSidekicksCustomizableObject();
	if (!CustomizableObject)
	{
		if (Warn)
		{
			Warn->Logf(ELogVerbosity::Error, TEXT("Cannot create Sidekick COI: Sidekicks Customizable Object is not configured and default %s could not be loaded."), DefaultSidekicksCustomizableObjectPath);
		}
		return nullptr;
	}

	UCustomizableObjectInstance* Instance = NewObject<UCustomizableObjectInstance>(InParent, InClass, InName, Flags | RF_Transactional);
	if (!Instance)
	{
		return nullptr;
	}

	Instance->SetObject(CustomizableObject);
	Instance->SetBuildParameterRelevancy(true);
	Instance->SetDefaultValues();
	return Instance;
}

FText USDMutableSidekickCustomizableObjectInstanceFactory::GetDisplayName() const
{
	return LOCTEXT("SidekickCoiFactoryDisplayName", "Sidekick Customizable Object Instance");
}

uint32 USDMutableSidekickCustomizableObjectInstanceFactory::GetMenuCategories() const
{
	return SDMutableEditorAssetCategory::Get();
}

FString USDMutableSidekickCustomizableObjectInstanceFactory::GetDefaultNewAssetName() const
{
	return TEXT("COI_Sidekick");
}

#undef LOCTEXT_NAMESPACE
