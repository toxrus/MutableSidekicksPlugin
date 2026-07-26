#include "SDMutable/SDMutableComponent.h"

#include "SDMutable/SDMutableCatalog.h"
#include "SDMutable/SDMutableDeveloperSettings.h"
#include "SDMutable/SDMutableParameters.h"
#include "SDMutable/SDMutableSidekickRecipeAsset.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "MuCO/CustomizableObject.h"
#include "MuCO/CustomizableObjectInstance.h"
#include "MuCO/CustomizableObjectInstanceUsage.h"
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

		// A valid component without a COI can still receive the clone created by this component.
		return FirstValidComponent;
	}

	UCustomizableObject* LoadConfiguredCustomizableObject()
	{
		const USDMutableDeveloperSettings* Settings = GetDefault<USDMutableDeveloperSettings>();
		return Settings ? Settings->SidekicksCustomizableObject.LoadSynchronous() : nullptr;
	}

	bool UsesConfiguredCustomizableObject(
		UCustomizableObjectInstance* Instance,
		const UCustomizableObject* ConfiguredCustomizableObject)
	{
		return Instance
			&& ConfiguredCustomizableObject
			&& Instance->GetCustomizableObject() == ConfiguredCustomizableObject;
	}

	void AssignInstanceToMutableComponent(
		UCustomizableSkeletalComponent& MutableComponent,
		UCustomizableObjectInstance* Instance)
	{
		MutableComponent.SetCustomizableObjectInstance(Instance);

		// Blueprint preview actors do not always replay the attachment notification after their
		// component templates are duplicated. Explicitly bind the usage to the preview mesh so
		// a completed Mutable update has a skeletal component on which to install its result.
		if (UCustomizableObjectInstanceUsage* InstanceUsage = MutableComponent.GetInstanceUsage())
		{
			InstanceUsage->AttachTo(Cast<USkeletalMeshComponent>(MutableComponent.GetAttachParent()));
		}
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
#if WITH_EDITOR
		// Registration/reconstruction is preview hydration, not an authored actor edit.
		TGuardValue<bool> SuppressEditorPreviewDirty(bSuppressEditorPreviewDirty, true);
#endif
		SynchronizeCustomizableObjectInstanceFromOwnerInternal(false);
#if WITH_EDITOR
		// Loaded editor-world actors should preview their saved local recipe before PIE starts.
		RefreshEditorPreviewFromRecipe();
#endif
	}
}

void USDMutableComponent::OnUnregister()
{
	// Drop transient preview/runtime objects before the actor/component leaves the world.
#if WITH_EDITORONLY_DATA
	StopWaitingForConfiguredCustomizableObjectCompilation();
#endif
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
	// These references are world-session state and should be reconstructed from the durable recipe after load.
	StopWaitingForConfiguredCustomizableObjectCompilation();
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
		if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(USDMutableComponent, SourceRecipeAsset))
		{
			HydrateStateFromSourceRecipeAsset();
		}
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

	// Automatic hydration is visual state, not a user edit, so avoid dirtying the level/package.
	TGuardValue<bool> SuppressEditorPreviewDirty(bSuppressEditorPreviewDirty, true);
	ApplyRecipeFromMutableDefaultsAndUpdate(false, false);
}
#endif

#if WITH_EDITORONLY_DATA
bool USDMutableComponent::WaitForConfiguredCustomizableObjectCompilation(UCustomizableObject& CustomizableObject)
{
	if (CustomizableObject.IsCompiled())
	{
		StopWaitingForConfiguredCustomizableObjectCompilation();
		return true;
	}

	if (PendingCompiledCustomizableObject.Get() != &CustomizableObject
		|| !PendingCompiledCustomizableObjectHandle.IsValid())
	{
		StopWaitingForConfiguredCustomizableObjectCompilation();
		PendingCompiledCustomizableObject = &CustomizableObject;
		PendingCompiledCustomizableObjectHandle = CustomizableObject.GetPostCompileDelegate().AddUObject(
			this,
			&USDMutableComponent::HandleConfiguredCustomizableObjectCompiled);
	}

	// Bind before requesting automatic compilation so a fast DDC-backed compile cannot beat the callback.
	if (!CustomizableObject.IsLoading())
	{
		CustomizableObject.ConditionalAutoCompile();
	}

	return CustomizableObject.IsCompiled();
}

