#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SDMutableColorPreset.h"
#include "SDMutableTypes.h"
#include "SDMutableComponent.generated.h"

class UCustomizableObjectInstance;
class USDMutableCatalog;
class USDMutableSidekickRecipeAsset;
class USkeletalMesh;
class UTexture;
class UTexture2D;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSDMutableRecipeChanged, const FSDMutableSidekickRecipe&, Recipe);

/** Actor component that owns actor-local Sidekicks recipe state and applies it to a Mutable skeletal component. */
UCLASS(ClassGroup=(SDMutable), meta=(BlueprintSpawnableComponent))
class MUTABLESIDEKICKSPLUGIN_API USDMutableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USDMutableComponent();

	/** Root catalog used to resolve recipe slot IDs into skeletal mesh assets and Mutable parameter names. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable")
	TObjectPtr<USDMutableCatalog> Catalog;

	/** Active instance currently assigned through this component; may be a template, editor clone, or runtime clone. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SDMutable")
	TObjectPtr<UCustomizableObjectInstance> CustomizableObjectInstance;

	/** Optional preset copied into this actor; the component stores a local copy rather than staying live-linked. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable|Preset")
	TSoftObjectPtr<USDMutableSidekickRecipeAsset> SourceRecipeAsset;

	/** Durable COI/template used as the source for transient editor/runtime clones. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable|Preset")
	TSoftObjectPtr<UCustomizableObjectInstance> TemplateCustomizableObjectInstance;

	/** Per-actor game-world COI clone, preventing one placed actor from mutating another actor's shared template COI. */
	UPROPERTY(VisibleAnywhere, Transient, BlueprintReadOnly, Category="SDMutable|Runtime")
	TObjectPtr<UCustomizableObjectInstance> RuntimeCustomizableObjectInstance;

	/** Per-component editor-world COI clone used by Slate actor mode so previews do not edit shared COI assets. */
	UPROPERTY(VisibleAnywhere, Transient, DuplicateTransient, BlueprintReadOnly, Category="SDMutable|Runtime")
	TObjectPtr<UCustomizableObjectInstance> EditorCustomizableObjectInstance;

	/** Transient BaseColor texture generated from ColorPalette during apply; never save this as recipe source data. */
	UPROPERTY(VisibleAnywhere, Transient, BlueprintReadOnly, Category="SDMutable|Runtime")
	TObjectPtr<UTexture2D> RuntimeColorTexture;

	/** Actor-local reconstructable appearance state. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable|Local State")
	FSDMutableSidekickRecipe Recipe;

	/** Actor-local palette state used to rebuild RuntimeColorTexture. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable|Local State")
	FSDMutableColorPalette ColorPalette;

	UPROPERTY(BlueprintAssignable, Category="SDMutable")
	FSDMutableRecipeChanged OnRecipeChanged;

	UFUNCTION(BlueprintCallable, Category="SDMutable")
	void SetCustomizableObjectInstance(UCustomizableObjectInstance* InInstance);

	UFUNCTION(BlueprintCallable, Category="SDMutable")
	bool SynchronizeCustomizableObjectInstanceFromOwner();

	/** Copy source recipe data into local actor state; future source asset edits are not automatically reflected. */
	UFUNCTION(BlueprintCallable, Category="SDMutable")
	bool CopyFromRecipeAsset(bool bApplyToMutable = true);

	/** Create or restore the per-actor runtime COI in game worlds, or synchronize the editor/template instance otherwise. */
	UFUNCTION(BlueprintCallable, Category="SDMutable")
	bool EnsureRuntimeCustomizableObjectInstance();

	UFUNCTION(BlueprintCallable, Category="SDMutable")
	void SetRecipe(const FSDMutableSidekickRecipe& InRecipe, bool bApplyToMutable = true);

	UFUNCTION(BlueprintCallable, Category="SDMutable")
	void SetColorPalette(const FSDMutableColorPalette& InColorPalette, bool bApplyToMutable = true);

	UFUNCTION(BlueprintCallable, Category="SDMutable")
	void ApplyRecipeToMutable();

	UFUNCTION(BlueprintCallable, Category="SDMutable")
	void ApplyRecipeToMutableAndUpdate(bool bIgnoreCloseDist = false, bool bForceHighPriority = false);

	/** Reset Mutable defaults first, then apply local recipe state and request a mesh rebuild. */
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
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
#if WITH_EDITOR
	virtual void PostLoad() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	void RefreshEditorPreviewFromRecipe();
#endif

	bool ApplyPartToMutable(const FSDMutablePartSelection& Selection) const;
	bool ApplyFloatParameter(FName ParameterName, float Value) const;
	bool ApplyColorParameter(FName ParameterName, const FLinearColor& Value) const;
	bool ApplyTextureParameter(FName ParameterName, UTexture* Value) const;
	bool ApplySkeletalMeshParameter(FName ParameterName, USkeletalMesh* Value) const;
	bool ApplyIntOptionParameter(FName ParameterName, FName OptionId) const;
	bool SynchronizeCustomizableObjectInstanceFromOwnerInternal(bool bLogErrors);
	bool EnsureEditorCustomizableObjectInstance(bool bLogErrors = true);
	bool IsRuntimeWorld() const;
	/** Returns the clone/template that should receive parameter writes for the current world type. */
	UCustomizableObjectInstance* GetActiveCustomizableObjectInstance() const;

#if WITH_EDITORONLY_DATA
	/** Automatic editor preview hydration should not dirty level packages. */
	bool bSuppressEditorPreviewDirty = false;
#endif
};
