#include "SDMutable/SSDMutableEditorWidget.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Actor.h"
#include "IContentBrowserSingleton.h"
#include "IDesktopPlatform.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "MuCO/CustomizableObject.h"
#include "MuCO/CustomizableObjectInstance.h"
#include "MuCO/CustomizableSkeletalComponent.h"
#include "SDMutable/SDMutableCatalog.h"
#include "SDMutable/SDMutableColorPreset.h"
#include "SDMutable/SDMutableComponent.h"
#include "SDMutable/SDMutableDeveloperSettings.h"
#include "SDMutable/SDMutableParameters.h"
#include "SDMutable/SDMutableSidekickRecipeAsset.h"
#include "SDMutable/SDMutableTextureBuilderLibrary.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateColorBrush.h"
#include "Styling/SlateBrush.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Engine/SkeletalMesh.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "SSDMutableEditorWidget"

namespace
{
	void AccumulatePackStats(const TArray<TSoftObjectPtr<USDMutableCatalogPack>>& PackRefs, int32& InOutLoadedPackCount, int32& InOutPartCount)
	{
		for (const TSoftObjectPtr<USDMutableCatalogPack>& PackRef : PackRefs)
		{
			const USDMutableCatalogPack* Pack = PackRef.LoadSynchronous();
			if (!Pack)
			{
				continue;
			}

			++InOutLoadedPackCount;

			for (const FSDMutableCatalogSlotParts& SlotParts : Pack->Slots)
			{
				InOutPartCount += SlotParts.Parts.Num();
			}
		}
	}

	FText GetSlotDisplayName(const ESDMutablePartSlot Slot)
	{
		if (const UEnum* SlotEnum = StaticEnum<ESDMutablePartSlot>())
		{
			return SlotEnum->GetDisplayNameTextByValue(static_cast<int64>(Slot));
		}

		return FText::FromString(TEXT("Unknown"));
	}

	FText GetPartDisplayName(const FSDMutableCatalogPartEntry& Part)
	{
		return Part.DisplayName.IsEmpty() ? FText::FromName(Part.PartId) : Part.DisplayName;
	}

	FText GetPackDisplayName(const FName PackId)
	{
		return PackId.IsNone() ? LOCTEXT("UnknownPackDisplayName", "Unknown Pack") : FText::FromName(PackId);
	}

	void AccumulateGlobalPackFilterItems(
		const TArray<TSoftObjectPtr<USDMutableCatalogPack>>& PackRefs,
		const ESDMutableCatalogPackType PackType,
		const TMap<FName, bool>& PreviousEnabledState,
		TArray<TSharedPtr<FSDMutablePackListItem>>& OutPackItems)
	{
		for (const TSoftObjectPtr<USDMutableCatalogPack>& PackRef : PackRefs)
		{
			const USDMutableCatalogPack* Pack = PackRef.LoadSynchronous();
			if (!Pack)
			{
				continue;
			}

			TSharedPtr<FSDMutablePackListItem> PackItem = MakeShared<FSDMutablePackListItem>();
			PackItem->PackId = Pack->PackId;
			PackItem->DisplayName = Pack->DisplayName.IsEmpty() ? GetPackDisplayName(Pack->PackId) : Pack->DisplayName;
			PackItem->PackType = PackType;
			for (const FSDMutableCatalogSlotParts& SlotParts : Pack->Slots)
			{
				PackItem->PartCount += SlotParts.Parts.Num();
			}
			if (const bool* bPreviousEnabled = PreviousEnabledState.Find(PackItem->PackId))
			{
				PackItem->bEnabled = *bPreviousEnabled;
			}

			OutPackItems.Add(PackItem);
		}
	}

	TSharedPtr<FSlateBrush> MakeThumbnailBrush(const FSDMutableCatalogPartEntry& Part)
	{
		UTexture2D* Thumbnail = Part.UIThumbnail.LoadSynchronous();
		if (!Thumbnail)
		{
			return nullptr;
		}

		TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
		Brush->SetResourceObject(Thumbnail);
		Brush->ImageSize = FVector2D(64.0f, 64.0f);
		return Brush;
	}

	TSharedPtr<FSlateBrush> MakeTransparentThumbnailBrush()
	{
		TSharedPtr<FSlateColorBrush> Brush = MakeShared<FSlateColorBrush>(FLinearColor::Transparent);
		Brush->ImageSize = FVector2D(64.0f, 64.0f);
		return Brush;
	}

	TSharedRef<SWidget> MakeThumbnailWidget(const TSharedPtr<FSlateBrush>& ThumbnailBrush)
	{
		if (ThumbnailBrush.IsValid())
		{
			return SNew(SImage)
				.Image(ThumbnailBrush.Get());
		}

		return SNew(SBorder)
			.Padding(4.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoThumbnailFallback", "No\nThumb"))
				.Justification(ETextJustify::Center)
			];
	}

	FString MakeSafeAssetName(const FString& SourceName)
	{
		FString SafeName = SourceName;
		for (TCHAR& Character : SafeName)
		{
			if (!FChar::IsAlnum(Character) && Character != TEXT('_'))
			{
				Character = TEXT('_');
			}
		}

		SafeName.RemoveFromStart(TEXT("_"));
		SafeName.RemoveFromEnd(TEXT("_"));
		return SafeName.IsEmpty() ? TEXT("Sidekick") : SafeName;
	}

	bool SaveAssetPackage(UObject& Asset)
	{
		UPackage* Package = Asset.GetPackage();
		if (!Package)
		{
			return false;
		}

		const FString PackageFileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(),
			FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Package, &Asset, *PackageFileName, SaveArgs);
	}

	FString PromptForRecipeSaveObjectPath(const FString& DefaultAssetName)
	{
		FSaveAssetDialogConfig SaveAssetDialogConfig;
		SaveAssetDialogConfig.DialogTitleOverride = LOCTEXT("SaveSidekickRecipeDialogTitle", "Save Sidekick Recipe");
		SaveAssetDialogConfig.DefaultPath = TEXT("/Game/SidekicksMutable/Recipes");
		SaveAssetDialogConfig.DefaultAssetName = DefaultAssetName;
		SaveAssetDialogConfig.AssetClassNames.Add(USDMutableSidekickRecipeAsset::StaticClass()->GetClassPathName());
		SaveAssetDialogConfig.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::Disallow;

		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		return ContentBrowserModule.Get().CreateModalSaveAssetDialog(SaveAssetDialogConfig);
	}

	FString GetRecipeJsonExportDirectory()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SidekicksMutable/RecipeExports"));
	}

	FString PromptForRecipeJsonSavePath(const FString& DefaultFileName)
	{
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		if (!DesktopPlatform)
		{
			return FString();
		}

		TArray<FString> SaveFilenames;
		const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
		const bool bPickedPath = DesktopPlatform->SaveFileDialog(
			ParentWindowHandle,
			TEXT("Export Sidekick Recipe JSON"),
			GetRecipeJsonExportDirectory(),
			DefaultFileName,
			TEXT("JSON files (*.json)|*.json"),
			EFileDialogFlags::None,
			SaveFilenames);

		return bPickedPath && SaveFilenames.Num() > 0 ? SaveFilenames[0] : FString();
	}

	FString PromptForRecipeJsonOpenPath()
	{
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		if (!DesktopPlatform)
		{
			return FString();
		}

		TArray<FString> OpenFilenames;
		const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
		const bool bPickedPath = DesktopPlatform->OpenFileDialog(
			ParentWindowHandle,
			TEXT("Import Sidekick Recipe JSON"),
			GetRecipeJsonExportDirectory(),
			TEXT(""),
			TEXT("JSON files (*.json)|*.json"),
			EFileDialogFlags::None,
			OpenFilenames);

		return bPickedPath && OpenFilenames.Num() > 0 ? OpenFilenames[0] : FString();
	}

	FString MakePackageName(const FString& PackagePath, const FString& AssetName)
	{
		FString CleanPath = PackagePath;
		CleanPath.ReplaceInline(TEXT("\\"), TEXT("/"));
		CleanPath.RemoveFromEnd(TEXT("/"));
		return FString::Printf(TEXT("%s/%s"), *CleanPath, *AssetName);
	}

	UObject* FindObjectInPackage(const FString& PackageName, const FString& AssetName, UClass* AssetClass)
	{
		UPackage* Package = FindPackage(nullptr, *PackageName);
		return Package ? StaticFindObject(AssetClass, Package, *AssetName) : nullptr;
	}

	void ApplyRecipeToCustomizableObjectInstance(
		UCustomizableObjectInstance& Instance,
		const USDMutableCatalog* Catalog,
		const FSDMutableSidekickRecipe& Recipe)
	{
		Instance.SetDefaultValues();
		Instance.SetFloatParameterSelectedOption(SDMutableParameters::MaleFemale.ToString(), Recipe.BodyShape.MaleFemale);
		Instance.SetFloatParameterSelectedOption(SDMutableParameters::Heavy.ToString(), Recipe.BodyShape.Heavy);
		Instance.SetFloatParameterSelectedOption(SDMutableParameters::Buff.ToString(), Recipe.BodyShape.Buff);
		Instance.SetFloatParameterSelectedOption(SDMutableParameters::Skinny.ToString(), Recipe.BodyShape.Skinny);
		Instance.SetColorParameterSelectedOption(SDMutableParameters::SkinColor.ToString(), Recipe.Material.SkinColor);
		Instance.SetColorParameterSelectedOption(SDMutableParameters::EyeColor.ToString(), Recipe.Material.EyeColor);
		Instance.SetColorParameterSelectedOption(SDMutableParameters::DirtColor.ToString(), Recipe.Material.DirtColor);
		Instance.SetFloatParameterSelectedOption(SDMutableParameters::CutWeight.ToString(), Recipe.Material.CutWeight);
		Instance.SetFloatParameterSelectedOption(SDMutableParameters::DirtWeight.ToString(), Recipe.Material.DirtWeight);
		Instance.SetFloatParameterSelectedOption(SDMutableParameters::DarkWeight.ToString(), Recipe.Material.DarkWeight);

		if (UTexture* BaseColor = Recipe.Material.BaseColor.LoadSynchronous())
		{
			Instance.SetTextureParameterSelectedOption(SDMutableParameters::BaseColor.ToString(), BaseColor);
		}

		if (Catalog)
		{
			for (const FSDMutablePartSelection& Selection : Recipe.Parts)
			{
				FSDMutableSlotTable SlotTable;
				if (!Catalog->GetSlotTable(Selection.Slot, SlotTable) || SlotTable.MutableParameterName.IsNone())
				{
					continue;
				}

				if (Selection.OptionId.IsNone())
				{
					if (USkeletalMesh* EmptyMesh = Catalog->LoadEmptySkeletalMesh())
					{
						Instance.SetSkeletalMeshParameterSelectedOption(SlotTable.MutableParameterName.ToString(), EmptyMesh);
					}
					continue;
				}

				FSDMutableCatalogPartEntry Part;
				if (Catalog->FindPartById(Selection.OptionId, Part))
				{
					if (USkeletalMesh* Mesh = Part.SkeletalMesh.LoadSynchronous())
					{
						Instance.SetSkeletalMeshParameterSelectedOption(SlotTable.MutableParameterName.ToString(), Mesh);
						continue;
					}
				}

				Instance.SetIntParameterSelectedOption(SlotTable.MutableParameterName.ToString(), Selection.OptionId.ToString());
			}
		}
	}

	bool ReadColorPaletteFromTexture(UTexture2D* Texture, FSDMutableColorPalette& OutPalette)
	{
		if (!Texture || Texture->Source.GetSizeX() != 32 || Texture->Source.GetSizeY() != 32 || Texture->Source.GetFormat() != TSF_BGRA8)
		{
			return false;
		}

		const uint8* SourceData = Texture->Source.LockMipReadOnly(0);
		if (!SourceData)
		{
			return false;
		}

		OutPalette.EnsureColorSlotCount();
		const FColor* Pixels = reinterpret_cast<const FColor*>(SourceData);
		constexpr int32 TextureSize = 32;
		constexpr int32 PatchSize = 2;
		constexpr int32 PatchesPerRow = TextureSize / PatchSize;

		for (int32 ColorIndex = 0; ColorIndex < FSDMutableColorPalette::NumSidekicksColorSlots; ++ColorIndex)
		{
			const int32 PatchX = ColorIndex % PatchesPerRow;
			const int32 PatchY = ColorIndex / PatchesPerRow;
			const int32 PixelIndex = (PatchY * PatchSize) * TextureSize + (PatchX * PatchSize);
			OutPalette.SetColorSlot(ColorIndex, FLinearColor::FromSRGBColor(Pixels[PixelIndex]), true);
		}

		Texture->Source.UnlockMip(0);
		return true;
	}

	void LoadSidekicksRecipeFromCustomizableObjectInstance(
		UCustomizableObjectInstance& Instance,
		const USDMutableCatalog* Catalog,
		FSDMutableSidekickRecipe& OutRecipe,
		FSDMutableColorPalette& OutPalette)
	{
		OutRecipe = FSDMutableSidekickRecipe();

		if (Instance.ContainsFloatParameter(SDMutableParameters::MaleFemale.ToString()))
		{
			OutRecipe.BodyShape.MaleFemale = Instance.GetFloatParameterSelectedOption(SDMutableParameters::MaleFemale.ToString());
		}
		if (Instance.ContainsFloatParameter(SDMutableParameters::Heavy.ToString()))
		{
			OutRecipe.BodyShape.Heavy = Instance.GetFloatParameterSelectedOption(SDMutableParameters::Heavy.ToString());
		}
		if (Instance.ContainsFloatParameter(SDMutableParameters::Buff.ToString()))
		{
			OutRecipe.BodyShape.Buff = Instance.GetFloatParameterSelectedOption(SDMutableParameters::Buff.ToString());
		}
		if (Instance.ContainsFloatParameter(SDMutableParameters::Skinny.ToString()))
		{
			OutRecipe.BodyShape.Skinny = Instance.GetFloatParameterSelectedOption(SDMutableParameters::Skinny.ToString());
		}

		if (Instance.ContainsColorParameter(SDMutableParameters::SkinColor.ToString()))
		{
			OutRecipe.Material.SkinColor = Instance.GetColorParameterSelectedOption(SDMutableParameters::SkinColor.ToString());
		}
		if (Instance.ContainsColorParameter(SDMutableParameters::EyeColor.ToString()))
		{
			OutRecipe.Material.EyeColor = Instance.GetColorParameterSelectedOption(SDMutableParameters::EyeColor.ToString());
		}
		if (Instance.ContainsColorParameter(SDMutableParameters::DirtColor.ToString()))
		{
			OutRecipe.Material.DirtColor = Instance.GetColorParameterSelectedOption(SDMutableParameters::DirtColor.ToString());
		}
		if (Instance.ContainsFloatParameter(SDMutableParameters::CutWeight.ToString()))
		{
			OutRecipe.Material.CutWeight = Instance.GetFloatParameterSelectedOption(SDMutableParameters::CutWeight.ToString());
		}
		if (Instance.ContainsFloatParameter(SDMutableParameters::DirtWeight.ToString()))
		{
			OutRecipe.Material.DirtWeight = Instance.GetFloatParameterSelectedOption(SDMutableParameters::DirtWeight.ToString());
		}
		if (Instance.ContainsFloatParameter(SDMutableParameters::DarkWeight.ToString()))
		{
			OutRecipe.Material.DarkWeight = Instance.GetFloatParameterSelectedOption(SDMutableParameters::DarkWeight.ToString());
		}

		if (Instance.ContainsTextureParameter(SDMutableParameters::BaseColor.ToString()))
		{
			if (UTexture* BaseColor = Instance.GetTextureParameterSelectedOption(SDMutableParameters::BaseColor.ToString()))
			{
				OutRecipe.Material.BaseColor = TSoftObjectPtr<UTexture>(BaseColor);
				if (UTexture2D* BaseColor2D = Cast<UTexture2D>(BaseColor))
				{
					ReadColorPaletteFromTexture(BaseColor2D, OutPalette);
				}
			}
		}

		if (!Catalog)
		{
			return;
		}

		USkeletalMesh* EmptyMesh = Catalog->LoadEmptySkeletalMesh();
		for (const FSDMutableSlotTable& SlotTable : Catalog->PartTables)
		{
			if (SlotTable.Slot == ESDMutablePartSlot::None || SlotTable.MutableParameterName.IsNone())
			{
				continue;
			}

			FName OptionId = NAME_None;
			const FString ParameterName = SlotTable.MutableParameterName.ToString();
			if (Instance.ContainsSkeletalMeshParameter(ParameterName))
			{
				USkeletalMesh* SelectedMesh = Instance.GetSkeletalMeshParameterSelectedOption(ParameterName);
				if (SelectedMesh && SelectedMesh != EmptyMesh)
				{
					TArray<FSDMutableCatalogPartEntry> Parts;
					Catalog->GetPartsForSlot(SlotTable.Slot, Parts);
					for (const FSDMutableCatalogPartEntry& Part : Parts)
					{
						if (Part.SkeletalMesh.LoadSynchronous() == SelectedMesh)
						{
							OptionId = Part.PartId;
							break;
						}
					}
				}
			}
			else if (Instance.ContainsEnumParameter(ParameterName) || Instance.ContainsIntParameter(ParameterName))
			{
				OptionId = FName(*Instance.GetEnumParameterSelectedOption(ParameterName));
			}

			FSDMutablePartSelection& Selection = OutRecipe.Parts.AddDefaulted_GetRef();
			Selection.Slot = SlotTable.Slot;
			Selection.OptionId = OptionId;
		}
	}

	USDMutableSidekickRecipeAsset* FindRecipeAssetForCustomizableObjectInstance(UCustomizableObjectInstance* Coi)
	{
		if (!Coi)
		{
			return nullptr;
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> RecipeAssets;
		AssetRegistryModule.Get().GetAssetsByClass(USDMutableSidekickRecipeAsset::StaticClass()->GetClassPathName(), RecipeAssets, true);

		for (const FAssetData& RecipeAssetData : RecipeAssets)
		{
			USDMutableSidekickRecipeAsset* RecipeAsset = Cast<USDMutableSidekickRecipeAsset>(RecipeAssetData.GetAsset());
			if (!RecipeAsset)
			{
				continue;
			}

			if (RecipeAsset->CustomizableObjectInstance.LoadSynchronous() == Coi)
			{
				return RecipeAsset;
			}
		}

		return nullptr;
	}

	UCustomizableSkeletalComponent* FindMutableSkeletalComponent(AActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}

		TInlineComponentArray<UCustomizableSkeletalComponent*> MutableSkeletalComponents;
		Actor->GetComponents(MutableSkeletalComponents);

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

	void SetActorCustomizableObjectInstance(AActor* Actor, UCustomizableObjectInstance* Instance)
	{
		UCustomizableSkeletalComponent* MutableSkeletalComponent = FindMutableSkeletalComponent(Actor);
		if (!MutableSkeletalComponent)
		{
			return;
		}

		Actor->Modify();
		MutableSkeletalComponent->Modify();
		MutableSkeletalComponent->SetCustomizableObjectInstance(Instance);
		MutableSkeletalComponent->UpdateSkeletalMeshAsync(true);
		MutableSkeletalComponent->MarkPackageDirty();
		Actor->MarkPackageDirty();
	}
}

