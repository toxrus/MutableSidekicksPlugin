#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SDMutableTypes.h"
#include "SDMutableComponent.generated.h"

class UCustomizableObjectInstance;
class USDMutableCatalog;
class USkeletalMesh;
class UTexture;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSDMutableRecipeChanged, const FSDMutableSidekickRecipe&, Recipe);

UCLASS(ClassGroup=(SDMutable), meta=(BlueprintSpawnableComponent))
class MUTABLESIDEKICKSPLUGIN_API USDMutableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USDMutableComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable")
	TObjectPtr<USDMutableCatalog> Catalog;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SDMutable")
	TObjectPtr<UCustomizableObjectInstance> CustomizableObjectInstance;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable")
	FSDMutableSidekickRecipe Recipe;

	UPROPERTY(BlueprintAssignable, Category="SDMutable")
	FSDMutableRecipeChanged OnRecipeChanged;

	UFUNCTION(BlueprintCallable, Category="SDMutable")
	void SetCustomizableObjectInstance(UCustomizableObjectInstance* InInstance);

	UFUNCTION(BlueprintCallable, Category="SDMutable")
	bool SynchronizeCustomizableObjectInstanceFromOwner();

	UFUNCTION(BlueprintCallable, Category="SDMutable")
	void SetRecipe(const FSDMutableSidekickRecipe& InRecipe, bool bApplyToMutable = true);

	UFUNCTION(BlueprintCallable, Category="SDMutable")
	void ApplyRecipeToMutable();

	UFUNCTION(BlueprintCallable, Category="SDMutable")
	void ApplyRecipeToMutableAndUpdate(bool bIgnoreCloseDist = false, bool bForceHighPriority = false);

	UFUNCTION(BlueprintCallable, Category="SDMutable")
	bool ApplyRecipeFromMutableDefaultsAndUpdate(bool bIgnoreCloseDist = false, bool bForceHighPriority = false);

	UFUNCTION(BlueprintCallable, Category="SDMutable")
	bool SetRecipeAndApplyFromMutableDefaultsAndUpdate(const FSDMutableSidekickRecipe& InRecipe, bool bIgnoreCloseDist = false, bool bForceHighPriority = false);

	UFUNCTION(BlueprintCallable, Category="SDMutable")
	bool ResetMutableParametersToDefaults(bool bUpdateAfterReset = false, bool bIgnoreCloseDist = false, bool bForceHighPriority = false);

	UFUNCTION(BlueprintCallable, Category="SDMutable")
	void UpdateMutableInstance(bool bIgnoreCloseDist = false, bool bForceHighPriority = false);

	UFUNCTION(BlueprintCallable, Category="SDMutable")
	void SetPart(ESDMutablePartSlot Slot, FName OptionId, bool bApplyToMutable = true);

	UFUNCTION(BlueprintCallable, Category="SDMutable")
	void SetBodyShape(const FSDMutableBodyShape& InBodyShape, bool bApplyToMutable = true);

	UFUNCTION(BlueprintCallable, Category="SDMutable")
	void SetMaterialSettings(const FSDMutableMaterialSettings& InMaterial, bool bApplyToMutable = true);

	UFUNCTION(BlueprintPure, Category="SDMutable")
	bool GetPart(ESDMutablePartSlot Slot, FName& OutOptionId) const;

private:
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
#if WITH_EDITOR
	virtual void PostLoad() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	bool ApplyPartToMutable(const FSDMutablePartSelection& Selection) const;
	bool ApplyFloatParameter(FName ParameterName, float Value) const;
	bool ApplyColorParameter(FName ParameterName, const FLinearColor& Value) const;
	bool ApplyTextureParameter(FName ParameterName, UTexture* Value) const;
	bool ApplySkeletalMeshParameter(FName ParameterName, USkeletalMesh* Value) const;
	bool ApplyIntOptionParameter(FName ParameterName, FName OptionId) const;
	bool SynchronizeCustomizableObjectInstanceFromOwnerInternal(bool bLogErrors);
};
