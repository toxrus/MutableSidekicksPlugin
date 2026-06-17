#include "SDMutable/SDMutableComponent.h"

#include "SDMutable/SDMutableCatalog.h"
#include "SDMutable/SDMutableDeveloperSettings.h"
#include "SDMutable/SDMutableParameters.h"
#include "SDMutable/SDMutableSidekickRecipeAsset.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "MuCO/CustomizableObject.h"
#include "MuCO/CustomizableObjectInstance.h"
#include "MuCO/CustomizableSkeletalComponent.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogSDMutable, Log, All);

namespace
{
	void SetNameOrStringProperty(UFunction* Function, void* Params, const FName PropertyName, const FName Value)
	{
		if (FNameProperty* NameProperty = FindFProperty<FNameProperty>(Function, PropertyName))
		{
			NameProperty->SetPropertyValue_InContainer(Params, Value);
			return;
		}

		if (FStrProperty* StringProperty = FindFProperty<FStrProperty>(Function, PropertyName))
		{
			StringProperty->SetPropertyValue_InContainer(Params, Value.ToString());
		}
	}

	void SetIntProperty(UFunction* Function, void* Params, const FName PropertyName, const int32 Value)
	{
		if (FIntProperty* IntProperty = FindFProperty<FIntProperty>(Function, PropertyName))
		{
			IntProperty->SetPropertyValue_InContainer(Params, Value);
		}
	}

	bool InvokeMutableFunction(UCustomizableObjectInstance* Instance, const FName FunctionName, TFunctionRef<void(UFunction*, void*)> FillParams)
	{
		if (!Instance)
		{
			return false;
		}

		UFunction* Function = Instance->FindFunction(FunctionName);
		if (!Function)
		{
			return false;
		}

		void* Params = FMemory_Alloca(Function->ParmsSize);
		FMemory::Memzero(Params, Function->ParmsSize);
		Function->InitializeStruct(Params);
		FillParams(Function, Params);
		Instance->ProcessEvent(Function, Params);
		Function->DestroyStruct(Params);
		return true;
	}

#if WITH_EDITOR
	void ModifyObjectForEditor(UObject* Object)
	{
		if (Object && !Object->HasAnyFlags(RF_ClassDefaultObject))
		{
			Object->Modify();
		}
	}

	void MarkObjectDirtyForEditor(UObject* Object)
	{
		if (Object && !Object->HasAnyFlags(RF_ClassDefaultObject))
		{
			Object->MarkPackageDirty();
		}
	}
#endif

	UCustomizableSkeletalComponent* FindMutableSkeletalComponent(AActor* Owner)
	{
		if (!Owner)
		{
			return nullptr;
		}

		TInlineComponentArray<UCustomizableSkeletalComponent*> MutableSkeletalComponents;
		Owner->GetComponents(MutableSkeletalComponents);

		UCustomizableSkeletalComponent* FirstValidComponent = nullptr;
		for (UCustomizableSkeletalComponent* Component : MutableSkeletalComponents)
		{
			if (!IsValid(Component))
			{
				continue;
			}

			if (!FirstValidComponent)
			{
				FirstValidComponent = Component;
			}

			if (Component->GetCustomizableObjectInstance())
			{
				return Component;
			}
		}

		return FirstValidComponent;
	}
}

USDMutableComponent::USDMutableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USDMutableComponent::OnRegister()
{
	Super::OnRegister();
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		SynchronizeCustomizableObjectInstanceFromOwnerInternal(false);
#if WITH_EDITOR
		RefreshEditorPreviewFromRecipe();
#endif
	}
}

void USDMutableComponent::OnUnregister()
{
	RuntimeColorTexture = nullptr;
	EditorCustomizableObjectInstance = nullptr;
	Super::OnUnregister();
}

void USDMutableComponent::BeginPlay()
{
	Super::BeginPlay();
	if (EnsureRuntimeCustomizableObjectInstance())
	{
		ApplyRecipeFromMutableDefaultsAndUpdate(false, false);
	}
}

void USDMutableComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RuntimeColorTexture = nullptr;
	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void USDMutableComponent::PostLoad()
{
	Super::PostLoad();
	EditorCustomizableObjectInstance = nullptr;
	RuntimeColorTexture = nullptr;
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		SynchronizeCustomizableObjectInstanceFromOwnerInternal(false);
	}
}

void USDMutableComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		SynchronizeCustomizableObjectInstanceFromOwnerInternal(false);
		RefreshEditorPreviewFromRecipe();
	}
}

