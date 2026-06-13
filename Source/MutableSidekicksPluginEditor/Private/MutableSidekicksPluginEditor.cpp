// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "IAssetTypeActions.h"
#include "SDMutable/SDMutableAssetFactories.h"
#include "SDMutable/SDMutableAssetTypeActions.h"
#include "SDMutable/SSDMutableEditorWidget.h"
#include "ToolMenus.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Containers/Ticker.h"
#include "MuCO/CustomizableObjectInstance.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "FMutableSidekicksPluginEditorModule"

namespace
{
	const FName SidekicksMutableEditorTabName(TEXT("SidekicksMutableEditor"));
}

class FMutableSidekicksPluginEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
			SidekicksMutableEditorTabName,
			FOnSpawnTab::CreateRaw(this, &FMutableSidekicksPluginEditorModule::SpawnEditorTab))
			.SetDisplayName(LOCTEXT("SidekicksMutableTabTitle", "Sidekicks Mutable"))
			.SetMenuType(ETabSpawnerMenuType::Hidden);

		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FMutableSidekicksPluginEditorModule::RegisterMenus));

		RegisterAssetTools();

		BindAssetEditorEvents();
		if (!bAssetEditorEventsBound)
		{
			PendingBindHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FMutableSidekicksPluginEditorModule::BindAssetEditorEventsOnTick));
		}
	}

	virtual void ShutdownModule() override
	{
		UnbindAssetEditorEvents();
		UnregisterAssetTools();

		if (PendingOpenTabHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(PendingOpenTabHandle);
			PendingOpenTabHandle.Reset();
		}

		if (PendingBindHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(PendingBindHandle);
			PendingBindHandle.Reset();
		}

		if (UToolMenus::IsToolMenuUIEnabled())
		{
			UToolMenus::UnRegisterStartupCallback(this);
			UToolMenus::UnregisterOwner(this);
		}

		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(SidekicksMutableEditorTabName);
	}

