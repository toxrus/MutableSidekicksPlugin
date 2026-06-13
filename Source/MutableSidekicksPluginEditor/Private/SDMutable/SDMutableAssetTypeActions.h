#pragma once

#include "AssetTypeActions_Base.h"

class FSDMutableSidekickRecipeAssetTypeActions final : public FAssetTypeActions_Base
{
public:
	explicit FSDMutableSidekickRecipeAssetTypeActions(uint32 InAssetCategory);

	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override;
	virtual const TArray<FText>& GetSubMenus() const override;

private:
	uint32 AssetCategory;
};

class FSDMutableCatalogPackAssetTypeActions final : public FAssetTypeActions_Base
{
public:
	explicit FSDMutableCatalogPackAssetTypeActions(uint32 InAssetCategory);

	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override;
	virtual const TArray<FText>& GetSubMenus() const override;

private:
	uint32 AssetCategory;
};