SSDMutableEditorWidget::~SSDMutableEditorWidget()
{
	CancelDeferredCoiRecipeApply();
	ResetPreviewTargetToDefaults();
}

void SSDMutableEditorWidget::Construct(const FArguments& InArgs)
{
	RefreshCatalog();
	RefreshTargetFromSelection();
	RebuildSlotItems();
	RebuildPackItems();
	RebuildPartItems();
	EditorColorPalette.EnsureColorSlotCount();
	ColorStatus = TEXT("Color palette is editor-local until recipe palette persistence is added.");

	ChildSlot
	[
		SNew(SBorder)
		.Padding(16.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Title", "Sidekicks Mutable"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
				.Text(this, &SSDMutableEditorWidget::GetCatalogStatusText)
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				SNew(STextBlock)
				.Text(this, &SSDMutableEditorWidget::GetCatalogSummaryText)
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(4.0f)
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("RefreshCatalog", "Refresh Catalog"))
					.OnClicked_Lambda([this]()
					{
						RefreshCatalog();
						RebuildSlotItems();
						RebuildPackItems();
						RebuildPartItems();
						RebuildSelectionPanels();
						return FReply::Handled();
					})
				]
				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("UseSelectedActor", "Use Selected Actor"))
					.OnClicked_Lambda([this]()
					{
						RefreshTargetFromSelection();
						RebuildSelectionPanels();
						return FReply::Handled();
					})
				]
				+ SUniformGridPanel::Slot(2, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("UseSelectedCoi", "Use Selected COI"))
					.OnClicked_Lambda([this]()
					{
						RefreshTargetFromSelectedCoi();
						RebuildSelectionPanels();
						return FReply::Handled();
					})
				]
				+ SUniformGridPanel::Slot(3, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("SaveRecipe", "Save"))
					.ToolTipText(LOCTEXT("SaveRecipeTooltip", "Save the active recipe DataAsset, its color texture, and its COI. Disabled until a recipe DataAsset is associated."))
					.IsEnabled_Lambda([this]()
					{
						return TargetRecipeAsset.IsValid();
					})
					.OnClicked_Lambda([this]()
					{
						SaveCurrentRecipe();
						return FReply::Handled();
					})
				]
				+ SUniformGridPanel::Slot(4, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("SaveRecipeAssetAs", "Save Recipe Asset As"))
					.ToolTipText(LOCTEXT("SaveRecipeAssetAsTooltip", "Create a new recipe DataAsset, COI, and color texture in the selected folder."))
					.OnClicked_Lambda([this]()
					{
						SaveCurrentRecipeAsset();
						return FReply::Handled();
					})
				]
				+ SUniformGridPanel::Slot(5, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("ExportRecipeJson", "Export JSON"))
					.ToolTipText(LOCTEXT("ExportRecipeJsonTooltip", "Export the current live Sidekicks recipe as a shareable JSON preset. Does not require a recipe DataAsset."))
					.OnClicked_Lambda([this]()
					{
						ExportCurrentRecipeJson();
						return FReply::Handled();
					})
				]
				+ SUniformGridPanel::Slot(6, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("ImportRecipeJson", "Import JSON"))
					.ToolTipText(LOCTEXT("ImportRecipeJsonTooltip", "Import a Sidekicks recipe JSON preset onto the current actor, COI, or recipe DataAsset target."))
					.OnClicked_Lambda([this]()
					{
						ImportCurrentRecipeJson();
						return FReply::Handled();
					})
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
				.Text(this, &SSDMutableEditorWidget::GetTargetStatusText)
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f)
			[
				SNew(SSeparator)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("MeshSelectionTitle", "Mesh Selection"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SBorder)
				.Padding(8.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 6.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("PackPanelTitle", "Pack Filters"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SAssignNew(PackListBox, SVerticalBox)
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SBorder)
				.Padding(8.0f)
				[
					BuildMaterialControlsWidget()
				]
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SBorder)
				.Padding(8.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(0.30f)
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(SBorder)
						.Padding(8.0f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 0.0f, 0.0f, 6.0f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.FillWidth(1.0f)
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("SlotPanelTitle", "Slots"))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
								]
								+ SHorizontalBox::Slot()
								.AutoWidth()
								[
									SNew(SButton)
									.ContentPadding(FMargin(6.0f, 2.0f))
									.Text(LOCTEXT("ClearAllSlotsButton", "Clear All"))
									.ToolTipText(LOCTEXT("ClearAllSlotsTooltip", "Set all mesh types to None."))
									.IsEnabled_Lambda([this]()
									{
										return !SlotItems.IsEmpty() && LoadedCatalog.IsValid() && (TargetComponent.IsValid() || TargetCustomizableObjectInstance.IsValid());
									})
									.OnClicked_Lambda([this]()
									{
										ClearAllSlots();
										return FReply::Handled();
									})
								]
							]
							+ SVerticalBox::Slot()
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot()
								[
									SAssignNew(SlotListBox, SVerticalBox)
								]
							]
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(0.70f)
					[
						SNew(SBorder)
						.Padding(8.0f)
						[
							SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 0.0f, 0.0f, 6.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("PartPanelTitle", "Mesh Option"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 0.0f, 0.0f, 8.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(0.0f, 0.0f, 4.0f, 0.0f)
							[
								SNew(SButton)
								.Text(LOCTEXT("PreviousPartOption", "<"))
								.ToolTipText(LOCTEXT("PreviousPartOptionTooltip", "Select the previous mesh option in the current filtered list."))
								.IsEnabled_Lambda([this]()
								{
									return CanSelectAdjacentPartOption(-1);
								})
								.OnClicked_Lambda([this]()
								{
									SelectAdjacentPartOption(-1);
									return FReply::Handled();
								})
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(0.0f, 0.0f, 4.0f, 0.0f)
							[
								SNew(SButton)
								.Text(LOCTEXT("NextPartOption", ">"))
								.ToolTipText(LOCTEXT("NextPartOptionTooltip", "Select the next mesh option in the current filtered list."))
								.IsEnabled_Lambda([this]()
								{
									return CanSelectAdjacentPartOption(1);
								})
								.OnClicked_Lambda([this]()
								{
									SelectAdjacentPartOption(1);
									return FReply::Handled();
								})
							]
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							[
								SAssignNew(PartMenuAnchor, SMenuAnchor)
								.Placement(MenuPlacement_BelowAnchor)
								.OnGetMenuContent(this, &SSDMutableEditorWidget::BuildPartDropdownMenuWidget)
								[
									SNew(SButton)
									.HAlign(HAlign_Fill)
									.OnClicked_Lambda([this]()
									{
										if (PartMenuAnchor.IsValid())
										{
											const bool bOpenMenu = !PartMenuAnchor->IsOpen();
											PartMenuAnchor->SetIsOpen(bOpenMenu);
											if (bOpenMenu && PartSearchTextBox.IsValid())
											{
												FSlateApplication::Get().SetKeyboardFocus(PartSearchTextBox);
												PartSearchTextBox->SelectAllText();
											}
										}
										return FReply::Handled();
									})
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot()
										.FillWidth(1.0f)
										[
											SNew(STextBlock)
											.Text(this, &SSDMutableEditorWidget::GetSelectedPartText)
											.AutoWrapText(true)
										]
										+ SHorizontalBox::Slot()
										.AutoWidth()
										.Padding(8.0f, 0.0f, 0.0f, 0.0f)
										[
											SNew(STextBlock)
											.Text(LOCTEXT("PartDropdownArrow", "v"))
										]
									]
								]
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(4.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SButton)
								.ContentPadding(FMargin(6.0f, 2.0f))
								.Text(LOCTEXT("ClearCurrentPartOption", "X"))
								.ToolTipText(LOCTEXT("ClearCurrentPartOptionTooltip", "Set the current mesh option to None."))
								.IsEnabled_Lambda([this]()
								{
									return SelectedSlotItem.IsValid() && LoadedCatalog.IsValid() && (TargetComponent.IsValid() || TargetCustomizableObjectInstance.IsValid());
								})
								.OnClicked_Lambda([this]()
								{
									ClearSlot(SelectedSlotItem);
									return FReply::Handled();
								})
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 0.0f, 0.0f, 8.0f)
						[
							SAssignNew(PartListBox, SVerticalBox)
						]
						+ SVerticalBox::Slot()
						.FillHeight(1.0f)
						[
							SNew(SExpandableArea)
							.AreaTitle(LOCTEXT("PartColorsAreaTitle", "Selected Mesh Colors"))
							.InitiallyCollapsed(false)
							.BodyContent()
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 0.0f, 0.0f, 8.0f)
								[
									SNew(STextBlock)
									.Text(this, &SSDMutableEditorWidget::GetColorStatusText)
									.AutoWrapText(true)
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 0.0f, 0.0f, 8.0f)
								[
									SNew(SUniformGridPanel)
									.SlotPadding(4.0f)
									+ SUniformGridPanel::Slot(0, 0)
									[
										SNew(SButton)
										.Text(LOCTEXT("GenerateRandomPalette", "Randomize Palette"))
										.OnClicked_Lambda([this]()
										{
											GenerateRandomColorPalette();
											return FReply::Handled();
										})
									]
									+ SUniformGridPanel::Slot(1, 0)
									[
										SNew(SButton)
										.Text(LOCTEXT("ClearPalette", "Clear Overrides"))
										.OnClicked_Lambda([this]()
										{
											ClearColorPalette();
											return FReply::Handled();
										})
									]
									+ SUniformGridPanel::Slot(2, 0)
									[
										SNew(SButton)
										.Text(LOCTEXT("ApplyPalette", "Apply Transient Texture"))
										.OnClicked_Lambda([this]()
										{
											ApplyColorPaletteToTarget();
											return FReply::Handled();
										})
									]
								]
								+ SVerticalBox::Slot()
								[
									SNew(SScrollBox)
									+ SScrollBox::Slot()
									[
										SAssignNew(ColorSwatchGrid, SUniformGridPanel)
										.SlotPadding(2.0f)
									]
								]
							]
						]
					]
				]
			]
		]
		]
	];

	RebuildSelectionPanels();
	RebuildColorSwatchGrid();
}