void USDMutableComponent::RefreshEditorPreviewFromRecipe()
{
	if (HasAnyFlags(RF_ClassDefaultObject) || IsRuntimeWorld())
	{
		return;
	}

	TGuardValue<bool> SuppressEditorPreviewDirty(bSuppressEditorPreviewDirty, true);
	ApplyRecipeFromMutableDefaultsAndUpdate(false, false);
}
#endif

void USDMutableComponent::SetCustomizableObjectInstance(UCustomizableObjectInstance* InInstance)
{
#if WITH_EDITOR
	ModifyObjectForEditor(this);
	ModifyObjectForEditor(GetOwner());
#endif
	CustomizableObjectInstance = InInstance;
	EditorCustomizableObjectInstance = nullptr;
	RuntimeCustomizableObjectInstance = nullptr;
	if (!IsRuntimeWorld())
	{
		TemplateCustomizableObjectInstance = InInstance;
	}
#if WITH_EDITOR
	MarkObjectDirtyForEditor(this);
	MarkObjectDirtyForEditor(GetOwner());
#endif
}

bool USDMutableComponent::SynchronizeCustomizableObjectInstanceFromOwner()
{
	return SynchronizeCustomizableObjectInstanceFromOwnerInternal(true);
}

bool USDMutableComponent::SynchronizeCustomizableObjectInstanceFromOwnerInternal(const bool bLogErrors)
{
	if (IsRuntimeWorld() && RuntimeCustomizableObjectInstance)
	{
		return true;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		if (bLogErrors)
		{
			UE_LOG(LogSDMutable, Error, TEXT("Cannot synchronize COI for %s: component has no owner actor."), *GetNameSafe(this));
		}
		return false;
	}

	UCustomizableSkeletalComponent* MutableSkeletalComponent = FindMutableSkeletalComponent(Owner);
	if (!MutableSkeletalComponent)
	{
		if (bLogErrors)
		{
			UE_LOG(LogSDMutable, Error, TEXT("Cannot synchronize COI for %s on actor %s: no UCustomizableSkeletalComponent exists on the actor."),
				*GetNameSafe(this),
				*GetNameSafe(Owner));
		}
		return false;
	}

	UCustomizableObjectInstance* ActorInstance = MutableSkeletalComponent->GetCustomizableObjectInstance();
	if (!IsRuntimeWorld() && EditorCustomizableObjectInstance)
	{
		if (ActorInstance != EditorCustomizableObjectInstance)
		{
#if WITH_EDITOR
			if (!bSuppressEditorPreviewDirty)
			{
				ModifyObjectForEditor(MutableSkeletalComponent);
			}
#endif
			MutableSkeletalComponent->SetCustomizableObjectInstance(EditorCustomizableObjectInstance);
			ActorInstance = EditorCustomizableObjectInstance;
#if WITH_EDITOR
			if (!bSuppressEditorPreviewDirty)
			{
				MarkObjectDirtyForEditor(MutableSkeletalComponent);
			}
#endif
		}
		CustomizableObjectInstance = EditorCustomizableObjectInstance;
		return true;
	}

	if (!ActorInstance)
	{
		if (bLogErrors)
		{
			UE_LOG(LogSDMutable, Error, TEXT("Cannot synchronize COI for %s on actor %s: UCustomizableSkeletalComponent %s has no COI."),
				*GetNameSafe(this),
				*GetNameSafe(Owner),
				*GetNameSafe(MutableSkeletalComponent));
		}
		return false;
	}

	if (CustomizableObjectInstance == ActorInstance)
	{
		return true;
	}

#if WITH_EDITOR
	ModifyObjectForEditor(this);
	ModifyObjectForEditor(Owner);
#endif
	CustomizableObjectInstance = ActorInstance;
	if (!IsRuntimeWorld())
	{
		TemplateCustomizableObjectInstance = ActorInstance;
	}
#if WITH_EDITOR
	MarkObjectDirtyForEditor(this);
	MarkObjectDirtyForEditor(Owner);
#endif
	UE_LOG(LogSDMutable, Log, TEXT("Synchronized %s COI from actor %s CustomizableSkeletalComponent %s: %s"),
		*GetNameSafe(this),
		*GetNameSafe(Owner),
		*GetNameSafe(MutableSkeletalComponent),
		*GetNameSafe(CustomizableObjectInstance));
	return true;
}

