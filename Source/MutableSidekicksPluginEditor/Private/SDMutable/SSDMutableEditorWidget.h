#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "SDMutable/SDMutableCatalog.h"
#include "SDMutable/SDMutableColorPreset.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class USDMutableCatalog;
class USDMutableComponent;
class USDMutableSidekickRecipeAsset;
class UCustomizableObjectInstance;
class UTexture2D;
struct FSlateBrush;
class SHorizontalBox;
class SEditableTextBox;
class SListViewBase;
class SMenuAnchor;
class SUniformGridPanel;
class SVerticalBox;
struct FSDMutableRecipeJsonExchange;

struct FSDMutableSlotListItem
{
	ESDMutablePartSlot Slot = ESDMutablePartSlot::None;
	FText DisplayName;
	int32 PartCount = 0;
};

struct FSDMutablePackListItem
{
	FName PackId = NAME_None;
	FText DisplayName;
	int32 PartCount = 0;
	ESDMutableCatalogPackType PackType = ESDMutableCatalogPackType::Unknown;
	bool bEnabled = true;
};

struct FSDMutablePartListItem
{
	FSDMutableCatalogPartEntry Part;
	TSharedPtr<FSlateBrush> ThumbnailBrush;
};

/** Main Slate editor for Sidekicks recipes, shared by COI, recipe DataAsset, and actor component targets. */
class SSDMutableEditorWidget final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSDMutableEditorWidget)
	{
	}
	SLATE_END_ARGS()

	~SSDMutableEditorWidget();

	void Construct(const FArguments& InArgs);
	void SetTargetCustomizableObjectInstance(UCustomizableObjectInstance* InCustomizableObjectInstance);