void SSDMutableEditorWidget::ResetPreviewTargetToDefaults()
{
	// Target switching only releases Slate-owned preview resources; component-owned editor COIs keep their actor preview state.
	PreviewColorTexture.Reset();
}

void SSDMutableEditorWidget::RefreshCatalog()
{
	LoadedCatalog.Reset();
	CatalogStatus.Reset();
	SpeciesPackCount = 0;
	OutfitPackCount = 0;
	SharedPackCount = 0;
	UnknownPackCount = 0;
	LoadedPackCount = 0;
	LoadedPartCount = 0;

	const USDMutableDeveloperSettings* Settings = GetDefault<USDMutableDeveloperSettings>();
	if (!Settings || Settings->RootCatalog.IsNull())
	{
		CatalogStatus = TEXT("No root catalog configured. Set Project Settings > Plugins > Sidekicks Mutable > Root Catalog.");
		return;
	}

	USDMutableCatalog* Catalog = Settings->RootCatalog.LoadSynchronous();
	if (!Catalog)
	{
		CatalogStatus = FString::Printf(TEXT("Failed to load root catalog: %s"), *Settings->RootCatalog.ToString());
		return;
	}

	LoadedCatalog = Catalog;
	SpeciesPackCount = Catalog->SpeciesPackCatalogs.Num();
	OutfitPackCount = Catalog->OutfitPackCatalogs.Num();
	SharedPackCount = Catalog->SharedPackCatalogs.Num();
	UnknownPackCount = Catalog->UnknownPackCatalogs.Num();

	AccumulatePackStats(Catalog->SpeciesPackCatalogs, LoadedPackCount, LoadedPartCount);
	AccumulatePackStats(Catalog->OutfitPackCatalogs, LoadedPackCount, LoadedPartCount);
	AccumulatePackStats(Catalog->SharedPackCatalogs, LoadedPackCount, LoadedPartCount);
	AccumulatePackStats(Catalog->UnknownPackCatalogs, LoadedPackCount, LoadedPartCount);

	CatalogStatus = FString::Printf(TEXT("Loaded root catalog: %s"), *Catalog->GetPathName());
}

void SSDMutableEditorWidget::RefreshTargetFromSelection()
{
	ResetPreviewTargetToDefaults();
	TargetComponent.Reset();
	TargetCustomizableObjectInstance.Reset();
	TargetStatus = TEXT("No selected actor with a USDMutableComponent.");

	if (!GEditor)
	{
		return;
	}

	USelection* SelectedActors = GEditor->GetSelectedActors();
	if (!SelectedActors)
	{
		return;
	}

	for (FSelectionIterator It(*SelectedActors); It; ++It)
	{
		AActor* Actor = Cast<AActor>(*It);
		if (!Actor)
		{
			continue;
		}

		USDMutableComponent* Component = Actor->FindComponentByClass<USDMutableComponent>();
		if (!Component)
		{
			continue;
		}

		TargetComponent = Component;
		Component->SynchronizeCustomizableObjectInstanceFromOwner();
		// Actor mode starts from the component's durable local state, not from the previous Slate target or COI defaults.
		EditorRecipe = Component->Recipe;
		EditorColorPalette = Component->ColorPalette;
		EditorColorPalette.EnsureColorSlotCount();

		if (!Component->Catalog && LoadedCatalog.IsValid())
		{
			Component->Modify();
			Actor->Modify();
			Component->Catalog = LoadedCatalog.Get();
			Component->MarkPackageDirty();
			Actor->MarkPackageDirty();
		}

		Component->ApplyRecipeFromMutableDefaultsAndUpdate(false, false);

		TargetStatus = FString::Printf(
			TEXT("Target: %s / %s. COI=%s. Preview uses this actor's SD component recipe."),
			*Actor->GetName(),
			*Component->GetName(),
			*GetNameSafe(Component->CustomizableObjectInstance.Get()));
		return;
	}
}

void SSDMutableEditorWidget::RefreshTargetFromSelectedCoi()
{
	ResetPreviewTargetToDefaults();
	TargetComponent.Reset();
	TargetCustomizableObjectInstance.Reset();
	EditorRecipe = FSDMutableSidekickRecipe();
	TargetStatus = TEXT("No selected UCustomizableObjectInstance asset.");

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	TArray<FAssetData> SelectedAssets;
	ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);
	for (const FAssetData& AssetData : SelectedAssets)
	{
		if (!AssetData.IsInstanceOf(UCustomizableObjectInstance::StaticClass()))
		{
			continue;
		}

		if (UCustomizableObjectInstance* Coi = Cast<UCustomizableObjectInstance>(AssetData.GetAsset()))
		{
			TargetCustomizableObjectInstance = Coi;
			TargetStatus = FString::Printf(TEXT("Target COI asset: %s. Editing uses an editor-local Sidekicks recipe."), *Coi->GetPathName());
			return;
		}
	}

	if (GEditor)
	{
		USelection* SelectedObjects = GEditor->GetSelectedObjects();
		if (SelectedObjects)
		{
			for (FSelectionIterator It(*SelectedObjects); It; ++It)
			{
				if (UCustomizableObjectInstance* Coi = Cast<UCustomizableObjectInstance>(*It))
				{
					TargetCustomizableObjectInstance = Coi;
					TargetStatus = FString::Printf(TEXT("Target COI object: %s. Editing uses an editor-local Sidekicks recipe."), *Coi->GetPathName());
					return;
				}
			}
		}
	}
}

void SSDMutableEditorWidget::SetTargetCustomizableObjectInstance(UCustomizableObjectInstance* InCustomizableObjectInstance)
{
	if (USDMutableSidekickRecipeAsset* RecipeAsset = FindRecipeAssetForCustomizableObjectInstance(InCustomizableObjectInstance))
	{
		// A recipe DA that references this COI is the authoritative source; edits should keep DA, texture, and COI in sync.
		SetTargetRecipeAsset(RecipeAsset);
		return;
	}

	if (TargetCustomizableObjectInstance.Get() == InCustomizableObjectInstance && !TargetRecipeAsset.IsValid())
	{
		return;
	}

	CancelDeferredCoiRecipeApply();
	ResetPreviewTargetToDefaults();
	TargetComponent.Reset();
	TargetCustomizableObjectInstance = InCustomizableObjectInstance;
	TargetRecipeAsset.Reset();
	EditorRecipe = FSDMutableSidekickRecipe();
	EditorColorPalette.ClearColorPaletteOverrides();

	if (InCustomizableObjectInstance)
	{
		LoadSidekicksRecipeFromCustomizableObjectInstance(*InCustomizableObjectInstance, LoadedCatalog.Get(), EditorRecipe, EditorColorPalette);
		TargetStatus = FString::Printf(TEXT("Target COI asset: %s. Editing uses an editor-local Sidekicks recipe."), *InCustomizableObjectInstance->GetPathName());
	}
	else
	{
		TargetStatus = TEXT("No selected UCustomizableObjectInstance asset.");
	}

	RebuildPartItems();
	RebuildSelectionPanels();
	RebuildColorSwatchGrid();
}

void SSDMutableEditorWidget::SetTargetRecipeAsset(USDMutableSidekickRecipeAsset* InRecipeAsset)
{
	UCustomizableObjectInstance* RecipeCoi = InRecipeAsset ? InRecipeAsset->CustomizableObjectInstance.LoadSynchronous() : nullptr;
	if (TargetRecipeAsset.Get() == InRecipeAsset && TargetCustomizableObjectInstance.Get() == RecipeCoi)
	{
		return;
	}

	CancelDeferredCoiRecipeApply();
	ResetPreviewTargetToDefaults();
	TargetComponent.Reset();
	TargetRecipeAsset = InRecipeAsset;
	TargetCustomizableObjectInstance.Reset();
	EditorRecipe = FSDMutableSidekickRecipe();
	EditorColorPalette.ClearColorPaletteOverrides();

	if (!InRecipeAsset)
	{
		TargetStatus = TEXT("No Sidekicks recipe DataAsset target.");
		RebuildPartItems();
		RebuildSelectionPanels();
		RebuildColorSwatchGrid();
		return;
	}

	EditorRecipe = InRecipeAsset->Recipe;
	EditorColorPalette = InRecipeAsset->ColorPalette;
	EditorColorPalette.EnsureColorSlotCount();

	// DA targets prefer their persisted texture asset so Save can update the same generated companion asset.
	if (UTexture2D* ColorTexture = InRecipeAsset->ColorTexture.LoadSynchronous())
	{
		EditorRecipe.Material.BaseColor = TSoftObjectPtr<UTexture>(ColorTexture);
	}

	if (RecipeCoi)
	{
		TargetCustomizableObjectInstance = RecipeCoi;
		ApplyEditorRecipeToTargetCoi();
	}

	TargetStatus = FString::Printf(
		TEXT("Target recipe DA: %s. DA is authoritative for COI %s."),
		*InRecipeAsset->GetPathName(),
		*GetNameSafe(TargetCustomizableObjectInstance.Get()));

	RebuildPartItems();
	RebuildSelectionPanels();
	RebuildColorSwatchGrid();
}

void SSDMutableEditorWidget::RebuildSlotItems()
{
	const ESDMutablePartSlot PreviousSlot = SelectedSlotItem.IsValid() ? SelectedSlotItem->Slot : ESDMutablePartSlot::None;
	SlotItems.Reset();
	SelectedSlotItem.Reset();

	USDMutableCatalog* Catalog = LoadedCatalog.Get();
	if (!Catalog)
	{
		return;
	}

	for (const FSDMutableSlotTable& SlotTable : Catalog->PartTables)
	{
		if (SlotTable.Slot == ESDMutablePartSlot::None)
		{
			continue;
		}

		TArray<FSDMutableCatalogPartEntry> Parts;
		Catalog->GetPartsForSlot(SlotTable.Slot, Parts);

		TSharedPtr<FSDMutableSlotListItem> Item = MakeShared<FSDMutableSlotListItem>();
		Item->Slot = SlotTable.Slot;
		Item->DisplayName = GetSlotDisplayName(SlotTable.Slot);
		Item->PartCount = Parts.Num() + 1;
		SlotItems.Add(Item);

		if (SlotTable.Slot == PreviousSlot)
		{
			SelectedSlotItem = Item;
		}
	}

	if (!SelectedSlotItem.IsValid() && !SlotItems.IsEmpty())
	{
		SelectedSlotItem = SlotItems[0];
	}
}