bool USDMutableComponent::EnsureEditorCustomizableObjectInstance(const bool bLogErrors)
{
	if (IsRuntimeWorld())
	{
		return EnsureRuntimeCustomizableObjectInstance();
	}

	if (EditorCustomizableObjectInstance)
	{
		return SynchronizeCustomizableObjectInstanceFromOwnerInternal(bLogErrors);
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		if (bLogErrors)
		{
			UE_LOG(LogSDMutable, Error, TEXT("Cannot create editor COI for %s: component has no owner actor."), *GetNameSafe(this));
		}
		return false;
	}

	UCustomizableSkeletalComponent* MutableSkeletalComponent = FindMutableSkeletalComponent(Owner);
	if (!MutableSkeletalComponent)
	{
		if (bLogErrors)
		{
			UE_LOG(LogSDMutable, Error, TEXT("Cannot create editor COI for %s on actor %s: no UCustomizableSkeletalComponent exists on the actor."),
				*GetNameSafe(this),
				*GetNameSafe(Owner));
		}
		return false;
	}

	UCustomizableObjectInstance* TemplateInstance = CustomizableObjectInstance.Get();
	if (!TemplateInstance)
	{
		TemplateInstance = MutableSkeletalComponent->GetCustomizableObjectInstance();
	}
	if (!TemplateInstance)
	{
		TemplateInstance = TemplateCustomizableObjectInstance.LoadSynchronous();
	}
	if (!TemplateInstance)
	{
		if (USDMutableSidekickRecipeAsset* RecipeAsset = SourceRecipeAsset.LoadSynchronous())
		{
			TemplateInstance = RecipeAsset->CustomizableObjectInstance.LoadSynchronous();
		}
	}

	if (TemplateInstance)
	{
		EditorCustomizableObjectInstance = TemplateInstance->CloneStatic(this);
		if (!EditorCustomizableObjectInstance)
		{
			EditorCustomizableObjectInstance = DuplicateObject<UCustomizableObjectInstance>(TemplateInstance, this);
		}
	}
	else
	{
		const USDMutableDeveloperSettings* Settings = GetDefault<USDMutableDeveloperSettings>();
		UCustomizableObject* SidekicksCustomizableObject = Settings ? Settings->SidekicksCustomizableObject.LoadSynchronous() : nullptr;
		if (SidekicksCustomizableObject)
		{
			EditorCustomizableObjectInstance = SidekicksCustomizableObject->CreateInstance();
		}
	}

	if (!EditorCustomizableObjectInstance)
	{
		if (bLogErrors)
		{
			UE_LOG(LogSDMutable, Error, TEXT("Cannot create editor COI for %s on actor %s: no template COI or configured Sidekicks Customizable Object was available."),
				*GetNameSafe(this),
				*GetNameSafe(Owner));
		}
		return false;
	}

	EditorCustomizableObjectInstance->SetFlags(RF_Transient);
#if WITH_EDITOR
	if (!bSuppressEditorPreviewDirty)
	{
		ModifyObjectForEditor(this);
		ModifyObjectForEditor(Owner);
		ModifyObjectForEditor(MutableSkeletalComponent);
	}
#endif
	MutableSkeletalComponent->SetCustomizableObjectInstance(EditorCustomizableObjectInstance);
	CustomizableObjectInstance = EditorCustomizableObjectInstance;
#if WITH_EDITOR
	if (!bSuppressEditorPreviewDirty)
	{
		MarkObjectDirtyForEditor(this);
		MarkObjectDirtyForEditor(Owner);
		MarkObjectDirtyForEditor(MutableSkeletalComponent);
	}
#endif
	UE_LOG(LogSDMutable, Log, TEXT("Created private editor COI %s for actor %s from template %s."),
		*GetNameSafe(EditorCustomizableObjectInstance),
		*GetNameSafe(Owner),
		*GetNameSafe(TemplateInstance));
	return true;
}

bool USDMutableComponent::CopyFromRecipeAsset(const bool bApplyToMutable)
{
	USDMutableSidekickRecipeAsset* RecipeAsset = SourceRecipeAsset.LoadSynchronous();
	if (!RecipeAsset)
	{
		UE_LOG(LogSDMutable, Warning, TEXT("Cannot copy Sidekicks recipe asset for %s: SourceRecipeAsset is not set or failed to load."),
			*GetNameSafe(this));
		return false;
	}

#if WITH_EDITOR
	ModifyObjectForEditor(this);
	ModifyObjectForEditor(GetOwner());
#endif
	Recipe = RecipeAsset->Recipe;
	ColorPalette = RecipeAsset->ColorPalette;
	if (!RecipeAsset->CustomizableObjectInstance.IsNull())
	{
		TemplateCustomizableObjectInstance = RecipeAsset->CustomizableObjectInstance;
	}
	OnRecipeChanged.Broadcast(Recipe);

	if (bApplyToMutable)
	{
		ApplyRecipeToMutable();
	}
#if WITH_EDITOR
	MarkObjectDirtyForEditor(this);
	MarkObjectDirtyForEditor(GetOwner());
#endif
	return true;
}