private:
	// Target and catalog refresh.
	void ResetPreviewTargetToDefaults();
	void RefreshCatalog();
	void RefreshTargetFromSelection();
	void RefreshTargetFromSelectedCoi();

	// List rebuilding and selection UI.
	void RebuildSlotItems();
	void RebuildPackItems();
	void RebuildPartItems();
	void ApplyPartSearchFilter();
	void RebuildPackListWidget();
	void RebuildSlotListWidget();
	void RebuildPartListWidget();
	void RebuildSelectionPanels();
	void RefreshPartSelectionWidgets();
	void RebuildColorSwatchGrid();

	// Control builders.
	TSharedRef<SWidget> BuildMorphControlsWidget();
	TSharedRef<SWidget> BuildMaterialControlsWidget();
	TSharedRef<SWidget> BuildMaterialColorControlsWidget();
	TSharedRef<SWidget> BuildMaterialScalarControlsWidget();
	TSharedRef<SWidget> BuildPartDropdownMenuWidget();
	TSharedRef<ITableRow> GeneratePartListRow(TSharedPtr<FSDMutablePartListItem> PartItem, const TSharedRef<STableViewBase>& OwnerTable) const;
	void SetPackFilterEnabled(TSharedPtr<FSDMutablePackListItem> PackItem, bool bEnabled);
	void SelectPack(TSharedPtr<FSDMutablePackListItem> PackItem);
	void SelectSlot(TSharedPtr<FSDMutableSlotListItem> SlotItem);
	void ClearSlot(TSharedPtr<FSDMutableSlotListItem> SlotItem);
	void ClearAllSlots();
	void ApplyPart(TSharedPtr<FSDMutablePartListItem> PartItem);
	void SelectAdjacentPartOption(int32 Direction);
	bool CanSelectAdjacentPartOption(int32 Direction) const;
	void OnPartComboSelectionChanged(TSharedPtr<FSDMutablePartListItem> PartItem, ESelectInfo::Type SelectInfo);
	void OnPartListSelectionChanged(TSharedPtr<FSDMutablePartListItem> PartItem, ESelectInfo::Type SelectInfo);

	// Recipe editing and target application.
	float GetBodyShapeValue(FName ParameterName) const;
	void SetBodyShapeValue(FName ParameterName, float Value);
	float GetMaterialScalarValue(FName ParameterName) const;
	void SetMaterialScalarValue(FName ParameterName, float Value);
	FLinearColor GetMaterialColorValue(FName ParameterName) const;
	void OpenMaterialColorPicker(FName ParameterName);
	void OpenColorPickerForSlot(int32 ColorSlotIndex);
	void SetColorSlot(int32 ColorSlotIndex, FLinearColor Color, bool bApplyToTarget);
	void ApplyColorPaletteToTarget();
	bool ApplyEditorRecipeToTargetCoi();

	// Direct COI targets may need async load/compile before their Mutable parameters can be written.
	bool IsTargetCoiReadyForRecipeApply(UCustomizableObjectInstance& Coi);
	void ScheduleDeferredCoiRecipeApply(UCustomizableObjectInstance& Coi);
	void CancelDeferredCoiRecipeApply();
	bool TickDeferredCoiRecipeApply(float DeltaTime);
	bool CommitEditorStateToRecipeAsset(bool bUpdateColorTexture);
	void SetTargetRecipeAsset(USDMutableSidekickRecipeAsset* InRecipeAsset);
	void GenerateRandomColorPalette();
	void ClearColorPalette();
	void SaveCurrentRecipe();
	void SaveCurrentRecipeAsset();

	// JSON exchange always imports into the current target instead of retargeting to paths embedded in the file.
	FSDMutableRecipeJsonExchange MakeCurrentRecipeJsonExchange() const;
	void ApplyRecipeJsonExchangeToCurrentTarget(const FSDMutableRecipeJsonExchange& Exchange);
	void ExportCurrentRecipeJson();
	void ImportCurrentRecipeJson();
	FName GetSelectedOptionId(ESDMutablePartSlot Slot) const;
	bool IsPartSelected(const FSDMutableCatalogPartEntry& Part) const;
	FText GetColorSlotDisplayName(int32 ColorSlotIndex) const;
	FText GetSelectedPartText() const;
	void OnPartSearchTextChanged(const FText& NewText);
	FText GetCatalogStatusText() const;
	FText GetCatalogSummaryText() const;
	FText GetTargetStatusText() const;
	FText GetColorStatusText() const;
	TSharedRef<SWidget> GeneratePartComboWidget(TSharedPtr<FSDMutablePartListItem> PartItem) const;

	// Weak target references keep the shared tab from owning assets or actor components longer than the editor does.
	TWeakObjectPtr<USDMutableCatalog> LoadedCatalog;
	TWeakObjectPtr<USDMutableComponent> TargetComponent;
	TWeakObjectPtr<UCustomizableObjectInstance> TargetCustomizableObjectInstance;
	TWeakObjectPtr<USDMutableSidekickRecipeAsset> TargetRecipeAsset;
	TWeakObjectPtr<UCustomizableObjectInstance> DeferredCoiRecipeApplyTarget;
	FTSTicker::FDelegateHandle DeferredCoiRecipeApplyTickerHandle;
	TArray<TSharedPtr<FSDMutableSlotListItem>> SlotItems;
	TArray<TSharedPtr<FSDMutablePackListItem>> PackItems;
	TArray<TSharedPtr<FSDMutablePartListItem>> AllPartItems;
	TArray<TSharedPtr<FSDMutablePartListItem>> PartItems;
	TSharedPtr<FSDMutablePackListItem> SelectedPackItem;
	TSharedPtr<FSDMutableSlotListItem> SelectedSlotItem;
	TSharedPtr<FSDMutablePartListItem> SelectedPartItem;
	TSharedPtr<SVerticalBox> PackListBox;
	TSharedPtr<SVerticalBox> SlotListBox;
	TSharedPtr<SVerticalBox> PartListBox;
	TSharedPtr<SMenuAnchor> PartMenuAnchor;
	TSharedPtr<SListView<TSharedPtr<FSDMutablePartListItem>>> PartListView;
	TSharedPtr<SEditableTextBox> PartSearchTextBox;
	TSharedPtr<SUniformGridPanel> ColorSwatchGrid;
	FSDMutableSidekickRecipe EditorRecipe;
	FSDMutableColorPalette EditorColorPalette;
	// Slate holds direct-COI preview textures strongly; actor targets let USDMutableComponent own transient textures instead.
	TStrongObjectPtr<UTexture2D> PreviewColorTexture;
	FString CatalogStatus;
	FString TargetStatus;
	FString ColorStatus;
	FString PartSearchFilter;
	int32 SelectedColorSlotIndex = 0;
	int32 SpeciesPackCount = 0;
	int32 OutfitPackCount = 0;
	int32 SharedPackCount = 0;
	int32 UnknownPackCount = 0;
	int32 LoadedPackCount = 0;
	int32 LoadedPartCount = 0;
	bool bDeferredCoiCompileRequested = false;
};