void SSDMutableEditorWidget::RebuildPackItems()
{
	TMap<FName, bool> PreviousEnabledState;
	for (const TSharedPtr<FSDMutablePackListItem>& PackItem : PackItems)
	{
		if (PackItem.IsValid())
		{
			PreviousEnabledState.Add(PackItem->PackId, PackItem->bEnabled);
		}
	}

	PackItems.Reset();
	SelectedPackItem.Reset();

	USDMutableCatalog* Catalog = LoadedCatalog.Get();
	if (!Catalog)
	{
		return;
	}

	AccumulateGlobalPackFilterItems(Catalog->SpeciesPackCatalogs, ESDMutableCatalogPackType::Species, PreviousEnabledState, PackItems);
	AccumulateGlobalPackFilterItems(Catalog->OutfitPackCatalogs, ESDMutableCatalogPackType::Outfit, PreviousEnabledState, PackItems);
	AccumulateGlobalPackFilterItems(Catalog->SharedPackCatalogs, ESDMutableCatalogPackType::Shared, PreviousEnabledState, PackItems);
	AccumulateGlobalPackFilterItems(Catalog->UnknownPackCatalogs, ESDMutableCatalogPackType::Unknown, PreviousEnabledState, PackItems);

	PackItems.Sort([](const TSharedPtr<FSDMutablePackListItem>& Left, const TSharedPtr<FSDMutablePackListItem>& Right)
	{
		if (!Left.IsValid() || !Right.IsValid())
		{
			return Left.IsValid();
		}

		if (Left->PackType != Right->PackType)
		{
			return static_cast<uint8>(Left->PackType) < static_cast<uint8>(Right->PackType);
		}

		return Left->DisplayName.ToString() < Right->DisplayName.ToString();
	});
}

void SSDMutableEditorWidget::RebuildPartItems()
{
	AllPartItems.Reset();
	PartItems.Reset();

	USDMutableCatalog* Catalog = LoadedCatalog.Get();
	if (!Catalog || !SelectedSlotItem.IsValid())
	{
		return;
	}

	FSDMutableCatalogPartEntry NonePart;
	NonePart.PartId = NAME_None;
	NonePart.MutableOptionId = NAME_None;
	NonePart.DisplayName = LOCTEXT("NonePartDisplayName", "None");
	NonePart.SkeletalMesh = Catalog->EmptySkeletalMesh;
	NonePart.UIThumbnail = Catalog->EmptySkeletalMeshUIThumbnail;
	NonePart.SourceAssetPath = Catalog->EmptySkeletalMesh.ToSoftObjectPath().ToString();

	TSharedPtr<FSDMutablePartListItem> NoneItem = MakeShared<FSDMutablePartListItem>();
	NoneItem->Part = NonePart;
	NoneItem->ThumbnailBrush = MakeThumbnailBrush(NonePart);
	if (!NoneItem->ThumbnailBrush.IsValid())
	{
		NoneItem->ThumbnailBrush = MakeTransparentThumbnailBrush();
	}
	AllPartItems.Add(NoneItem);

	TArray<FSDMutableCatalogPartEntry> Parts;
	Catalog->GetPartsForSlot(SelectedSlotItem->Slot, Parts);

	for (const FSDMutableCatalogPartEntry& Part : Parts)
	{
		const TSharedPtr<FSDMutablePackListItem>* MatchingPackItem = PackItems.FindByPredicate([&Part](const TSharedPtr<FSDMutablePackListItem>& PackItem)
		{
			return PackItem.IsValid() && PackItem->PackId == Part.PackId;
		});

		if (MatchingPackItem && MatchingPackItem->IsValid() && !(*MatchingPackItem)->bEnabled)
		{
			continue;
		}

		TSharedPtr<FSDMutablePartListItem> PartItem = MakeShared<FSDMutablePartListItem>();
		PartItem->Part = Part;
		PartItem->ThumbnailBrush = MakeThumbnailBrush(Part);
		AllPartItems.Add(PartItem);
	}

	const FName CurrentOptionId = SelectedSlotItem.IsValid() ? GetSelectedOptionId(SelectedSlotItem->Slot) : NAME_None;
	SelectedPartItem.Reset();
	for (const TSharedPtr<FSDMutablePartListItem>& PartItem : AllPartItems)
	{
		if (PartItem.IsValid() && PartItem->Part.PartId == CurrentOptionId)
		{
			SelectedPartItem = PartItem;
			break;
		}
	}

	if (!SelectedPartItem.IsValid() && !AllPartItems.IsEmpty())
	{
		SelectedPartItem = AllPartItems[0];
	}

	ApplyPartSearchFilter();
}

void SSDMutableEditorWidget::ApplyPartSearchFilter()
{
	PartItems.Reset();

	const FString SearchText = PartSearchFilter.TrimStartAndEnd();
	if (SearchText.IsEmpty())
	{
		PartItems = AllPartItems;
		return;
	}

	const FString SearchTextLower = SearchText.ToLower();
	for (const TSharedPtr<FSDMutablePartListItem>& PartItem : AllPartItems)
	{
		if (!PartItem.IsValid())
		{
			continue;
		}

		const FSDMutableCatalogPartEntry& Part = PartItem->Part;
		const FString SearchableText = FString::Printf(
			TEXT("%s %s %s %s"),
			*GetPartDisplayName(Part).ToString(),
			*Part.PartId.ToString(),
			*Part.PackId.ToString(),
			*Part.SourceAssetPath).ToLower();

		if (SearchableText.Contains(SearchTextLower))
		{
			PartItems.Add(PartItem);
		}
	}
}

void SSDMutableEditorWidget::RebuildPackListWidget()
{
	if (!PackListBox.IsValid())
	{
		return;
	}

	PackListBox->ClearChildren();

	if (PackItems.IsEmpty())
	{
		PackListBox->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoPackFiltersAvailable", "No pack catalogs available."))
			.AutoWrapText(true)
		];
		return;
	}

	auto AddPackFilterRow = [this](const FText& RowLabel, const ESDMutableCatalogPackType PackType)
	{
		TSharedRef<SWrapBox> FilterWrapBox = SNew(SWrapBox)
			.UseAllottedSize(true)
			.InnerSlotPadding(FVector2D(10.0f, 2.0f));
		bool bHasRowItems = false;

		for (const TSharedPtr<FSDMutablePackListItem>& PackItem : PackItems)
		{
			if (!PackItem.IsValid() || PackItem->PackType != PackType)
			{
				continue;
			}

			bHasRowItems = true;
			FilterWrapBox->AddSlot()
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsChecked(PackItem->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
				.OnCheckStateChanged_Lambda([this, PackItem](const ECheckBoxState NewState)
				{
					SetPackFilterEnabled(PackItem, NewState == ECheckBoxState::Checked);
				})
				[
					SNew(STextBlock)
					.Text(FText::Format(
						LOCTEXT("PackFilterFormat", "{0} ({1})"),
						PackItem->DisplayName,
						FText::AsNumber(PackItem->PartCount)))
				]
			];
		}

		if (!bHasRowItems)
		{
			FilterWrapBox->AddSlot()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoPackFiltersForRow", "None"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			];
		}

		PackListBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Top)
			.Padding(0.0f, 2.0f, 8.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(RowLabel)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				FilterWrapBox
			]
		];
	};

	AddPackFilterRow(LOCTEXT("SpeciesPackFilterRow", "Species"), ESDMutableCatalogPackType::Species);
	AddPackFilterRow(LOCTEXT("OutfitPackFilterRow", "Outfits"), ESDMutableCatalogPackType::Outfit);
}

void SSDMutableEditorWidget::RebuildSlotListWidget()
{
	if (!SlotListBox.IsValid())
	{
		return;
	}

	SlotListBox->ClearChildren();

	if (SlotItems.IsEmpty())
	{
		SlotListBox->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoSlotsAvailable", "No catalog slots available."))
			.AutoWrapText(true)
		];
		return;
	}

	for (const TSharedPtr<FSDMutableSlotListItem>& SlotItem : SlotItems)
	{
		if (!SlotItem.IsValid())
		{
			continue;
		}

		const bool bSelected = SlotItem == SelectedSlotItem;
		const FName SelectedOptionId = GetSelectedOptionId(SlotItem->Slot);
		const FText SelectionText = SelectedOptionId.IsNone() ? LOCTEXT("SlotNoneSelection", "None") : FText::FromName(SelectedOptionId);

		SlotListBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.ContentPadding(FMargin(5.0f, 1.0f))
				.ToolTipText(LOCTEXT("ClearSlotTooltip", "Set this mesh type to None."))
				.OnClicked_Lambda([this, SlotItem]()
				{
					ClearSlot(SlotItem);
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ClearSlotButton", "x"))
					.Justification(ETextJustify::Center)
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SButton)
				.HAlign(HAlign_Fill)
				.OnClicked_Lambda([this, SlotItem]()
				{
					SelectSlot(SlotItem);
					return FReply::Handled();
				})
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::Format(
							LOCTEXT("SlotButtonFormat", "{0}{1} ({2})"),
							bSelected ? FText::FromString(TEXT("> ")) : FText::GetEmpty(),
							SlotItem->DisplayName,
							FText::AsNumber(SlotItem->PartCount)))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::Format(LOCTEXT("SlotSelectionFormat", "Selected: {0}"), SelectionText))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.AutoWrapText(true)
					]
				]
			]
		];
	}
}

void SSDMutableEditorWidget::RebuildPartListWidget()
{
	if (!PartListBox.IsValid())
	{
		return;
	}

	PartListBox->ClearChildren();

	if (!SelectedSlotItem.IsValid())
	{
		PartListBox->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoSlotSelected", "No slot selected."))
		];
		return;
	}

	if (!SelectedPartItem.IsValid())
	{
		PartListBox->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoSelectedPart", "No mesh option selected."))
		];
		return;
	}

	const FSDMutableCatalogPartEntry& Part = SelectedPartItem->Part;
	const FText PackName = Part.PackId.IsNone() ? FText::GetEmpty() : FText::Format(LOCTEXT("PartPackFormat", "Pack: {0}"), FText::FromName(Part.PackId));
	const FText ColorCount = FText::Format(LOCTEXT("PartColorCount", "Color properties: {0}"), FText::AsNumber(Part.ColorProperties.Num()));

	PartListBox->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 6.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 10.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(256.0f)
			.HeightOverride(256.0f)
			[
				MakeThumbnailWidget(SelectedPartItem->ThumbnailBrush)
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(GetPartDisplayName(Part))
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(PackName)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(ColorCount)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.AutoWrapText(true)
			]
		]
	];
}

void SSDMutableEditorWidget::RebuildSelectionPanels()
{
	RebuildPackListWidget();
	RebuildSlotListWidget();
	RefreshPartSelectionWidgets();
}

void SSDMutableEditorWidget::RefreshPartSelectionWidgets()
{
	if (PartListView.IsValid())
	{
		PartListView->RequestListRefresh();
		if (PartItems.Contains(SelectedPartItem))
		{
			PartListView->SetSelection(SelectedPartItem, ESelectInfo::Direct);
		}
		else
		{
			PartListView->ClearSelection();
		}
	}
	RebuildPartListWidget();
	RebuildColorSwatchGrid();
}

TSharedRef<SWidget> SSDMutableEditorWidget::BuildMorphControlsWidget()
{
	const auto MakeMorphRow = [this](const FText& Label, const FName ParameterName)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(80.0f)
				[
					SNew(STextBlock)
					.Text(Label)
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.Value_Lambda([this, ParameterName]()
				{
					return GetBodyShapeValue(ParameterName);
				})
				.OnValueChanged_Lambda([this, ParameterName](const float Value)
				{
					SetBodyShapeValue(ParameterName, Value);
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(58.0f)
				[
					SNew(SNumericEntryBox<float>)
					.MinValue(0.0f)
					.MaxValue(1.0f)
					.MinSliderValue(0.0f)
					.MaxSliderValue(1.0f)
					.AllowSpin(false)
					.Value_Lambda([this, ParameterName]() -> TOptional<float>
					{
						return GetBodyShapeValue(ParameterName);
					})
					.OnValueCommitted_Lambda([this, ParameterName](const float Value, ETextCommit::Type)
					{
						SetBodyShapeValue(ParameterName, Value);
					})
				]
			];
	};

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("MorphControlsTitle", "Morph Sliders"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 3.0f)
		[
			MakeMorphRow(LOCTEXT("MorphMaleFemale", "Male/Female"), SDMutableParameters::MaleFemale)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 3.0f)
		[
			MakeMorphRow(LOCTEXT("MorphHeavy", "Heavy"), SDMutableParameters::Heavy)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 3.0f)
		[
			MakeMorphRow(LOCTEXT("MorphBuff", "Buff"), SDMutableParameters::Buff)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeMorphRow(LOCTEXT("MorphSkinny", "Skinny"), SDMutableParameters::Skinny)
		];
}

TSharedRef<SWidget> SSDMutableEditorWidget::BuildMaterialControlsWidget()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(0.32f)
		.Padding(0.0f, 0.0f, 12.0f, 0.0f)
		[
			BuildMaterialColorControlsWidget()
		]
		+ SHorizontalBox::Slot()
		.FillWidth(0.36f)
		.Padding(0.0f, 0.0f, 12.0f, 0.0f)
		[
			BuildMaterialScalarControlsWidget()
		]
		+ SHorizontalBox::Slot()
		.FillWidth(0.32f)
		[
			BuildMorphControlsWidget()
		];
}