void USDMutableComponent::HandleConfiguredCustomizableObjectCompiled()
{
	UCustomizableObject* CompiledCustomizableObject = PendingCompiledCustomizableObject.Get();
	StopWaitingForConfiguredCustomizableObjectCompilation();

	if (!CompiledCustomizableObject
		|| !CompiledCustomizableObject->IsCompiled()
		|| CompiledCustomizableObject != LoadConfiguredCustomizableObject())
	{
		return;
	}

	if (IsRuntimeWorld())
	{
		if (EnsureRuntimeCustomizableObjectInstance())
		{
			ApplyRecipeFromMutableDefaultsAndUpdate(false, false);
		}
	}
	else
	{
		RefreshEditorPreviewFromRecipe();
	}
}

void USDMutableComponent::StopWaitingForConfiguredCustomizableObjectCompilation()
{
	if (UCustomizableObject* PendingObject = PendingCompiledCustomizableObject.Get();
		PendingObject && PendingCompiledCustomizableObjectHandle.IsValid())
	{
		PendingObject->GetPostCompileDelegate().Remove(PendingCompiledCustomizableObjectHandle);
	}

	PendingCompiledCustomizableObject.Reset();
	PendingCompiledCustomizableObjectHandle.Reset();
}
#endif

void USDMutableComponent::SetCustomizableObjectInstance(UCustomizableObjectInstance* InInstance)
{
	UCustomizableObject* ConfiguredCustomizableObject = LoadConfiguredCustomizableObject();
	if (InInstance && !UsesConfiguredCustomizableObject(InInstance, ConfiguredCustomizableObject))
	{
		UE_LOG(LogSDMutable, Error, TEXT("Rejected COI %s on component %s because it does not use the Sidekicks Customizable Object selected in project settings (%s)."),
			*GetNameSafe(InInstance),
			*GetNameSafe(this),
			*GetNameSafe(ConfiguredCustomizableObject));
		return;
	}

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
	UCustomizableObject* ConfiguredCustomizableObject = LoadConfiguredCustomizableObject();
	if (!ConfiguredCustomizableObject)
	{
		if (bLogErrors)
		{
			UE_LOG(LogSDMutable, Error, TEXT("Cannot synchronize COI for %s: no Sidekicks Customizable Object is selected in project settings."),
				*GetNameSafe(this));
		}
		return false;
	}

	if (IsRuntimeWorld() && UsesConfiguredCustomizableObject(RuntimeCustomizableObjectInstance, ConfiguredCustomizableObject))
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
		if (!UsesConfiguredCustomizableObject(EditorCustomizableObjectInstance, ConfiguredCustomizableObject))
		{
			EditorCustomizableObjectInstance = nullptr;
		}
		else
		{
		// Once actor-mode editing has created a private editor clone, keep the skeletal component pointed at it.
			if (ActorInstance != EditorCustomizableObjectInstance)
			{
#if WITH_EDITOR
				if (!bSuppressEditorPreviewDirty)
				{
					ModifyObjectForEditor(MutableSkeletalComponent);
				}
#endif
				AssignInstanceToMutableComponent(*MutableSkeletalComponent, EditorCustomizableObjectInstance);
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

	if (!UsesConfiguredCustomizableObject(ActorInstance, ConfiguredCustomizableObject))
	{
		if (bLogErrors)
		{
			UE_LOG(LogSDMutable, Warning, TEXT("Ignoring COI %s on actor %s because it does not use the configured Sidekicks Customizable Object %s."),
				*GetNameSafe(ActorInstance),
				*GetNameSafe(Owner),
				*GetNameSafe(ConfiguredCustomizableObject));
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

	UCustomizableObject* ConfiguredCustomizableObject = LoadConfiguredCustomizableObject();
	if (!ConfiguredCustomizableObject)
	{
		if (bLogErrors)
		{
			UE_LOG(LogSDMutable, Error, TEXT("Cannot create editor COI for %s: no Sidekicks Customizable Object is selected in project settings."),
				*GetNameSafe(this));
		}
		return false;
	}

	if (!ConfiguredCustomizableObject->IsCompiled())
	{
#if WITH_EDITORONLY_DATA
		WaitForConfiguredCustomizableObjectCompilation(*ConfiguredCustomizableObject);
#endif
		return false;
	}

	if (UsesConfiguredCustomizableObject(EditorCustomizableObjectInstance, ConfiguredCustomizableObject))
	{
		return SynchronizeCustomizableObjectInstanceFromOwnerInternal(bLogErrors);
	}
	EditorCustomizableObjectInstance = nullptr;

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
	if (!UsesConfiguredCustomizableObject(TemplateInstance, ConfiguredCustomizableObject))
	{
		TemplateInstance = MutableSkeletalComponent->GetCustomizableObjectInstance();
	}
	if (!UsesConfiguredCustomizableObject(TemplateInstance, ConfiguredCustomizableObject))
	{
		TemplateInstance = TemplateCustomizableObjectInstance.LoadSynchronous();
	}
	if (!UsesConfiguredCustomizableObject(TemplateInstance, ConfiguredCustomizableObject))
	{
		if (USDMutableSidekickRecipeAsset* RecipeAsset = SourceRecipeAsset.LoadSynchronous())
		{
			TemplateInstance = RecipeAsset->CustomizableObjectInstance.LoadSynchronous();
		}
	}
	if (!UsesConfiguredCustomizableObject(TemplateInstance, ConfiguredCustomizableObject))
	{
		TemplateInstance = nullptr;
	}

	// Editor clones isolate placed actors from shared COI assets while still using the template's Customizable Object.
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
		EditorCustomizableObjectInstance = ConfiguredCustomizableObject->CreateInstance();
	}

	if (!EditorCustomizableObjectInstance)
	{
		if (bLogErrors)
		{
			UE_LOG(LogSDMutable, Error, TEXT("Cannot create editor COI for %s on actor %s from configured Sidekicks Customizable Object %s."),
				*GetNameSafe(this),
				*GetNameSafe(Owner),
				*GetNameSafe(ConfiguredCustomizableObject));
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
	AssignInstanceToMutableComponent(*MutableSkeletalComponent, EditorCustomizableObjectInstance);
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
		UCustomizableObjectInstance* RecipeInstance = RecipeAsset->CustomizableObjectInstance.LoadSynchronous();
		UCustomizableObject* ConfiguredCustomizableObject = LoadConfiguredCustomizableObject();
		if (UsesConfiguredCustomizableObject(RecipeInstance, ConfiguredCustomizableObject))
		{
			TemplateCustomizableObjectInstance = RecipeAsset->CustomizableObjectInstance;
		}
		else
		{
			UE_LOG(LogSDMutable, Warning, TEXT("Recipe asset %s COI %s does not use configured Sidekicks Customizable Object %s; copied recipe data but ignored its COI."),
				*GetNameSafe(RecipeAsset),
				*GetNameSafe(RecipeInstance),
				*GetNameSafe(ConfiguredCustomizableObject));
		}
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

bool USDMutableComponent::HydrateStateFromSourceRecipeAsset()
{
	if (SourceRecipeAsset.IsNull())
	{
		return false;
	}

	USDMutableSidekickRecipeAsset* RecipeAsset = SourceRecipeAsset.LoadSynchronous();
	if (!RecipeAsset)
	{
		UE_LOG(LogSDMutable, Warning, TEXT("Cannot hydrate Sidekicks state for %s: source recipe asset %s failed to load."),
			*GetNameSafe(this),
			*SourceRecipeAsset.ToSoftObjectPath().ToString());
		return false;
	}

	Recipe = RecipeAsset->Recipe;
	ColorPalette = RecipeAsset->ColorPalette;
	OnRecipeChanged.Broadcast(Recipe);
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

	UCustomizableObject* ConfiguredCustomizableObject = LoadConfiguredCustomizableObject();
	if (!ConfiguredCustomizableObject)
	{
		UE_LOG(LogSDMutable, Error, TEXT("Cannot create runtime COI for %s on actor %s: no Sidekicks Customizable Object is selected in project settings."),
			*GetNameSafe(this),
			*GetNameSafe(Owner));
		return false;
	}

	if (!ConfiguredCustomizableObject->IsCompiled())
	{
#if WITH_EDITORONLY_DATA
		WaitForConfiguredCustomizableObjectCompilation(*ConfiguredCustomizableObject);
#endif
		return false;
	}

	if (UsesConfiguredCustomizableObject(RuntimeCustomizableObjectInstance, ConfiguredCustomizableObject))
	{
		if (MutableSkeletalComponent->GetCustomizableObjectInstance() != RuntimeCustomizableObjectInstance)
		{
			AssignInstanceToMutableComponent(*MutableSkeletalComponent, RuntimeCustomizableObjectInstance);
		}
		CustomizableObjectInstance = RuntimeCustomizableObjectInstance;
		return true;
	}
	RuntimeCustomizableObjectInstance = nullptr;

	UCustomizableObjectInstance* TemplateInstance = TemplateCustomizableObjectInstance.LoadSynchronous();
	if (!UsesConfiguredCustomizableObject(TemplateInstance, ConfiguredCustomizableObject))
	{
		TemplateInstance = MutableSkeletalComponent->GetCustomizableObjectInstance();
	}
	if (!UsesConfiguredCustomizableObject(TemplateInstance, ConfiguredCustomizableObject))
	{
		if (USDMutableSidekickRecipeAsset* RecipeAsset = SourceRecipeAsset.LoadSynchronous())
		{
			TemplateInstance = RecipeAsset->CustomizableObjectInstance.LoadSynchronous();
		}
	}
	if (!UsesConfiguredCustomizableObject(TemplateInstance, ConfiguredCustomizableObject))
	{
		TemplateInstance = nullptr;
	}

	// Game worlds get a per-actor transient clone so runtime changes cannot leak through shared template assets.
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
		RuntimeCustomizableObjectInstance = ConfiguredCustomizableObject->CreateInstance();
	}

	if (!RuntimeCustomizableObjectInstance)
	{
		UE_LOG(LogSDMutable, Error, TEXT("Cannot create runtime COI for %s on actor %s from configured Sidekicks Customizable Object %s."),
			*GetNameSafe(this),
			*GetNameSafe(Owner),
			*GetNameSafe(ConfiguredCustomizableObject));
		return false;
	}

	RuntimeCustomizableObjectInstance->SetFlags(RF_Transient);
	AssignInstanceToMutableComponent(*MutableSkeletalComponent, RuntimeCustomizableObjectInstance);
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
	// Parameter writes must target the current-world clone when one exists, not the durable template COI.
	const bool bInstanceReady = IsRuntimeWorld()
		? EnsureRuntimeCustomizableObjectInstance()
		: EnsureEditorCustomizableObjectInstance();
	if (!bInstanceReady)
	{
		return;
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
		// Rebuild the derived BaseColor texture from local palette state. The transient package avoids old-world references.
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
	const bool bInstanceReady = IsRuntimeWorld()
		? EnsureRuntimeCustomizableObjectInstance()
		: EnsureEditorCustomizableObjectInstance();
	if (!bInstanceReady)
	{
		return false;
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
	const bool bInstanceReady = IsRuntimeWorld()
		? EnsureRuntimeCustomizableObjectInstance()
		: EnsureEditorCustomizableObjectInstance();
	if (!bInstanceReady)
	{
		return;
	}

	UCustomizableObjectInstance* ActiveInstance = GetActiveCustomizableObjectInstance();
	if (ActiveInstance)
	{
		UE_LOG(LogSDMutable, Log, TEXT("Updating Mutable instance %s after applying Sidekicks parameters. IgnoreCloseDist=%s ForceHighPriority=%s"),
			*GetNameSafe(ActiveInstance),
			bIgnoreCloseDist ? TEXT("true") : TEXT("false"),
			bForceHighPriority ? TEXT("true") : TEXT("false"));

		ActiveInstance->UpdateSkeletalMeshAsync(bIgnoreCloseDist, bForceHighPriority);
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
	USDMutableCatalog* EffectiveCatalog = GetConfiguredCatalog();
	if (!EffectiveCatalog)
	{
		UE_LOG(LogSDMutable, Warning, TEXT("Cannot apply Sidekicks part %s for slot %d: no catalog is set on component %s or in project settings"),
			*Selection.OptionId.ToString(),
			static_cast<int32>(Selection.Slot),
			*GetNameSafe(this));
		return false;
	}

	FSDMutableSlotTable SlotTable;
	if (!EffectiveCatalog->GetSlotTable(Selection.Slot, SlotTable) || SlotTable.MutableParameterName.IsNone())
	{
		UE_LOG(LogSDMutable, Warning, TEXT("Cannot apply Sidekicks part %s for slot %d: missing slot table or Mutable parameter name in catalog %s"),
			*Selection.OptionId.ToString(),
			static_cast<int32>(Selection.Slot),
			*GetNameSafe(EffectiveCatalog));
		return false;
	}

	if (Selection.OptionId.IsNone())
	{
		// Mutable skeletal mesh parameters require a valid mesh, so clearing a slot applies the catalog's empty mesh.
		USkeletalMesh* EmptyMesh = EffectiveCatalog->LoadEmptySkeletalMesh();
		if (!EmptyMesh)
		{
			UE_LOG(LogSDMutable, Warning, TEXT("Cannot apply None for slot %d: root catalog %s has no loadable EmptySkeletalMesh (%s)"),
				static_cast<int32>(Selection.Slot),
				*GetNameSafe(EffectiveCatalog),
				*EffectiveCatalog->EmptySkeletalMesh.ToSoftObjectPath().ToString());
			return false;
		}

		UE_LOG(LogSDMutable, Verbose, TEXT("Applying Sidekicks None selection. Slot=%d MutableParameter=%s EmptyMeshPath=%s"),
			static_cast<int32>(Selection.Slot),
			*SlotTable.MutableParameterName.ToString(),
			*EmptyMesh->GetPathName());
		return ApplySkeletalMeshParameter(SlotTable.MutableParameterName, EmptyMesh);
	}

	FSDMutableCatalogPartEntry Part;
	if (EffectiveCatalog->FindPartById(Selection.OptionId, Part))
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
			*GetNameSafe(EffectiveCatalog),
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

USDMutableCatalog* USDMutableComponent::GetConfiguredCatalog() const
{
	const USDMutableDeveloperSettings* Settings = GetDefault<USDMutableDeveloperSettings>();
	return Settings ? Settings->RootCatalog.LoadSynchronous() : nullptr;
}

UCustomizableObjectInstance* USDMutableComponent::GetActiveCustomizableObjectInstance() const
{
	UCustomizableObject* ConfiguredCustomizableObject = LoadConfiguredCustomizableObject();
	if (!ConfiguredCustomizableObject || !ConfiguredCustomizableObject->IsCompiled())
	{
		return nullptr;
	}

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
