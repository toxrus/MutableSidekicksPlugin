#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "SDMutableAssetFactories.generated.h"

UCLASS()
class USDMutableSidekickRecipeAssetFactory final : public UFactory
{
	GENERATED_BODY()

public:
	USDMutableSidekickRecipeAssetFactory();

	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
	virtual FString GetDefaultNewAssetName() const override;
};

UCLASS()
class USDMutableCatalogPackFactory final : public UFactory
{
	GENERATED_BODY()

public:
	USDMutableCatalogPackFactory();

	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
	virtual FString GetDefaultNewAssetName() const override;
};

UCLASS()
class USDMutableSidekickCustomizableObjectInstanceFactory final : public UFactory
{
	GENERATED_BODY()

public:
	USDMutableSidekickCustomizableObjectInstanceFactory();

	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
	virtual FString GetDefaultNewAssetName() const override;
};

namespace SDMutableEditorAssetCategory
{
	void Set(uint32 InCategory);
	uint32 Get();
}