TSharedRef<SWidget> SSDMutableEditorWidget::BuildPartDropdownMenuWidget()
{
	return SNew(SBorder)
		.Padding(8.0f)
		[
			SNew(SBox)
			.WidthOverride(520.0f)
			.MaxDesiredHeight(420.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					SAssignNew(PartSearchTextBox, SEditableTextBox)
					.HintText(LOCTEXT("PartSearchHint", "Search mesh options..."))
					.Text_Lambda([this]()
					{
						return FText::FromString(PartSearchFilter);
					})
					.OnTextChanged(this, &SSDMutableEditorWidget::OnPartSearchTextChanged)
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SAssignNew(PartListView, SListView<TSharedPtr<FSDMutablePartListItem>>)
					.ListItemsSource(&PartItems)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &SSDMutableEditorWidget::GeneratePartListRow)
					.OnSelectionChanged(this, &SSDMutableEditorWidget::OnPartListSelectionChanged)
				]
			]
		];
}

TSharedRef<ITableRow> SSDMutableEditorWidget::GeneratePartListRow(
	TSharedPtr<FSDMutablePartListItem> PartItem,
	const TSharedRef<STableViewBase>& OwnerTable) const
{
	return SNew(STableRow<TSharedPtr<FSDMutablePartListItem>>, OwnerTable)
		.Padding(4.0f)
		[
			GeneratePartComboWidget(PartItem)
		];
}

TSharedRef<SWidget> SSDMutableEditorWidget::BuildMaterialColorControlsWidget()
{
	const auto MakeColorButton = [this](const FText& Label, const FName ParameterName)
	{
		return SNew(SButton)
			.ContentPadding(4.0f)
			.OnClicked_Lambda([this, ParameterName]()
			{
				OpenMaterialColorPicker(ParameterName);
				return FReply::Handled();
			})
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 2.0f)
				[
					SNew(STextBlock)
					.Text(Label)
					.Justification(ETextJustify::Center)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.WidthOverride(90.0f)
					.HeightOverride(18.0f)
					[
						SNew(SColorBlock)
						.Color_Lambda([this, ParameterName]()
						{
							return GetMaterialColorValue(ParameterName);
						})
						.Size(FVector2D(90.0f, 18.0f))
						.AlphaDisplayMode(EColorBlockAlphaDisplayMode::Ignore)
					]
				]
			];
	};

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("MaterialColorControlsTitle", "Material Colors"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			MakeColorButton(LOCTEXT("MaterialSkinColor", "Skin Color"), SDMutableParameters::SkinColor)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			MakeColorButton(LOCTEXT("MaterialEyeColor", "Eye Color"), SDMutableParameters::EyeColor)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeColorButton(LOCTEXT("MaterialDirtColor", "Dirt Color"), SDMutableParameters::DirtColor)
		];
}

TSharedRef<SWidget> SSDMutableEditorWidget::BuildMaterialScalarControlsWidget()
{
	const auto MakeMaterialSlider = [this](const FText& Label, const FName ParameterName)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(76.0f)
				[
					SNew(STextBlock)
					.Text(Label)
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.Value_Lambda([this, ParameterName]()
				{
					return GetMaterialScalarValue(ParameterName);
				})
				.OnValueChanged_Lambda([this, ParameterName](const float Value)
				{
					SetMaterialScalarValue(ParameterName, Value);
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(58.0f)
				[
					SNew(SNumericEntryBox<float>)
					.MinValue(0.0f)
					.MaxValue(1.0f)
					.MinSliderValue(0.0f)
					.MaxSliderValue(1.0f)
					.AllowSpin(false)
					.Value_Lambda([this, ParameterName]() -> TOptional<float>
					{
						return GetMaterialScalarValue(ParameterName);
					})
					.OnValueCommitted_Lambda([this, ParameterName](const float Value, ETextCommit::Type)
					{
						SetMaterialScalarValue(ParameterName, Value);
					})
				]
			];
	};

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("MaterialScalarControlsTitle", "Material Sliders"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 3.0f)
		[
			MakeMaterialSlider(LOCTEXT("MaterialCutWeight", "Cut"), SDMutableParameters::CutWeight)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 3.0f)
		[
			MakeMaterialSlider(LOCTEXT("MaterialDirtWeight", "Dirt"), SDMutableParameters::DirtWeight)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeMaterialSlider(LOCTEXT("MaterialDarkWeight", "Dark"), SDMutableParameters::DarkWeight)
		];
}

float SSDMutableEditorWidget::GetBodyShapeValue(const FName ParameterName) const
{
	const USDMutableComponent* Component = TargetComponent.Get();
	const FSDMutableBodyShape& BodyShape = Component ? Component->Recipe.BodyShape : EditorRecipe.BodyShape;
	if (ParameterName == SDMutableParameters::MaleFemale)
	{
		return BodyShape.MaleFemale;
	}
	if (ParameterName == SDMutableParameters::Heavy)
	{
		return BodyShape.Heavy;
	}
	if (ParameterName == SDMutableParameters::Buff)
	{
		return BodyShape.Buff;
	}
	if (ParameterName == SDMutableParameters::Skinny)
	{
		return BodyShape.Skinny;
	}

	return 0.0f;
}

void SSDMutableEditorWidget::SetBodyShapeValue(const FName ParameterName, const float Value)
{
	USDMutableComponent* Component = TargetComponent.Get();
	if (!Component && !TargetCustomizableObjectInstance.IsValid())
	{
		TargetStatus = TEXT("Cannot edit morph slider: no selected actor/component or COI target.");
		return;
	}

	FSDMutableBodyShape BodyShape = Component ? Component->Recipe.BodyShape : EditorRecipe.BodyShape;
	const float ClampedValue = FMath::Clamp(Value, 0.0f, 1.0f);
	if (ParameterName == SDMutableParameters::MaleFemale)
	{
		BodyShape.MaleFemale = ClampedValue;
	}
	else if (ParameterName == SDMutableParameters::Heavy)
	{
		BodyShape.Heavy = ClampedValue;
	}
	else if (ParameterName == SDMutableParameters::Buff)
	{
		BodyShape.Buff = ClampedValue;
	}
	else if (ParameterName == SDMutableParameters::Skinny)
	{
		BodyShape.Skinny = ClampedValue;
	}
	else
	{
		return;
	}

	if (Component)
	{
		Component->SetBodyShape(BodyShape, true);
		Component->UpdateMutableInstance(false, false);
		TargetStatus = FString::Printf(TEXT("Edited morph %s on %s."), *ParameterName.ToString(), *GetNameSafe(Component));
	}
	else
	{
		EditorRecipe.BodyShape = BodyShape;
		if (!CommitEditorStateToRecipeAsset(false))
		{
			ApplyEditorRecipeToTargetCoi();
		}
	}
}

float SSDMutableEditorWidget::GetMaterialScalarValue(const FName ParameterName) const
{
	const USDMutableComponent* Component = TargetComponent.Get();
	const FSDMutableMaterialSettings& Material = Component ? Component->Recipe.Material : EditorRecipe.Material;
	if (ParameterName == SDMutableParameters::CutWeight)
	{
		return Material.CutWeight;
	}
	if (ParameterName == SDMutableParameters::DirtWeight)
	{
		return Material.DirtWeight;
	}
	if (ParameterName == SDMutableParameters::DarkWeight)
	{
		return Material.DarkWeight;
	}

	return 0.0f;
}

void SSDMutableEditorWidget::SetMaterialScalarValue(const FName ParameterName, const float Value)
{
	USDMutableComponent* Component = TargetComponent.Get();
	if (!Component && !TargetCustomizableObjectInstance.IsValid())
	{
		TargetStatus = TEXT("Cannot edit material scalar: no selected actor/component or COI target.");
		return;
	}

	FSDMutableMaterialSettings Material = Component ? Component->Recipe.Material : EditorRecipe.Material;
	const float ClampedValue = FMath::Clamp(Value, 0.0f, 1.0f);
	if (ParameterName == SDMutableParameters::CutWeight)
	{
		Material.CutWeight = ClampedValue;
	}
	else if (ParameterName == SDMutableParameters::DirtWeight)
	{
		Material.DirtWeight = ClampedValue;
	}
	else if (ParameterName == SDMutableParameters::DarkWeight)
	{
		Material.DarkWeight = ClampedValue;
	}
	else
	{
		return;
	}

	if (Component)
	{
		Component->SetMaterialSettings(Material, true);
		Component->UpdateMutableInstance(false, false);
		TargetStatus = FString::Printf(TEXT("Edited material parameter %s on %s."), *ParameterName.ToString(), *GetNameSafe(Component));
	}
	else
	{
		EditorRecipe.Material = Material;
		if (!CommitEditorStateToRecipeAsset(false))
		{
			ApplyEditorRecipeToTargetCoi();
		}
	}
}

FLinearColor SSDMutableEditorWidget::GetMaterialColorValue(const FName ParameterName) const
{
	const USDMutableComponent* Component = TargetComponent.Get();
	const FSDMutableMaterialSettings& Material = Component ? Component->Recipe.Material : EditorRecipe.Material;
	if (ParameterName == SDMutableParameters::SkinColor)
	{
		return Material.SkinColor;
	}
	if (ParameterName == SDMutableParameters::EyeColor)
	{
		return Material.EyeColor;
	}
	if (ParameterName == SDMutableParameters::DirtColor)
	{
		return Material.DirtColor;
	}

	return FLinearColor::White;
}

void SSDMutableEditorWidget::OpenMaterialColorPicker(const FName ParameterName)
{
	FColorPickerArgs PickerArgs;
	PickerArgs.ParentWidget = SharedThis(this);
	PickerArgs.bUseAlpha = false;
	PickerArgs.bOnlyRefreshOnOk = true;
	PickerArgs.bClampValue = true;
	PickerArgs.InitialColor = GetMaterialColorValue(ParameterName);
	PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda([this, ParameterName](const FLinearColor NewColor)
	{
		USDMutableComponent* Component = TargetComponent.Get();
		if (!Component && !TargetCustomizableObjectInstance.IsValid())
		{
			TargetStatus = TEXT("Cannot edit material color: no selected actor/component or COI target.");
			return;
		}

		FSDMutableMaterialSettings Material = Component ? Component->Recipe.Material : EditorRecipe.Material;
		if (ParameterName == SDMutableParameters::SkinColor)
		{
			Material.SkinColor = NewColor;
		}
		else if (ParameterName == SDMutableParameters::EyeColor)
		{
			Material.EyeColor = NewColor;
		}
		else if (ParameterName == SDMutableParameters::DirtColor)
		{
			Material.DirtColor = NewColor;
		}
		else
		{
			return;
		}

		if (Component)
		{
			Component->SetMaterialSettings(Material, true);
			Component->UpdateMutableInstance(false, false);
			TargetStatus = FString::Printf(TEXT("Edited material color %s on %s."), *ParameterName.ToString(), *GetNameSafe(Component));
		}
		else
		{
			EditorRecipe.Material = Material;
			if (!CommitEditorStateToRecipeAsset(false))
			{
				ApplyEditorRecipeToTargetCoi();
			}
		}
	});

	OpenColorPicker(PickerArgs);
}

void SSDMutableEditorWidget::RebuildColorSwatchGrid()
{
	if (!ColorSwatchGrid.IsValid())
	{
		return;
	}

	EditorColorPalette.EnsureColorSlotCount();
	ColorSwatchGrid->ClearChildren();

	if (!SelectedPartItem.IsValid() || SelectedPartItem->Part.ColorProperties.IsEmpty())
	{
		ColorSwatchGrid->AddSlot(0, 0)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoPartColorProperties", "No color properties recorded for the selected mesh option. Rebuild pack catalogs to populate mesh color metadata."))
			.AutoWrapText(true)
		];
		return;
	}

	constexpr int32 SwatchesPerRow = 4;
	int32 VisibleColorIndex = 0;
	for (const FSDMutablePartColorProperty& ColorProperty : SelectedPartItem->Part.ColorProperties)
	{
		const int32 ColorSlotIndex = ColorProperty.PaletteIndex;
		if (ColorSlotIndex < 0 || ColorSlotIndex >= FSDMutableColorPalette::NumSidekicksColorSlots)
		{
			continue;
		}

		const int32 Row = VisibleColorIndex / SwatchesPerRow;
		const int32 Column = VisibleColorIndex % SwatchesPerRow;
		const FLinearColor SlotColor = EditorColorPalette.GetResolvedColorSlot(ColorSlotIndex);
		const bool bSelected = ColorSlotIndex == SelectedColorSlotIndex;
		const FText SlotDisplayName = ColorProperty.ColorName.IsNone() ? GetColorSlotDisplayName(ColorSlotIndex) : FText::FromName(ColorProperty.ColorName);
		++VisibleColorIndex;

		ColorSwatchGrid->AddSlot(Column, Row)
		[
			SNew(SButton)
			.ContentPadding(2.0f)
			.ToolTipText(FText::Format(
				LOCTEXT("ColorSwatchTooltip", "{0}\nSlot index: {1}\nClick to edit."),
				SlotDisplayName,
				FText::AsNumber(ColorSlotIndex)))
			.OnClicked_Lambda([this, ColorSlotIndex]()
			{
				SelectedColorSlotIndex = ColorSlotIndex;
				OpenColorPickerForSlot(ColorSlotIndex);
				RebuildColorSwatchGrid();
				return FReply::Handled();
			})
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 2.0f)
				[
					SNew(STextBlock)
					.Text(SlotDisplayName)
					.Justification(ETextJustify::Center)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.WidthOverride(120.0f)
					.HeightOverride(22.0f)
					[
						SNew(SColorBlock)
						.Color(SlotColor)
						.Size(FVector2D(120.0f, 22.0f))
						.AlphaDisplayMode(EColorBlockAlphaDisplayMode::Ignore)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(bSelected ? FText::FromString(TEXT(">")) : FText::FromString(TEXT(" ")))
					.Justification(ETextJustify::Center)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
				]
			]
		];
	}
}