bool USDMutableComponent::EnsureRuntimeCustomizableObjectInstance()
{
	if (!IsRuntimeWorld())
	{
		return SynchronizeCustomizableObjectInstanceFromOwner();
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		UE_LOG(LogSDMutable, Error, TEXT("Cannot create runtime COI for %s: component has no owner actor."), *GetNameSafe(this));
		return false;
	}

	UCustomizableSkeletalComponent* MutableSkeletalComponent = FindMutableSkeletalComponent(Owner);
	if (!MutableSkeletalComponent)
	{
		UE_LOG(LogSDMutable, Error, TEXT("Cannot create runtime COI for %s on actor %s: no UCustomizableSkeletalComponent exists on the actor."),
			*GetNameSafe(this),
			*GetNameSafe(Owner));
		return false;
	}

	if (RuntimeCustomizableObjectInstance)
	{
		if (MutableSkeletalComponent->GetCustomizableObjectInstance() != RuntimeCustomizableObjectInstance)
		{
			MutableSkeletalComponent->SetCustomizableObjectInstance(RuntimeCustomizableObjectInstance);
		}
		CustomizableObjectInstance = RuntimeCustomizableObjectInstance;
		return true;
	}

	UCustomizableObjectInstance* TemplateInstance = TemplateCustomizableObjectInstance.LoadSynchronous();
	if (!TemplateInstance)
	{
		TemplateInstance = MutableSkeletalComponent->GetCustomizableObjectInstance();
	}
	if (!TemplateInstance)
	{
		if (USDMutableSidekickRecipeAsset* RecipeAsset = SourceRecipeAsset.LoadSynchronous())
		{
			TemplateInstance = RecipeAsset->CustomizableObjectInstance.LoadSynchronous();
		}
	}

	if (TemplateInstance)
	{
		RuntimeCustomizableObjectInstance = TemplateInstance->CloneStatic(this);
		if (!RuntimeCustomizableObjectInstance)
		{
			RuntimeCustomizableObjectInstance = DuplicateObject<UCustomizableObjectInstance>(TemplateInstance, this);
		}
	}
	else
	{
		const USDMutableDeveloperSettings* Settings = GetDefault<USDMutableDeveloperSettings>();
		UCustomizableObject* SidekicksCustomizableObject = Settings ? Settings->SidekicksCustomizableObject.LoadSynchronous() : nullptr;
		if (SidekicksCustomizableObject)
		{
			RuntimeCustomizableObjectInstance = SidekicksCustomizableObject->CreateInstance();
		}
	}

	if (!RuntimeCustomizableObjectInstance)
	{
		UE_LOG(LogSDMutable, Error, TEXT("Cannot create runtime COI for %s on actor %s: no template COI or configured Sidekicks Customizable Object was available."),
			*GetNameSafe(this),
			*GetNameSafe(Owner));
		return false;
	}

	RuntimeCustomizableObjectInstance->SetFlags(RF_Transient);
	MutableSkeletalComponent->SetCustomizableObjectInstance(RuntimeCustomizableObjectInstance);
	CustomizableObjectInstance = RuntimeCustomizableObjectInstance;
	UE_LOG(LogSDMutable, Log, TEXT("Created private runtime COI %s for actor %s from template %s."),
		*GetNameSafe(RuntimeCustomizableObjectInstance),
		*GetNameSafe(Owner),
		*GetNameSafe(TemplateInstance));
	return true;
}

void USDMutableComponent::SetRecipe(const FSDMutableSidekickRecipe& InRecipe, const bool bApplyToMutable)
{
#if WITH_EDITOR
	ModifyObjectForEditor(this);
	ModifyObjectForEditor(GetOwner());
#endif
	Recipe = InRecipe;
	OnRecipeChanged.Broadcast(Recipe);

	if (bApplyToMutable)
	{
		ApplyRecipeToMutable();
	}
#if WITH_EDITOR
	MarkObjectDirtyForEditor(this);
	MarkObjectDirtyForEditor(GetOwner());
#endif
}

void USDMutableComponent::SetColorPalette(const FSDMutableColorPalette& InColorPalette, const bool bApplyToMutable)
{
#if WITH_EDITOR
	ModifyObjectForEditor(this);
	ModifyObjectForEditor(GetOwner());
#endif
	ColorPalette = InColorPalette;
	if (bApplyToMutable)
	{
		ApplyRecipeToMutable();
	}
#if WITH_EDITOR
	MarkObjectDirtyForEditor(this);
	MarkObjectDirtyForEditor(GetOwner());
#endif
}