private:
	void RegisterAssetTools()
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
		IAssetTools& AssetTools = AssetToolsModule.Get();

		SidekicksMutableAssetCategory = AssetTools.RegisterAdvancedAssetCategory(
			TEXT("SidekicksMutable"),
			LOCTEXT("SidekicksMutableAssetCategory", "Sidekicks Mutable"));
		SDMutableEditorAssetCategory::Set(SidekicksMutableAssetCategory);

		RegisterAssetTypeAction(AssetTools, MakeShared<FSDMutableSidekickRecipeAssetTypeActions>(SidekicksMutableAssetCategory));
		RegisterAssetTypeAction(AssetTools, MakeShared<FSDMutableCatalogPackAssetTypeActions>(SidekicksMutableAssetCategory));
	}

	void RegisterAssetTypeAction(IAssetTools& AssetTools, const TSharedRef<IAssetTypeActions>& Action)
	{
		AssetTools.RegisterAssetTypeActions(Action);
		RegisteredAssetTypeActions.Add(Action);
	}

	void UnregisterAssetTools()
	{
		if (!FModuleManager::Get().IsModuleLoaded(TEXT("AssetTools")))
		{
			RegisteredAssetTypeActions.Reset();
			return;
		}

		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		for (const TSharedPtr<IAssetTypeActions>& Action : RegisteredAssetTypeActions)
		{
			if (Action.IsValid())
			{
				AssetTools.UnregisterAssetTypeActions(Action.ToSharedRef());
			}
		}
		RegisteredAssetTypeActions.Reset();
	}

	TSharedRef<SDockTab> SpawnEditorTab(const FSpawnTabArgs& SpawnTabArgs)
	{
		TSharedPtr<SSDMutableEditorWidget> EditorWidget;
		SAssignNew(EditorWidget, SSDMutableEditorWidget);
		ActiveEditorWidget = EditorWidget;

		if (PendingTargetCoi.IsValid())
		{
			EditorWidget->SetTargetCustomizableObjectInstance(PendingTargetCoi.Get());
		}

		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				EditorWidget.ToSharedRef()
			];
	}

	void BindAssetEditorEvents()
	{
		if (bAssetEditorEventsBound)
		{
			return;
		}

		if (GEditor)
		{
			if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				AssetOpenedHandle = AssetEditorSubsystem->OnAssetOpenedInEditor().AddRaw(this, &FMutableSidekicksPluginEditorModule::HandleAssetOpenedInEditor);
				AssetEditorOpenedHandle = AssetEditorSubsystem->OnAssetEditorOpened().AddRaw(this, &FMutableSidekicksPluginEditorModule::HandleAssetEditorOpened);
				AssetEditorRequestedOpenHandle = AssetEditorSubsystem->OnAssetEditorRequestedOpen().AddRaw(this, &FMutableSidekicksPluginEditorModule::HandleAssetEditorRequestedOpen);
				bAssetEditorEventsBound = true;
			}
		}
	}

	bool BindAssetEditorEventsOnTick(float DeltaTime)
	{
		BindAssetEditorEvents();
		if (bAssetEditorEventsBound)
		{
			PendingBindHandle.Reset();
			return false;
		}

		return true;
	}

	void UnbindAssetEditorEvents()
	{
		if (!bAssetEditorEventsBound)
		{
			return;
		}

		if (GEditor)
		{
			if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				AssetEditorSubsystem->OnAssetOpenedInEditor().Remove(AssetOpenedHandle);
				AssetEditorSubsystem->OnAssetEditorOpened().Remove(AssetEditorOpenedHandle);
				AssetEditorSubsystem->OnAssetEditorRequestedOpen().Remove(AssetEditorRequestedOpenHandle);
			}
		}

		AssetOpenedHandle.Reset();
		AssetEditorOpenedHandle.Reset();
		AssetEditorRequestedOpenHandle.Reset();
		bAssetEditorEventsBound = false;
	}

	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);

		UToolMenu* WindowMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window"));
		FToolMenuSection& Section = WindowMenu->FindOrAddSection(TEXT("WindowLayout"));
		Section.AddMenuEntry(
			TEXT("OpenSidekicksMutableEditor"),
			LOCTEXT("OpenSidekicksMutableEditor", "Sidekicks Mutable"),
			LOCTEXT("OpenSidekicksMutableEditorTooltip", "Open the dockable Sidekicks Mutable editor tab."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FMutableSidekicksPluginEditorModule::OpenEditorTab)));

		Section.AddMenuEntry(
			TEXT("OpenSidekicksMutableEditorWindow"),
			LOCTEXT("OpenSidekicksMutableEditorWindow", "Sidekicks Mutable Window"),
			LOCTEXT("OpenSidekicksMutableEditorWindowTooltip", "Open the Sidekicks Mutable editor in a standalone Slate window."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FMutableSidekicksPluginEditorModule::OpenEditorWindow)));
	}

	void OpenEditorTab()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(SidekicksMutableEditorTabName);
	}

	void ScheduleOpenEditorTab(UCustomizableObjectInstance* TargetCoi, IAssetEditorInstance* AssetEditorInstance)
	{
		PendingTargetCoi = TargetCoi;
		PendingAssetEditorInstance = AssetEditorInstance;

		if (ActiveEditorWidget.IsValid())
		{
			ActiveEditorWidget.Pin()->SetTargetCustomizableObjectInstance(TargetCoi);
			PendingAssetEditorInstance = nullptr;
			return;
		}

		if (PendingOpenTabHandle.IsValid())
		{
			return;
		}

		PendingOpenTabHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FMutableSidekicksPluginEditorModule::OpenEditorTabOnTick));
	}

	bool OpenEditorTabOnTick(float DeltaTime)
	{
		PendingOpenTabHandle.Reset();

		if (PendingAssetEditorInstance)
		{
			PendingAssetEditorInstance->InvokeTab(FTabId(SidekicksMutableEditorTabName));
		}
		else
		{
			OpenEditorTab();
		}

		if (ActiveEditorWidget.IsValid())
		{
			ActiveEditorWidget.Pin()->SetTargetCustomizableObjectInstance(PendingTargetCoi.Get());
		}

		PendingAssetEditorInstance = nullptr;
		return false;
	}

	UCustomizableObjectInstance* GetCustomizableObjectInstanceAsset(UObject* Asset) const
	{
		if (UCustomizableObjectInstance* Coi = Cast<UCustomizableObjectInstance>(Asset))
		{
			return Coi;
		}

		return nullptr;
	}

	void HandleAssetEditorRequestedOpen(UObject* Asset)
	{
		if (UCustomizableObjectInstance* Coi = GetCustomizableObjectInstanceAsset(Asset))
		{
			PendingTargetCoi = Coi;
			if (ActiveEditorWidget.IsValid())
			{
				ActiveEditorWidget.Pin()->SetTargetCustomizableObjectInstance(Coi);
			}
		}
	}

	void HandleAssetEditorOpened(UObject* Asset)
	{
		if (UCustomizableObjectInstance* Coi = GetCustomizableObjectInstanceAsset(Asset))
		{
			PendingTargetCoi = Coi;
			if (ActiveEditorWidget.IsValid())
			{
				ActiveEditorWidget.Pin()->SetTargetCustomizableObjectInstance(Coi);
			}
		}
	}

	void HandleAssetOpenedInEditor(UObject* Asset, IAssetEditorInstance* AssetEditorInstance)
	{
		UCustomizableObjectInstance* Coi = GetCustomizableObjectInstanceAsset(Asset);
		if (!Coi)
		{
			return;
		}

		ScheduleOpenEditorTab(Coi, AssetEditorInstance);
	}

	void OpenEditorWindow()
	{
		TSharedRef<SWindow> EditorWindow = SNew(SWindow)
			.Title(LOCTEXT("SidekicksMutableWindowTitle", "Sidekicks Mutable"))
			.ClientSize(FVector2D(1280.0f, 900.0f))
			.SupportsMaximize(true)
			.SupportsMinimize(true)
			[
				SNew(SSDMutableEditorWidget)
			];

		FSlateApplication::Get().AddWindow(EditorWindow);
	}

	FDelegateHandle AssetOpenedHandle;
	FDelegateHandle AssetEditorOpenedHandle;
	FDelegateHandle AssetEditorRequestedOpenHandle;
	FTSTicker::FDelegateHandle PendingOpenTabHandle;
	FTSTicker::FDelegateHandle PendingBindHandle;
	TWeakPtr<SSDMutableEditorWidget> ActiveEditorWidget;
	TWeakObjectPtr<UCustomizableObjectInstance> PendingTargetCoi;
	IAssetEditorInstance* PendingAssetEditorInstance = nullptr;
	TArray<TSharedPtr<IAssetTypeActions>> RegisteredAssetTypeActions;
	uint32 SidekicksMutableAssetCategory = EAssetTypeCategories::Misc;
	bool bAssetEditorEventsBound = false;
};

IMPLEMENT_MODULE(FMutableSidekicksPluginEditorModule, MutableSidekicksPluginEditor)

#undef LOCTEXT_NAMESPACE