void SSDMutableEditorWidget::SetPackFilterEnabled(TSharedPtr<FSDMutablePackListItem> PackItem, const bool bEnabled)
{
	if (!PackItem.IsValid())
	{
		return;
	}

	if (PackItem->bEnabled == bEnabled)
	{
		return;
	}

	PackItem->bEnabled = bEnabled;
	RebuildPartItems();
	RefreshPartSelectionWidgets();
}

void SSDMutableEditorWidget::SelectPack(TSharedPtr<FSDMutablePackListItem> PackItem)
{
	if (SelectedPackItem == PackItem)
	{
		return;
	}

	SelectedPackItem = PackItem;
	RebuildPartItems();
	RefreshPartSelectionWidgets();
}

void SSDMutableEditorWidget::SelectSlot(TSharedPtr<FSDMutableSlotListItem> SlotItem)
{
	if (SelectedSlotItem == SlotItem)
	{
		return;
	}

	SelectedSlotItem = SlotItem;
	RebuildPartItems();
	RebuildSlotListWidget();
	RefreshPartSelectionWidgets();
}

void SSDMutableEditorWidget::ClearSlot(TSharedPtr<FSDMutableSlotListItem> SlotItem)
{
	USDMutableComponent* Component = TargetComponent.Get();
	USDMutableCatalog* Catalog = LoadedCatalog.Get();
	if ((!Component && !TargetCustomizableObjectInstance.IsValid()) || !Catalog || !SlotItem.IsValid())
	{
		TargetStatus = TEXT("Cannot clear mesh type: missing target, catalog, or slot.");
		return;
	}

	SelectedSlotItem = SlotItem;
	if (Component)
	{
		if (!Component->Catalog)
		{
			Component->Catalog = Catalog;
		}

		Component->SetPart(SlotItem->Slot, NAME_None, true);
		Component->UpdateMutableInstance(false, false);
	}
	else
	{
		bool bFoundExistingSelection = false;
		for (FSDMutablePartSelection& Selection : EditorRecipe.Parts)
		{
			if (Selection.Slot == SlotItem->Slot)
			{
				Selection.OptionId = NAME_None;
				bFoundExistingSelection = true;
				break;
			}
		}
		if (!bFoundExistingSelection)
		{
			FSDMutablePartSelection& NewSelection = EditorRecipe.Parts.AddDefaulted_GetRef();
			NewSelection.Slot = SlotItem->Slot;
			NewSelection.OptionId = NAME_None;
		}
		if (!CommitEditorStateToRecipeAsset(false))
		{
			ApplyEditorRecipeToTargetCoi();
		}
	}

	TargetStatus = FString::Printf(
		TEXT("Cleared %s to None on %s."),
	*GetSlotDisplayName(SlotItem->Slot).ToString(),
	Component ? *GetNameSafe(Component) : *GetNameSafe(TargetCustomizableObjectInstance.Get()));

	RebuildPartItems();
	RebuildSlotListWidget();
	RefreshPartSelectionWidgets();
}

void SSDMutableEditorWidget::ClearAllSlots()
{
	USDMutableComponent* Component = TargetComponent.Get();
	USDMutableCatalog* Catalog = LoadedCatalog.Get();
	if ((!Component && !TargetCustomizableObjectInstance.IsValid()) || !Catalog || SlotItems.IsEmpty())
	{
		TargetStatus = TEXT("Cannot clear all mesh types: missing target, catalog, or slots.");
		return;
	}

	if (Component)
	{
		if (!Component->Catalog)
		{
			Component->Catalog = Catalog;
		}

		for (const TSharedPtr<FSDMutableSlotListItem>& SlotItem : SlotItems)
		{
			if (SlotItem.IsValid())
			{
				Component->SetPart(SlotItem->Slot, NAME_None, false);
			}
		}
		Component->UpdateMutableInstance(false, false);
	}
	else
	{
		for (const TSharedPtr<FSDMutableSlotListItem>& SlotItem : SlotItems)
		{
			if (!SlotItem.IsValid())
			{
				continue;
			}

			bool bFoundExistingSelection = false;
			for (FSDMutablePartSelection& Selection : EditorRecipe.Parts)
			{
				if (Selection.Slot == SlotItem->Slot)
				{
					Selection.OptionId = NAME_None;
					bFoundExistingSelection = true;
					break;
				}
			}
			if (!bFoundExistingSelection)
			{
				FSDMutablePartSelection& NewSelection = EditorRecipe.Parts.AddDefaulted_GetRef();
				NewSelection.Slot = SlotItem->Slot;
				NewSelection.OptionId = NAME_None;
			}
		}
		if (!CommitEditorStateToRecipeAsset(false))
		{
			ApplyEditorRecipeToTargetCoi();
		}
	}

	TargetStatus = FString::Printf(
		TEXT("Cleared all mesh types to None on %s."),
		Component ? *GetNameSafe(Component) : *GetNameSafe(TargetCustomizableObjectInstance.Get()));

	RebuildPartItems();
	RebuildSlotListWidget();
	RefreshPartSelectionWidgets();
}

bool SSDMutableEditorWidget::CanSelectAdjacentPartOption(const int32 Direction) const
{
	if (Direction == 0 || PartItems.Num() <= 1)
	{
		return false;
	}

	const int32 CurrentIndex = PartItems.IndexOfByKey(SelectedPartItem);
	if (CurrentIndex == INDEX_NONE)
	{
		return !PartItems.IsEmpty();
	}

	const int32 TargetIndex = CurrentIndex + Direction;
	return PartItems.IsValidIndex(TargetIndex);
}

void SSDMutableEditorWidget::SelectAdjacentPartOption(const int32 Direction)
{
	if (Direction == 0 || PartItems.IsEmpty())
	{
		return;
	}

	int32 CurrentIndex = PartItems.IndexOfByKey(SelectedPartItem);
	if (CurrentIndex == INDEX_NONE)
	{
		CurrentIndex = Direction > 0 ? -1 : PartItems.Num();
	}

	const int32 TargetIndex = CurrentIndex + Direction;
	if (!PartItems.IsValidIndex(TargetIndex))
	{
		return;
	}

	ApplyPart(PartItems[TargetIndex]);
}

void SSDMutableEditorWidget::OnPartComboSelectionChanged(TSharedPtr<FSDMutablePartListItem> PartItem, ESelectInfo::Type SelectInfo)
{
	SelectedPartItem = PartItem;
	if (SelectInfo != ESelectInfo::Direct)
	{
		ApplyPart(PartItem);
	}
	else
	{
		RebuildPartListWidget();
		RebuildColorSwatchGrid();
	}
}

void SSDMutableEditorWidget::OnPartListSelectionChanged(TSharedPtr<FSDMutablePartListItem> PartItem, ESelectInfo::Type SelectInfo)
{
	if (!PartItem.IsValid())
	{
		return;
	}

	SelectedPartItem = PartItem;
	if (SelectInfo != ESelectInfo::Direct)
	{
		ApplyPart(PartItem);
		if (PartMenuAnchor.IsValid())
		{
			PartMenuAnchor->SetIsOpen(false);
		}
	}
	else
	{
		RebuildPartListWidget();
		RebuildColorSwatchGrid();
	}
}

void SSDMutableEditorWidget::OpenColorPickerForSlot(const int32 ColorSlotIndex)
{
	if (ColorSlotIndex < 0 || ColorSlotIndex >= FSDMutableColorPalette::NumSidekicksColorSlots)
	{
		return;
	}

	FColorPickerArgs PickerArgs;
	PickerArgs.ParentWidget = SharedThis(this);
	PickerArgs.bUseAlpha = false;
	PickerArgs.bOnlyRefreshOnOk = true;
	PickerArgs.bClampValue = true;
	PickerArgs.InitialColor = EditorColorPalette.GetResolvedColorSlot(ColorSlotIndex);
	PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda([this, ColorSlotIndex](const FLinearColor NewColor)
	{
		SetColorSlot(ColorSlotIndex, NewColor, true);
	});

	OpenColorPicker(PickerArgs);
}

void SSDMutableEditorWidget::SetColorSlot(const int32 ColorSlotIndex, const FLinearColor Color, const bool bApplyToTarget)
{
	if (!EditorColorPalette.SetColorSlot(ColorSlotIndex, Color, true))
	{
		ColorStatus = FString::Printf(TEXT("Invalid color slot index: %d"), ColorSlotIndex);
		return;
	}

	SelectedColorSlotIndex = ColorSlotIndex;
	ColorStatus = FString::Printf(TEXT("Edited color slot %d."), ColorSlotIndex);
	RebuildColorSwatchGrid();

	if (bApplyToTarget)
	{
		ApplyColorPaletteToTarget();
	}
}

void SSDMutableEditorWidget::ApplyColorPaletteToTarget()
{
	USDMutableComponent* Component = TargetComponent.Get();
	if (!Component && !TargetCustomizableObjectInstance.IsValid())
	{
		ColorStatus = TEXT("Cannot apply color palette: no selected actor/component or COI target.");
		return;
	}

	EditorColorPalette.EnsureColorSlotCount();
	if (TargetRecipeAsset.IsValid())
	{
		if (CommitEditorStateToRecipeAsset(true))
		{
			ColorStatus = FString::Printf(
				TEXT("Updated authoritative recipe DA palette and pushed texture to %s."),
				*GetNameSafe(TargetCustomizableObjectInstance.Get()));
		}
		return;
	}

	if (Component)
	{
		// Actor targets persist palette state on the component; the component builds/applies its own transient texture.
		Component->SetColorPalette(EditorColorPalette, true);
		Component->UpdateMutableInstance(false, false);

		ColorStatus = FString::Printf(
			TEXT("Applied transient color texture from 256 palette slots to %s."),
			*GetNameSafe(Component));
		return;
	}

	UObject* TextureOuter = TargetCustomizableObjectInstance.Get();
	if (!TextureOuter)
	{
		TextureOuter = GetTransientPackage();
	}

	// Direct COI targets do not have component-local palette state, so Slate owns the transient preview texture.
	PreviewColorTexture.Reset(USDMutableColorPreset::BuildTransientColorTextureFromPalette(EditorColorPalette, TextureOuter));
	if (!PreviewColorTexture.IsValid())
	{
		ColorStatus = TEXT("Failed to build transient color texture from editor palette.");
		return;
	}

	EditorRecipe.Material.BaseColor = TSoftObjectPtr<UTexture>(PreviewColorTexture.Get());
	ApplyEditorRecipeToTargetCoi();

	ColorStatus = FString::Printf(
		TEXT("Applied transient color texture from 256 palette slots to %s."),
		*GetNameSafe(TargetCustomizableObjectInstance.Get()));
}

bool SSDMutableEditorWidget::IsTargetCoiReadyForRecipeApply(UCustomizableObjectInstance& Coi)
{
	UCustomizableObject* CustomizableObject = Coi.GetCustomizableObject();
	if (!CustomizableObject)
	{
		TargetStatus = FString::Printf(
			TEXT("Cannot apply editor recipe: COI %s has no Customizable Object."),
			*GetNameSafe(&Coi));
		return false;
	}

	if (CustomizableObject->IsCompiled())
	{
		return true;
	}

	ScheduleDeferredCoiRecipeApply(Coi);

	if (CustomizableObject->IsLoading())
	{
		TargetStatus = FString::Printf(
			TEXT("Deferred recipe apply: Customizable Object %s is still loading."),
			*GetNameSafe(CustomizableObject));
		return false;
	}

	if (!bDeferredCoiCompileRequested)
	{
		// Cold COIs need their Customizable Object compiled before parameter writes are reliable.
		FCompileParams CompileParams;
		CompileParams.bSkipIfCompiled = true;
		CompileParams.bSkipIfNotOutOfDate = false;
		CompileParams.bAsync = true;
		CompileParams.CompileOnlySelectedInstance = &Coi;
		CustomizableObject->Compile(CompileParams);
		bDeferredCoiCompileRequested = true;
	}

	TargetStatus = FString::Printf(
		TEXT("Deferred recipe apply: compiling Customizable Object %s before writing COI parameters."),
		*GetNameSafe(CustomizableObject));
	return false;
}

