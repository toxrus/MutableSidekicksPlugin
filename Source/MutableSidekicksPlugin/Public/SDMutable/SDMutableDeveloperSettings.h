#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SDMutableDeveloperSettings.generated.h"

class USDMutableCatalog;
class UCustomizableObject;

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Sidekicks Mutable"))
class MUTABLESIDEKICKSPLUGIN_API USDMutableDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Catalog", meta=(AllowedClasses="/Script/MutableSidekicksPlugin.SDMutableCatalog"))
	TSoftObjectPtr<USDMutableCatalog> RootCatalog;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Asset Creation", meta=(AllowedClasses="/Script/CustomizableObject.CustomizableObject"))
	TSoftObjectPtr<UCustomizableObject> SidekicksCustomizableObject;

	virtual FName GetCategoryName() const override;
};