void USDMutableComponent::ApplyRecipeToMutable()
{
	if (IsRuntimeWorld())
	{
		EnsureRuntimeCustomizableObjectInstance();
	}
	else
	{
		EnsureEditorCustomizableObjectInstance();
	}
	UCustomizableObjectInstance* ActiveInstance = GetActiveCustomizableObjectInstance();
	UE_LOG(LogSDMutable, Verbose, TEXT("Applying Sidekicks recipe to Mutable instance %s. Parts=%d"),
		*GetNameSafe(ActiveInstance),
		Recipe.Parts.Num());

	ApplyFloatParameter(SDMutableParameters::MaleFemale, Recipe.BodyShape.MaleFemale);
	ApplyFloatParameter(SDMutableParameters::Heavy, Recipe.BodyShape.Heavy);
	ApplyFloatParameter(SDMutableParameters::Buff, Recipe.BodyShape.Buff);
	ApplyFloatParameter(SDMutableParameters::Skinny, Recipe.BodyShape.Skinny);

	ApplyColorParameter(SDMutableParameters::SkinColor, Recipe.Material.SkinColor);
	ApplyColorParameter(SDMutableParameters::EyeColor, Recipe.Material.EyeColor);
	ApplyColorParameter(SDMutableParameters::DirtColor, Recipe.Material.DirtColor);
	ApplyFloatParameter(SDMutableParameters::CutWeight, Recipe.Material.CutWeight);
	ApplyFloatParameter(SDMutableParameters::DirtWeight, Recipe.Material.DirtWeight);
	ApplyFloatParameter(SDMutableParameters::DarkWeight, Recipe.Material.DarkWeight);

	if (!ColorPalette.ColorSlots.IsEmpty())
	{
		RuntimeColorTexture = USDMutableColorPreset::BuildTransientColorTextureFromPalette(ColorPalette, GetTransientPackage());
		if (RuntimeColorTexture)
		{
			ApplyTextureParameter(SDMutableParameters::BaseColor, RuntimeColorTexture);
		}
		else
		{
			UE_LOG(LogSDMutable, Warning, TEXT("Failed to build runtime Sidekicks color texture from local palette on component %s"),
				*GetNameSafe(this));
		}
	}
	else if (UTexture* BaseColor = Recipe.Material.BaseColor.LoadSynchronous())
	{
		ApplyTextureParameter(SDMutableParameters::BaseColor, BaseColor);
	}
	else if (!Recipe.Material.BaseColor.IsNull())
	{
		UE_LOG(LogSDMutable, Warning, TEXT("Failed to synchronously load BaseColor texture %s"),
			*Recipe.Material.BaseColor.ToSoftObjectPath().ToString());
	}

	for (const FSDMutablePartSelection& Selection : Recipe.Parts)
	{
		ApplyPartToMutable(Selection);
	}
}

void USDMutableComponent::ApplyRecipeToMutableAndUpdate(const bool bIgnoreCloseDist, const bool bForceHighPriority)
{
	ApplyRecipeToMutable();
	UpdateMutableInstance(bIgnoreCloseDist, bForceHighPriority);
}

bool USDMutableComponent::ApplyRecipeFromMutableDefaultsAndUpdate(const bool bIgnoreCloseDist, const bool bForceHighPriority)
{
	if (!ResetMutableParametersToDefaults(false))
	{
		return false;
	}

	ApplyRecipeToMutable();
	UpdateMutableInstance(bIgnoreCloseDist, bForceHighPriority);
	return true;
}

bool USDMutableComponent::SetRecipeAndApplyFromMutableDefaultsAndUpdate(const FSDMutableSidekickRecipe& InRecipe, const bool bIgnoreCloseDist, const bool bForceHighPriority)
{
	Recipe = InRecipe;
	OnRecipeChanged.Broadcast(Recipe);
	return ApplyRecipeFromMutableDefaultsAndUpdate(bIgnoreCloseDist, bForceHighPriority);
}

bool USDMutableComponent::ResetMutableParametersToDefaults(const bool bUpdateAfterReset, const bool bIgnoreCloseDist, const bool bForceHighPriority)
{
	if (IsRuntimeWorld())
	{
		EnsureRuntimeCustomizableObjectInstance();
	}
	else
	{
		EnsureEditorCustomizableObjectInstance();
	}
	UCustomizableObjectInstance* ActiveInstance = GetActiveCustomizableObjectInstance();
	if (!ActiveInstance)
	{
		UE_LOG(LogSDMutable, Warning, TEXT("Cannot reset Mutable parameters: CustomizableObjectInstance is null on component %s"),
			*GetNameSafe(this));
		return false;
	}

	UE_LOG(LogSDMutable, Log, TEXT("Resetting Mutable instance %s parameters to Customizable Object defaults. UpdateAfterReset=%s"),
		*GetNameSafe(ActiveInstance),
		bUpdateAfterReset ? TEXT("true") : TEXT("false"));

	ActiveInstance->SetDefaultValues();

	if (bUpdateAfterReset)
	{
		UpdateMutableInstance(bIgnoreCloseDist, bForceHighPriority);
	}

	return true;
}

