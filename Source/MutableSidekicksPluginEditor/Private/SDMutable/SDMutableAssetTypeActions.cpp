#include "SDMutable/SDMutableAssetTypeActions.h"

#include "SDMutable/SDMutableCatalog.h"
#include "SDMutable/SDMutableSidekickRecipeAsset.h"

#define LOCTEXT_NAMESPACE "SDMutableAssetTypeActions"

FSDMutableSidekickRecipeAssetTypeActions::FSDMutableSidekickRecipeAssetTypeActions(const uint32 InAssetCategory)
	: AssetCategory(InAssetCategory)
{
}

FText FSDMutableSidekickRecipeAssetTypeActions::GetName() const
{
	return LOCTEXT("SidekickRecipeAssetTypeName", "Sidekick Recipe");
}

FColor FSDMutableSidekickRecipeAssetTypeActions::GetTypeColor() const
{
	return FColor(82, 156, 255);
}

UClass* FSDMutableSidekickRecipeAssetTypeActions::GetSupportedClass() const
{
	return USDMutableSidekickRecipeAsset::StaticClass();
}

uint32 FSDMutableSidekickRecipeAssetTypeActions::GetCategories()
{
	return AssetCategory;
}

const TArray<FText>& FSDMutableSidekickRecipeAssetTypeActions::GetSubMenus() const
{
	static const TArray<FText> SubMenus;
	return SubMenus;
}

FSDMutableCatalogPackAssetTypeActions::FSDMutableCatalogPackAssetTypeActions(const uint32 InAssetCategory)
	: AssetCategory(InAssetCategory)
{
}

FText FSDMutableCatalogPackAssetTypeActions::GetName() const
{
	return LOCTEXT("CatalogPackAssetTypeName", "Sidekick Pack Catalog");
}

FColor FSDMutableCatalogPackAssetTypeActions::GetTypeColor() const
{
	return FColor(125, 200, 120);
}

UClass* FSDMutableCatalogPackAssetTypeActions::GetSupportedClass() const
{
	return USDMutableCatalogPack::StaticClass();
}

uint32 FSDMutableCatalogPackAssetTypeActions::GetCategories()
{
	return AssetCategory;
}

const TArray<FText>& FSDMutableCatalogPackAssetTypeActions::GetSubMenus() const
{
	static const TArray<FText> SubMenus;
	return SubMenus;
}

#undef LOCTEXT_NAMESPACE