void SSDMutableEditorWidget::ScheduleDeferredCoiRecipeApply(UCustomizableObjectInstance& Coi)
{
	DeferredCoiRecipeApplyTarget = &Coi;

	if (DeferredCoiRecipeApplyTickerHandle.IsValid())
	{
		return;
	}

	DeferredCoiRecipeApplyTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateSP(this, &SSDMutableEditorWidget::TickDeferredCoiRecipeApply),
		0.25f);
}

void SSDMutableEditorWidget::CancelDeferredCoiRecipeApply()
{
	if (DeferredCoiRecipeApplyTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DeferredCoiRecipeApplyTickerHandle);
		DeferredCoiRecipeApplyTickerHandle.Reset();
	}

	DeferredCoiRecipeApplyTarget.Reset();
	bDeferredCoiCompileRequested = false;
}

bool SSDMutableEditorWidget::TickDeferredCoiRecipeApply(float DeltaTime)
{
	UCustomizableObjectInstance* DeferredCoi = DeferredCoiRecipeApplyTarget.Get();
	if (!DeferredCoi || DeferredCoi != TargetCustomizableObjectInstance.Get())
	{
		DeferredCoiRecipeApplyTickerHandle.Reset();
		DeferredCoiRecipeApplyTarget.Reset();
		bDeferredCoiCompileRequested = false;
		return false;
	}

	UCustomizableObject* CustomizableObject = DeferredCoi->GetCustomizableObject();
	if (!CustomizableObject)
	{
		DeferredCoiRecipeApplyTickerHandle.Reset();
		DeferredCoiRecipeApplyTarget.Reset();
		bDeferredCoiCompileRequested = false;
		return false;
	}

	if (!CustomizableObject->IsCompiled())
	{
		return true;
	}

	DeferredCoiRecipeApplyTickerHandle.Reset();
	DeferredCoiRecipeApplyTarget.Reset();
	bDeferredCoiCompileRequested = false;
	ApplyEditorRecipeToTargetCoi();
	return false;
}

bool SSDMutableEditorWidget::ApplyEditorRecipeToTargetCoi()
{
	UCustomizableObjectInstance* Coi = TargetCustomizableObjectInstance.Get();
	USDMutableCatalog* Catalog = LoadedCatalog.Get();
	if (!Coi)
	{
		TargetStatus = TEXT("Cannot apply editor recipe: no selected COI target.");
		return false;
	}

	if (!Catalog)
	{
		TargetStatus = TEXT("Cannot apply editor recipe: no loaded root catalog.");
		return false;
	}

	if (!IsTargetCoiReadyForRecipeApply(*Coi))
	{
		return false;
	}

	ApplyRecipeToCustomizableObjectInstance(*Coi, Catalog, EditorRecipe);
	Coi->UpdateSkeletalMeshAsync(false, false);
	TargetStatus = FString::Printf(TEXT("Applied editor-local Sidekicks recipe to COI %s."), *GetNameSafe(Coi));
	return true;
}

bool SSDMutableEditorWidget::CommitEditorStateToRecipeAsset(const bool bUpdateColorTexture)
{
	USDMutableSidekickRecipeAsset* RecipeAsset = TargetRecipeAsset.Get();
	if (!RecipeAsset)
	{
		return false;
	}

	RecipeAsset->Modify();

	if (bUpdateColorTexture)
	{
		// The DA palette is source of truth; the texture is a generated companion refreshed during save/apply.
		if (UTexture2D* ColorTexture = RecipeAsset->ColorTexture.LoadSynchronous())
		{
			if (USDMutableTextureBuilderLibrary::WriteSidekicksColorTextureFromPalette(ColorTexture, EditorColorPalette))
			{
				EditorRecipe.Material.BaseColor = TSoftObjectPtr<UTexture>(ColorTexture);
				ColorTexture->MarkPackageDirty();
			}
		}
	}

	RecipeAsset->Recipe = EditorRecipe;
	RecipeAsset->ColorPalette = EditorColorPalette;
	if (UTexture2D* ColorTexture = Cast<UTexture2D>(EditorRecipe.Material.BaseColor.LoadSynchronous()))
	{
		RecipeAsset->ColorTexture = TSoftObjectPtr<UTexture2D>(ColorTexture);
	}
	if (UCustomizableObjectInstance* Coi = TargetCustomizableObjectInstance.Get())
	{
		RecipeAsset->CustomizableObjectInstance = TSoftObjectPtr<UCustomizableObjectInstance>(Coi);
	}
	RecipeAsset->MarkPackageDirty();

	const bool bApplied = ApplyEditorRecipeToTargetCoi();
	TargetStatus = FString::Printf(
		TEXT("Updated authoritative recipe DA %s and pushed to COI %s."),
		*GetNameSafe(RecipeAsset),
		*GetNameSafe(TargetCustomizableObjectInstance.Get()));
	return bApplied;
}

void SSDMutableEditorWidget::GenerateRandomColorPalette()
{
	EditorColorPalette.GenerateRandomColorPalette();
	RebuildColorSwatchGrid();
	ApplyColorPaletteToTarget();
}

void SSDMutableEditorWidget::ClearColorPalette()
{
	EditorColorPalette.ClearColorPaletteOverrides();
	RebuildColorSwatchGrid();
	ApplyColorPaletteToTarget();
}

FSDMutableRecipeJsonExchange SSDMutableEditorWidget::MakeCurrentRecipeJsonExchange() const
{
	const USDMutableComponent* Component = TargetComponent.Get();
	const USDMutableSidekickRecipeAsset* RecipeAsset = TargetRecipeAsset.Get();
	const UCustomizableObjectInstance* Coi = Component ? Component->CustomizableObjectInstance.Get() : TargetCustomizableObjectInstance.Get();
	const AActor* Owner = Component ? Component->GetOwner() : nullptr;

	FSDMutableRecipeJsonExchange Exchange;
	Exchange.ExchangeVersion = 1;
	Exchange.SourceAssetName = RecipeAsset
		? RecipeAsset->GetName()
		: MakeSafeAssetName(Owner ? Owner->GetActorNameOrLabel() : (Coi ? Coi->GetName() : TEXT("SidekickPreset")));
	Exchange.Recipe = Component ? Component->Recipe : EditorRecipe;
	Exchange.ColorPalette = Component ? Component->ColorPalette : EditorColorPalette;
	Exchange.ColorPalette.EnsureColorSlotCount();

	if (const UTexture* BaseColor = Exchange.Recipe.Material.BaseColor.LoadSynchronous())
	{
		if (!BaseColor->IsIn(GetTransientPackage()))
		{
			Exchange.ColorTexturePath = BaseColor->GetPathName();
		}
	}
	if (Coi && !Coi->IsIn(GetTransientPackage()))
	{
		Exchange.CustomizableObjectInstancePath = Coi->GetPathName();
	}

	return Exchange;
}

void SSDMutableEditorWidget::ApplyRecipeJsonExchangeToCurrentTarget(const FSDMutableRecipeJsonExchange& Exchange)
{
	EditorRecipe = Exchange.Recipe;
	EditorColorPalette = Exchange.ColorPalette;
	EditorColorPalette.EnsureColorSlotCount();

	USDMutableComponent* Component = TargetComponent.Get();
	if (Component)
	{
		EditorRecipe.Material.BaseColor.Reset();
	}
	else
	{
		UObject* TextureOuter = TargetCustomizableObjectInstance.Get();
		if (!TextureOuter)
		{
			TextureOuter = GetTransientPackage();
		}

		PreviewColorTexture.Reset(USDMutableColorPreset::BuildTransientColorTextureFromPalette(EditorColorPalette, TextureOuter));
		if (PreviewColorTexture.IsValid())
		{
			EditorRecipe.Material.BaseColor = TSoftObjectPtr<UTexture>(PreviewColorTexture.Get());
		}
		else if (!Exchange.ColorTexturePath.IsEmpty())
		{
			EditorRecipe.Material.BaseColor = TSoftObjectPtr<UTexture>(FSoftObjectPath(Exchange.ColorTexturePath));
		}
	}

	if (TargetRecipeAsset.IsValid())
	{
		CommitEditorStateToRecipeAsset(true);
	}
	else if (Component)
	{
		Component->SetRecipe(EditorRecipe, false);
		Component->SetColorPalette(EditorColorPalette, false);
		Component->ApplyRecipeFromMutableDefaultsAndUpdate(false, false);
	}
	else if (TargetCustomizableObjectInstance.IsValid())
	{
		ApplyEditorRecipeToTargetCoi();
	}

	RebuildPartItems();
	RebuildSelectionPanels();
	RebuildColorSwatchGrid();
}

void SSDMutableEditorWidget::ExportCurrentRecipeJson()
{
	const FSDMutableRecipeJsonExchange Exchange = MakeCurrentRecipeJsonExchange();
	const FString DefaultFileName = MakeSafeAssetName(Exchange.SourceAssetName) + TEXT(".json");
	const FString OutputPath = PromptForRecipeJsonSavePath(DefaultFileName);
	if (OutputPath.IsEmpty())
	{
		TargetStatus = TEXT("Export JSON cancelled.");
		return;
	}

	FString Json;
	if (!FJsonObjectConverter::UStructToJsonObjectString(
		FSDMutableRecipeJsonExchange::StaticStruct(),
		&Exchange,
		Json,
		0,
		0,
		0,
		nullptr,
		true))
	{
		TargetStatus = TEXT("Failed to serialize Sidekicks recipe JSON.");
		return;
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
	if (!FFileHelper::SaveStringToFile(Json, *OutputPath))
	{
		TargetStatus = FString::Printf(TEXT("Failed to write Sidekicks recipe JSON: %s"), *OutputPath);
		return;
	}

	TargetStatus = FString::Printf(TEXT("Exported Sidekicks recipe JSON: %s"), *OutputPath);
}

void SSDMutableEditorWidget::ImportCurrentRecipeJson()
{
	const FString InputPath = PromptForRecipeJsonOpenPath();
	if (InputPath.IsEmpty())
	{
		TargetStatus = TEXT("Import JSON cancelled.");
		return;
	}

	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *InputPath))
	{
		TargetStatus = FString::Printf(TEXT("Failed to read Sidekicks recipe JSON: %s"), *InputPath);
		return;
	}

	FSDMutableRecipeJsonExchange Exchange;
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Exchange, 0, 0))
	{
		TargetStatus = FString::Printf(TEXT("Failed to parse Sidekicks recipe JSON: %s"), *InputPath);
		return;
	}

	ApplyRecipeJsonExchangeToCurrentTarget(Exchange);
	TargetStatus = FString::Printf(TEXT("Imported Sidekicks recipe JSON onto current target: %s"), *InputPath);
}

void SSDMutableEditorWidget::SaveCurrentRecipe()
{
	USDMutableSidekickRecipeAsset* RecipeAsset = TargetRecipeAsset.Get();
	if (!RecipeAsset)
	{
		TargetStatus = TEXT("Cannot save: no recipe DataAsset is associated. Use Save Recipe Asset As first.");
		return;
	}

	CommitEditorStateToRecipeAsset(true);

	bool bSavedTexture = true;
	if (UTexture2D* ColorTexture = RecipeAsset->ColorTexture.LoadSynchronous())
	{
		bSavedTexture = SaveAssetPackage(*ColorTexture);
	}

	bool bSavedCoi = true;
	if (UCustomizableObjectInstance* Coi = RecipeAsset->CustomizableObjectInstance.LoadSynchronous())
	{
		bSavedCoi = SaveAssetPackage(*Coi);
	}

	const bool bSavedRecipe = SaveAssetPackage(*RecipeAsset);
	TargetStatus = FString::Printf(
		TEXT("%s recipe DA %s, texture %s, and COI %s."),
		(bSavedRecipe && bSavedTexture && bSavedCoi) ? TEXT("Saved") : TEXT("Attempted to save"),
		*RecipeAsset->GetPathName(),
		*GetNameSafe(RecipeAsset->ColorTexture.LoadSynchronous()),
		*GetNameSafe(RecipeAsset->CustomizableObjectInstance.LoadSynchronous()));
}