void USDMutableComponent::UpdateMutableInstance(const bool bIgnoreCloseDist, const bool bForceHighPriority)
{
	if (IsRuntimeWorld())
	{
		EnsureRuntimeCustomizableObjectInstance();
	}
	else
	{
		EnsureEditorCustomizableObjectInstance();
	}
	UCustomizableObjectInstance* ActiveInstance = GetActiveCustomizableObjectInstance();
	if (ActiveInstance)
	{
		UE_LOG(LogSDMutable, Log, TEXT("Updating Mutable instance %s after applying Sidekicks parameters. IgnoreCloseDist=%s ForceHighPriority=%s"),
			*GetNameSafe(ActiveInstance),
			bIgnoreCloseDist ? TEXT("true") : TEXT("false"),
			bForceHighPriority ? TEXT("true") : TEXT("false"));

		ActiveInstance->UpdateSkeletalMeshAsync(bIgnoreCloseDist, bForceHighPriority);
#if WITH_EDITOR
		ActiveInstance->PostEditChange();
		if (!bSuppressEditorPreviewDirty)
		{
			MarkObjectDirtyForEditor(this);
			MarkObjectDirtyForEditor(GetOwner());
		}
#endif
		return;
	}

	UE_LOG(LogSDMutable, Warning, TEXT("Cannot update Mutable instance: CustomizableObjectInstance is null on component %s"),
		*GetNameSafe(this));
}

void USDMutableComponent::SetPart(const ESDMutablePartSlot Slot, const FName OptionId, const bool bApplyToMutable)
{
#if WITH_EDITOR
	ModifyObjectForEditor(this);
	ModifyObjectForEditor(GetOwner());
#endif
	for (FSDMutablePartSelection& Selection : Recipe.Parts)
	{
		if (Selection.Slot == Slot)
		{
			Selection.OptionId = OptionId;
			OnRecipeChanged.Broadcast(Recipe);

			if (bApplyToMutable)
			{
				if (!IsRuntimeWorld())
				{
					EnsureEditorCustomizableObjectInstance();
				}
				ApplyPartToMutable(Selection);
			}
#if WITH_EDITOR
			MarkObjectDirtyForEditor(this);
			MarkObjectDirtyForEditor(GetOwner());
#endif
			return;
		}
	}

	FSDMutablePartSelection& NewSelection = Recipe.Parts.AddDefaulted_GetRef();
	NewSelection.Slot = Slot;
	NewSelection.OptionId = OptionId;
	OnRecipeChanged.Broadcast(Recipe);

	if (bApplyToMutable)
	{
		if (!IsRuntimeWorld())
		{
			EnsureEditorCustomizableObjectInstance();
		}
		ApplyPartToMutable(NewSelection);
	}
#if WITH_EDITOR
	MarkObjectDirtyForEditor(this);
	MarkObjectDirtyForEditor(GetOwner());
#endif
}

void USDMutableComponent::SetBodyShape(const FSDMutableBodyShape& InBodyShape, const bool bApplyToMutable)
{
#if WITH_EDITOR
	ModifyObjectForEditor(this);
	ModifyObjectForEditor(GetOwner());
#endif
	Recipe.BodyShape = InBodyShape;
	OnRecipeChanged.Broadcast(Recipe);

	if (bApplyToMutable)
	{
		if (!IsRuntimeWorld())
		{
			EnsureEditorCustomizableObjectInstance();
		}
		ApplyFloatParameter(SDMutableParameters::MaleFemale, Recipe.BodyShape.MaleFemale);
		ApplyFloatParameter(SDMutableParameters::Heavy, Recipe.BodyShape.Heavy);
		ApplyFloatParameter(SDMutableParameters::Buff, Recipe.BodyShape.Buff);
		ApplyFloatParameter(SDMutableParameters::Skinny, Recipe.BodyShape.Skinny);
	}
#if WITH_EDITOR
	MarkObjectDirtyForEditor(this);
	MarkObjectDirtyForEditor(GetOwner());
#endif
}

void USDMutableComponent::SetMaterialSettings(const FSDMutableMaterialSettings& InMaterial, const bool bApplyToMutable)
{
#if WITH_EDITOR
	ModifyObjectForEditor(this);
	ModifyObjectForEditor(GetOwner());
#endif
	Recipe.Material = InMaterial;
	OnRecipeChanged.Broadcast(Recipe);

	if (bApplyToMutable)
	{
		ApplyRecipeToMutable();
	}
#if WITH_EDITOR
	MarkObjectDirtyForEditor(this);
	MarkObjectDirtyForEditor(GetOwner());
#endif
}

bool USDMutableComponent::GetPart(const ESDMutablePartSlot Slot, FName& OutOptionId) const
{
	for (const FSDMutablePartSelection& Selection : Recipe.Parts)
	{
		if (Selection.Slot == Slot)
		{
			OutOptionId = Selection.OptionId;
			return true;
		}
	}

	OutOptionId = NAME_None;
	return false;
}

bool USDMutableComponent::ApplyPartToMutable(const FSDMutablePartSelection& Selection) const
{
	if (!Catalog)
	{
		UE_LOG(LogSDMutable, Warning, TEXT("Cannot apply Sidekicks part %s for slot %d: Catalog is null on component %s"),
			*Selection.OptionId.ToString(),
			static_cast<int32>(Selection.Slot),
			*GetNameSafe(this));
		return false;
	}

	FSDMutableSlotTable SlotTable;
	if (!Catalog->GetSlotTable(Selection.Slot, SlotTable) || SlotTable.MutableParameterName.IsNone())
	{
		UE_LOG(LogSDMutable, Warning, TEXT("Cannot apply Sidekicks part %s for slot %d: missing slot table or Mutable parameter name in catalog %s"),
			*Selection.OptionId.ToString(),
			static_cast<int32>(Selection.Slot),
			*GetNameSafe(Catalog));
		return false;
	}

	if (Selection.OptionId.IsNone())
	{
		USkeletalMesh* EmptyMesh = Catalog->LoadEmptySkeletalMesh();
		if (!EmptyMesh)
		{
			UE_LOG(LogSDMutable, Warning, TEXT("Cannot apply None for slot %d: root catalog %s has no loadable EmptySkeletalMesh (%s)"),
				static_cast<int32>(Selection.Slot),
				*GetNameSafe(Catalog),
				*Catalog->EmptySkeletalMesh.ToSoftObjectPath().ToString());
			return false;
		}

		UE_LOG(LogSDMutable, Verbose, TEXT("Applying Sidekicks None selection. Slot=%d MutableParameter=%s EmptyMeshPath=%s"),
			static_cast<int32>(Selection.Slot),
			*SlotTable.MutableParameterName.ToString(),
			*EmptyMesh->GetPathName());
		return ApplySkeletalMeshParameter(SlotTable.MutableParameterName, EmptyMesh);
	}

	FSDMutableCatalogPartEntry Part;
	if (Catalog->FindPartById(Selection.OptionId, Part))
	{
		bool bApplied = false;

		UE_LOG(LogSDMutable, Verbose, TEXT("Applying Sidekicks part. Slot=%d PartId=%s MutableParameter=%s MeshPath=%s"),
			static_cast<int32>(Selection.Slot),
			*Selection.OptionId.ToString(),
			*SlotTable.MutableParameterName.ToString(),
			*Part.SkeletalMesh.ToSoftObjectPath().ToString());

		if (USkeletalMesh* SkeletalMesh = Part.SkeletalMesh.LoadSynchronous())
		{
			bApplied |= ApplySkeletalMeshParameter(SlotTable.MutableParameterName, SkeletalMesh);
		}
		else if (!Part.SkeletalMesh.IsNull())
		{
			UE_LOG(LogSDMutable, Warning, TEXT("Failed to synchronously load Sidekicks skeletal mesh for part %s from %s"),
				*Selection.OptionId.ToString(),
				*Part.SkeletalMesh.ToSoftObjectPath().ToString());
		}

		if (bApplied)
		{
			return true;
		}
	}
	else
	{
		UE_LOG(LogSDMutable, Verbose, TEXT("Part %s not found in catalog %s. Falling back to int option parameter %s."),
			*Selection.OptionId.ToString(),
			*GetNameSafe(Catalog),
			*SlotTable.MutableParameterName.ToString());
	}

	return ApplyIntOptionParameter(SlotTable.MutableParameterName, Selection.OptionId);
}

bool USDMutableComponent::ApplyFloatParameter(const FName ParameterName, const float Value) const
{
	UCustomizableObjectInstance* ActiveInstance = GetActiveCustomizableObjectInstance();
	if (!ActiveInstance)
	{
		UE_LOG(LogSDMutable, Warning, TEXT("Cannot apply float parameter %s: CustomizableObjectInstance is null"),
			*ParameterName.ToString());
		return false;
	}

	UE_LOG(LogSDMutable, VeryVerbose, TEXT("Applying Mutable float parameter %s=%f"),
		*ParameterName.ToString(),
		Value);
#if WITH_EDITOR
#endif
	ActiveInstance->SetFloatParameterSelectedOption(ParameterName.ToString(), Value);
#if WITH_EDITOR
#endif
	return true;
}