void SSDMutableEditorWidget::SaveCurrentRecipeAsset()
{
	USDMutableComponent* Component = TargetComponent.Get();
	UCustomizableObjectInstance* SourceCoi = Component ? Component->CustomizableObjectInstance.Get() : TargetCustomizableObjectInstance.Get();
	if (!Component && !SourceCoi)
	{
		TargetStatus = TEXT("Cannot save recipe: no selected actor/component or COI target.");
		return;
	}

	if (!SourceCoi)
	{
		TargetStatus = TEXT("Cannot save recipe: selected target has no CustomizableObjectInstance to duplicate.");
		return;
	}

	EditorColorPalette.EnsureColorSlotCount();
	FSDMutableColorPalette SavedColorPalette = Component ? Component->ColorPalette : EditorColorPalette;
	SavedColorPalette.EnsureColorSlotCount();

	const AActor* Owner = Component ? Component->GetOwner() : nullptr;
	const FString SourceName = MakeSafeAssetName(Owner ? Owner->GetActorNameOrLabel() : SourceCoi->GetName());
	const FString DefaultAssetName = FString::Printf(TEXT("DA_SDRecipe_%s"), *SourceName);
	const FString SaveObjectPath = PromptForRecipeSaveObjectPath(DefaultAssetName);
	if (SaveObjectPath.IsEmpty())
	{
		TargetStatus = TEXT("Save recipe cancelled.");
		return;
	}

	const FString RecipePackagePath = FPackageName::ObjectPathToPackageName(SaveObjectPath);
	const FString TargetPackagePath = FPaths::GetPath(RecipePackagePath);
	FString BaseAssetName = FPaths::GetBaseFilename(RecipePackagePath);
	BaseAssetName.RemoveFromStart(TEXT("DA_"));
	BaseAssetName.RemoveFromStart(TEXT("COI_"));
	BaseAssetName.RemoveFromStart(TEXT("T_"));

	const FString RecipeAssetName = FString::Printf(TEXT("DA_%s"), *BaseAssetName);
	const FString TextureAssetName = FString::Printf(TEXT("T_%s"), *BaseAssetName);
	const FString CoiAssetName = FString::Printf(TEXT("COI_%s"), *BaseAssetName);
	const FString ActualRecipePackagePath = MakePackageName(TargetPackagePath, RecipeAssetName);
	const FString CoiPackagePath = MakePackageName(TargetPackagePath, CoiAssetName);

	USDMutableColorPreset* TemporaryColorPreset = NewObject<USDMutableColorPreset>(GetTransientPackage());
	TemporaryColorPreset->Palette = SavedColorPalette;
	UTexture2D* SavedTexture = USDMutableTextureBuilderLibrary::BuildSidekicksColorTextureAsset(
		TemporaryColorPreset,
		TargetPackagePath,
		TextureAssetName,
		true);

	FSDMutableSidekickRecipe SavedRecipe = Component ? Component->Recipe : EditorRecipe;
	if (SavedTexture)
	{
		SavedRecipe.Material.BaseColor = TSoftObjectPtr<UTexture>(SavedTexture);
	}

	UPackage* CoiPackage = CreatePackage(*CoiPackagePath);
	if (!CoiPackage)
	{
		TargetStatus = FString::Printf(TEXT("Cannot save recipe: failed to create COI package %s."), *CoiPackagePath);
		return;
	}

	UCustomizableObjectInstance* SavedCoi = Cast<UCustomizableObjectInstance>(FindObjectInPackage(CoiPackagePath, CoiAssetName, UCustomizableObjectInstance::StaticClass()));
	const bool bCreatedCoi = SavedCoi == nullptr;
	if (!SavedCoi)
	{
		SavedCoi = DuplicateObject<UCustomizableObjectInstance>(SourceCoi, CoiPackage, *CoiAssetName);
		if (SavedCoi)
		{
			SavedCoi->SetFlags(RF_Public | RF_Standalone);
		}
	}

	if (!SavedCoi)
	{
		TargetStatus = FString::Printf(TEXT("Cannot save recipe: failed to create COI asset %s."), *CoiAssetName);
		return;
	}

	SavedCoi->Modify();
	const USDMutableCatalog* TargetCatalog = Component ? Component->Catalog.Get() : LoadedCatalog.Get();
	ApplyRecipeToCustomizableObjectInstance(*SavedCoi, TargetCatalog, SavedRecipe);
	if (bCreatedCoi)
	{
		FAssetRegistryModule::AssetCreated(SavedCoi);
	}
	CoiPackage->MarkPackageDirty();

	UPackage* RecipePackage = CreatePackage(*ActualRecipePackagePath);
	if (!RecipePackage)
	{
		TargetStatus = FString::Printf(TEXT("Cannot save recipe: failed to create package %s."), *ActualRecipePackagePath);
		return;
	}

	USDMutableSidekickRecipeAsset* RecipeAsset = FindObject<USDMutableSidekickRecipeAsset>(RecipePackage, *RecipeAssetName);
	const bool bCreatedRecipeAsset = RecipeAsset == nullptr;
	if (!RecipeAsset)
	{
		RecipeAsset = NewObject<USDMutableSidekickRecipeAsset>(RecipePackage, *RecipeAssetName, RF_Public | RF_Standalone);
	}

	if (!RecipeAsset)
	{
		TargetStatus = FString::Printf(TEXT("Cannot save recipe: failed to create asset %s."), *RecipeAssetName);
		return;
	}

	RecipeAsset->Modify();
	RecipeAsset->Recipe = SavedRecipe;
	RecipeAsset->ColorPalette = SavedColorPalette;
	RecipeAsset->ColorTexture = TSoftObjectPtr<UTexture2D>(SavedTexture);
	RecipeAsset->CustomizableObjectInstance = TSoftObjectPtr<UCustomizableObjectInstance>(SavedCoi);

	if (bCreatedRecipeAsset)
	{
		FAssetRegistryModule::AssetCreated(RecipeAsset);
	}

	RecipePackage->MarkPackageDirty();
	const bool bSavedCoi = SaveAssetPackage(*SavedCoi);
	const bool bSavedRecipe = SaveAssetPackage(*RecipeAsset);
	if (Component)
	{
		SetActorCustomizableObjectInstance(Component->GetOwner(), SavedCoi);
		Component->SetCustomizableObjectInstance(SavedCoi);
		Component->SourceRecipeAsset = TSoftObjectPtr<USDMutableSidekickRecipeAsset>(RecipeAsset);
		Component->TemplateCustomizableObjectInstance = TSoftObjectPtr<UCustomizableObjectInstance>(SavedCoi);
		Component->SetColorPalette(SavedColorPalette, false);
		Component->SetRecipe(SavedRecipe, true);
		Component->UpdateMutableInstance(false, false);
	}
	else
	{
		TargetRecipeAsset = RecipeAsset;
		TargetCustomizableObjectInstance = SavedCoi;
		EditorRecipe = SavedRecipe;
		EditorColorPalette.EnsureColorSlotCount();
		ApplyEditorRecipeToTargetCoi();
	}
	RebuildSelectionPanels();
	TargetStatus = FString::Printf(
		TEXT("%s recipe asset %s, COI %s, and texture %s."),
		(bSavedRecipe && bSavedCoi && SavedTexture) ? TEXT("Saved") : TEXT("Attempted to save"),
		*ActualRecipePackagePath,
		*CoiPackagePath,
		SavedTexture ? *SavedTexture->GetPathName() : TEXT("None"));
}

void SSDMutableEditorWidget::ApplyPart(TSharedPtr<FSDMutablePartListItem> PartItem)
{
	USDMutableComponent* Component = TargetComponent.Get();
	USDMutableCatalog* Catalog = LoadedCatalog.Get();
	if ((!Component && !TargetCustomizableObjectInstance.IsValid()) || !Catalog || !SelectedSlotItem.IsValid() || !PartItem.IsValid())
	{
		TargetStatus = TEXT("Cannot apply part: missing target, catalog, slot, or part.");
		return;
	}

	SelectedPartItem = PartItem;
	if (Component)
	{
		// Component writes go through USDMutableComponent so actor-local editor/runtime COI ownership is preserved.
		if (!Component->Catalog)
		{
			Component->Catalog = Catalog;
		}

		Component->SetPart(SelectedSlotItem->Slot, PartItem->Part.PartId, true);
		Component->UpdateMutableInstance(false, false);
	}
	else
	{
		bool bFoundExistingSelection = false;
		for (FSDMutablePartSelection& Selection : EditorRecipe.Parts)
		{
			if (Selection.Slot == SelectedSlotItem->Slot)
			{
				Selection.OptionId = PartItem->Part.PartId;
				bFoundExistingSelection = true;
				break;
			}
		}
		if (!bFoundExistingSelection)
		{
			FSDMutablePartSelection& NewSelection = EditorRecipe.Parts.AddDefaulted_GetRef();
			NewSelection.Slot = SelectedSlotItem->Slot;
			NewSelection.OptionId = PartItem->Part.PartId;
		}
		if (!CommitEditorStateToRecipeAsset(false))
		{
			ApplyEditorRecipeToTargetCoi();
		}
	}

	TargetStatus = FString::Printf(
		TEXT("Applied %s to %s on %s."),
		PartItem->Part.PartId.IsNone() ? TEXT("None") : *PartItem->Part.PartId.ToString(),
		*GetSlotDisplayName(SelectedSlotItem->Slot).ToString(),
	Component ? *GetNameSafe(Component) : *GetNameSafe(TargetCustomizableObjectInstance.Get()));

	RebuildSlotListWidget();
	RefreshPartSelectionWidgets();
}

FName SSDMutableEditorWidget::GetSelectedOptionId(const ESDMutablePartSlot Slot) const
{
	const USDMutableComponent* Component = TargetComponent.Get();
	const TArray<FSDMutablePartSelection>& Selections = Component ? Component->Recipe.Parts : EditorRecipe.Parts;
	if (!Component && !TargetCustomizableObjectInstance.IsValid())
	{
		return NAME_None;
	}

	for (const FSDMutablePartSelection& Selection : Selections)
	{
		if (Selection.Slot == Slot)
		{
			return Selection.OptionId;
		}
	}

	return NAME_None;
}

bool SSDMutableEditorWidget::IsPartSelected(const FSDMutableCatalogPartEntry& Part) const
{
	if (!SelectedSlotItem.IsValid())
	{
		return false;
	}

	return GetSelectedOptionId(SelectedSlotItem->Slot) == Part.PartId;
}

FText SSDMutableEditorWidget::GetColorSlotDisplayName(const int32 ColorSlotIndex) const
{
	return FSDMutableHelpers::GetColorSlotDisplayName(ColorSlotIndex);
}

FText SSDMutableEditorWidget::GetSelectedPartText() const
{
	if (!SelectedPartItem.IsValid())
	{
		return LOCTEXT("NoPartComboSelection", "Select Mesh Option");
	}

	const FText PartName = GetPartDisplayName(SelectedPartItem->Part);
	if (SelectedPartItem->Part.PackId.IsNone())
	{
		return PartName;
	}

	return FText::Format(
		LOCTEXT("SelectedPartComboText", "{0} ({1})"),
		PartName,
		FText::FromName(SelectedPartItem->Part.PackId));
}

void SSDMutableEditorWidget::OnPartSearchTextChanged(const FText& NewText)
{
	PartSearchFilter = NewText.ToString();
	ApplyPartSearchFilter();

	if (PartListView.IsValid())
	{
		PartListView->RequestListRefresh();
		if (PartItems.Contains(SelectedPartItem))
		{
			PartListView->SetSelection(SelectedPartItem, ESelectInfo::Direct);
		}
		else
		{
			PartListView->ClearSelection();
		}
	}
}

FText SSDMutableEditorWidget::GetCatalogStatusText() const
{
	return FText::FromString(CatalogStatus);
}

FText SSDMutableEditorWidget::GetCatalogSummaryText() const
{
	return FText::Format(
		LOCTEXT("CatalogSummary", "Packs: {0} species, {1} outfit, {2} shared, {3} unknown\nLoaded pack DataAssets: {4}\nIndexed part entries: {5}"),
		FText::AsNumber(SpeciesPackCount),
		FText::AsNumber(OutfitPackCount),
		FText::AsNumber(SharedPackCount),
		FText::AsNumber(UnknownPackCount),
		FText::AsNumber(LoadedPackCount),
		FText::AsNumber(LoadedPartCount));
}

FText SSDMutableEditorWidget::GetTargetStatusText() const
{
	return FText::FromString(TargetStatus);
}

FText SSDMutableEditorWidget::GetColorStatusText() const
{
	return FText::Format(
		LOCTEXT("ColorStatusFormat", "{0}\nSelected color slot: {1}"),
		FText::FromString(ColorStatus),
		FText::AsNumber(SelectedColorSlotIndex));
}

TSharedRef<SWidget> SSDMutableEditorWidget::GeneratePartComboWidget(TSharedPtr<FSDMutablePartListItem> PartItem) const
{
	if (!PartItem.IsValid())
	{
		return SNew(STextBlock).Text(LOCTEXT("InvalidPartComboItem", "Invalid"));
	}

	const FText PartName = GetPartDisplayName(PartItem->Part);
	const FText PackName = PartItem->Part.PackId.IsNone() ? FText::GetEmpty() : FText::Format(LOCTEXT("PartComboPackFormat", "Pack: {0}"), FText::FromName(PartItem->Part.PackId));
	const FText ColorCount = FText::Format(LOCTEXT("PartComboColorCount", "Colors: {0}"), FText::AsNumber(PartItem->Part.ColorProperties.Num()));

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(40.0f)
			.HeightOverride(40.0f)
			[
				MakeThumbnailWidget(PartItem->ThumbnailBrush)
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(PartName)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(PackName)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(ColorCount)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]
		];
}

#undef LOCTEXT_NAMESPACE