bool USDMutableComponent::ApplyColorParameter(const FName ParameterName, const FLinearColor& Value) const
{
	UCustomizableObjectInstance* ActiveInstance = GetActiveCustomizableObjectInstance();
	if (!ActiveInstance)
	{
		UE_LOG(LogSDMutable, Warning, TEXT("Cannot apply color parameter %s: CustomizableObjectInstance is null"),
			*ParameterName.ToString());
		return false;
	}

	UE_LOG(LogSDMutable, VeryVerbose, TEXT("Applying Mutable color parameter %s=%s"),
		*ParameterName.ToString(),
		*Value.ToString());
#if WITH_EDITOR
#endif
	ActiveInstance->SetColorParameterSelectedOption(ParameterName.ToString(), Value);
#if WITH_EDITOR
#endif
	return true;
}

bool USDMutableComponent::ApplyTextureParameter(const FName ParameterName, UTexture* Value) const
{
	UCustomizableObjectInstance* ActiveInstance = GetActiveCustomizableObjectInstance();
	if (!ActiveInstance || !Value)
	{
		UE_LOG(LogSDMutable, Warning, TEXT("Cannot apply texture parameter %s: Instance=%s Texture=%s"),
			*ParameterName.ToString(),
			*GetNameSafe(ActiveInstance),
			*GetNameSafe(Value));
		return false;
	}

	UE_LOG(LogSDMutable, Log, TEXT("Applying Mutable texture parameter %s Texture=%s Path=%s"),
		*ParameterName.ToString(),
		*GetNameSafe(Value),
		*Value->GetPathName());
#if WITH_EDITOR
#endif
	ActiveInstance->SetTextureParameterSelectedOption(ParameterName.ToString(), Value);
#if WITH_EDITOR
#endif
	return true;
}

bool USDMutableComponent::ApplySkeletalMeshParameter(const FName ParameterName, USkeletalMesh* Value) const
{
	UCustomizableObjectInstance* ActiveInstance = GetActiveCustomizableObjectInstance();
	if (!ActiveInstance || !Value)
	{
		UE_LOG(LogSDMutable, Warning, TEXT("Cannot apply skeletal mesh parameter %s: Instance=%s Mesh=%s"),
			*ParameterName.ToString(),
			*GetNameSafe(ActiveInstance),
			*GetNameSafe(Value));
		return false;
	}

	const USkeleton* Skeleton = Value->GetSkeleton();
	UE_LOG(LogSDMutable, Log, TEXT("Applying Mutable skeletal mesh parameter %s Mesh=%s MeshPath=%s Skeleton=%s SkeletonPath=%s"),
		*ParameterName.ToString(),
		*GetNameSafe(Value),
		*Value->GetPathName(),
		*GetNameSafe(Skeleton),
		Skeleton ? *Skeleton->GetPathName() : TEXT("None"));

#if WITH_EDITOR
#endif
	ActiveInstance->SetSkeletalMeshParameterSelectedOption(ParameterName.ToString(), Value);
#if WITH_EDITOR
#endif
	return true;
}

bool USDMutableComponent::ApplyIntOptionParameter(const FName ParameterName, const FName OptionId) const
{
	UCustomizableObjectInstance* ActiveInstance = GetActiveCustomizableObjectInstance();
	if (!ActiveInstance)
	{
		UE_LOG(LogSDMutable, Warning, TEXT("Cannot apply int option parameter %s=%s: CustomizableObjectInstance is null"),
			*ParameterName.ToString(),
			*OptionId.ToString());
		return false;
	}

	UE_LOG(LogSDMutable, Verbose, TEXT("Applying Mutable int option fallback %s=%s"),
		*ParameterName.ToString(),
		*OptionId.ToString());
#if WITH_EDITOR
#endif
	ActiveInstance->SetIntParameterSelectedOption(ParameterName.ToString(), OptionId.ToString());
#if WITH_EDITOR
#endif
	return true;
}

bool USDMutableComponent::IsRuntimeWorld() const
{
	const UWorld* World = GetWorld();
	return World && World->IsGameWorld();
}

UCustomizableObjectInstance* USDMutableComponent::GetActiveCustomizableObjectInstance() const
{
	if (IsRuntimeWorld() && RuntimeCustomizableObjectInstance)
	{
		return RuntimeCustomizableObjectInstance;
	}

	if (!IsRuntimeWorld() && EditorCustomizableObjectInstance)
	{
		return EditorCustomizableObjectInstance;
	}

	return CustomizableObjectInstance;
}
