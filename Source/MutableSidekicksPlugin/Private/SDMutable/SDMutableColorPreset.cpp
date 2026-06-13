#include "SDMutable/SDMutableColorPreset.h"

#include "Engine/Texture2D.h"
#include "TextureResource.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogSDMutableColorPreset, Log, All);

namespace
{
	constexpr int32 SidekicksColorTextureSize = 32;
	constexpr int32 SidekicksColorPatchSize = 2;
	constexpr int32 SidekicksColorPatchesPerRow = SidekicksColorTextureSize / SidekicksColorPatchSize;

#if WITH_EDITOR
	FString MakeSidekicksColorTexturePackageName(const FString& PackagePath, const FString& AssetName)
	{
		FString CleanPath = PackagePath;
		CleanPath.ReplaceInline(TEXT("\\"), TEXT("/"));
		CleanPath.RemoveFromEnd(TEXT("/"));

		return FString::Printf(TEXT("%s/%s"), *CleanPath, *AssetName);
	}
#endif

	void ConfigureSidekicksColorTexture(UTexture2D& Texture)
	{
		Texture.SRGB = true;
		Texture.CompressionSettings = TC_VectorDisplacementmap;
		Texture.MipGenSettings = TMGS_NoMipmaps;
		Texture.Filter = TF_Nearest;
	}

	void BuildSidekicksColorTexturePixelData(const FSDMutableColorPalette& Palette, TArray<FColor>& OutPixels)
	{
		OutPixels.SetNumZeroed(SidekicksColorTextureSize * SidekicksColorTextureSize);

		for (int32 ColorIndex = 0; ColorIndex < FSDMutableColorPalette::NumSidekicksColorSlots; ++ColorIndex)
		{
			const FColor PixelColor = Palette.GetResolvedColorSlot(ColorIndex).ToFColor(false);

			const int32 PatchX = ColorIndex % SidekicksColorPatchesPerRow;
			const int32 PatchY = ColorIndex / SidekicksColorPatchesPerRow;
			const int32 StartX = PatchX * SidekicksColorPatchSize;
			const int32 StartY = PatchY * SidekicksColorPatchSize;

			for (int32 Y = 0; Y < SidekicksColorPatchSize; ++Y)
			{
				for (int32 X = 0; X < SidekicksColorPatchSize; ++X)
				{
					const int32 PixelIndex = (StartY + Y) * SidekicksColorTextureSize + (StartX + X);
					OutPixels[PixelIndex] = PixelColor;
				}
			}
		}
	}

	void WriteSidekicksColorTexturePlatformPixels(UTexture2D& Texture, const TArray<FColor>& Pixels)
	{
		FTexturePlatformData* PlatformData = Texture.GetPlatformData();
		if (!PlatformData)
		{
			PlatformData = new FTexturePlatformData();
			Texture.SetPlatformData(PlatformData);
		}

		PlatformData->SizeX = SidekicksColorTextureSize;
		PlatformData->SizeY = SidekicksColorTextureSize;
		PlatformData->SetNumSlices(1);
		PlatformData->PixelFormat = PF_B8G8R8A8;

		if (PlatformData->Mips.Num() == 0)
		{
			PlatformData->Mips.Add(new FTexture2DMipMap(SidekicksColorTextureSize, SidekicksColorTextureSize, 1));
		}

		FTexture2DMipMap& MipMap = PlatformData->Mips[0];
		MipMap.SizeX = SidekicksColorTextureSize;
		MipMap.SizeY = SidekicksColorTextureSize;
		MipMap.BulkData.SetBulkDataFlags(BULKDATA_ForceInlinePayload);

		void* TextureData = MipMap.BulkData.Lock(LOCK_READ_WRITE);
		TextureData = MipMap.BulkData.Realloc(SidekicksColorTextureSize * SidekicksColorTextureSize * sizeof(FColor));
		FMemory::Memcpy(TextureData, Pixels.GetData(), SidekicksColorTextureSize * SidekicksColorTextureSize * sizeof(FColor));
		MipMap.BulkData.Unlock();

		ConfigureSidekicksColorTexture(Texture);
		Texture.UpdateResource();
	}

#if WITH_EDITOR
	void WriteSidekicksColorTextureAssetPixels(UTexture2D& Texture, const TArray<FColor>& Pixels)
	{
		WriteSidekicksColorTexturePlatformPixels(Texture, Pixels);

		Texture.Source.Init(SidekicksColorTextureSize, SidekicksColorTextureSize, 1, 1, TSF_BGRA8);
		uint8* SourceData = Texture.Source.LockMip(0);
		FMemory::Memcpy(SourceData, Pixels.GetData(), SidekicksColorTextureSize * SidekicksColorTextureSize * sizeof(FColor));
		Texture.Source.UnlockMip(0);

		Texture.MarkPackageDirty();
		Texture.PostEditChange();
	}

	bool SaveSidekicksColorTexturePackage(UPackage& Package, UTexture2D& Texture)
	{
		const FString PackageFileName = FPackageName::LongPackageNameToFilename(
			Package.GetName(),
			FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;

		const bool bSaved = UPackage::SavePackage(&Package, &Texture, *PackageFileName, SaveArgs);
		if (!bSaved)
		{
			UE_LOG(LogSDMutableColorPreset, Warning, TEXT("Failed to save Sidekicks color texture package: %s"), *Package.GetName());
		}

		return bSaved;
	}
#endif
}

USDMutableColorPreset::USDMutableColorPreset()
{
	EnsureColorSlotCount();
}

void FSDMutableColorPalette::EnsureColorSlotCount()
{
	ColorSlots.SetNum(NumSidekicksColorSlots);
}

bool FSDMutableColorPalette::SetColorSlot(const int32 Index, const FLinearColor Color, const bool bOverride)
{
	if (Index < 0 || Index >= NumSidekicksColorSlots)
	{
		return false;
	}

	EnsureColorSlotCount();
	ColorSlots[Index].Color = Color;
	ColorSlots[Index].bOverride = bOverride;
	return true;
}

FLinearColor FSDMutableColorPalette::GetResolvedColorSlot(const int32 Index) const
{
	if (!ColorSlots.IsValidIndex(Index) || !ColorSlots[Index].bOverride)
	{
		return DefaultColor;
	}

	return ColorSlots[Index].Color;
}

void FSDMutableColorPalette::GenerateRandomColorPalette()
{
	EnsureColorSlotCount();

	FRandomStream RandomStream(bUseRandomSeed ? FMath::Rand() : RandomSeed);
	const float ClampedMinSaturation = FMath::Clamp(RandomMinSaturation, 0.0f, 1.0f);
	const float ClampedMinValue = FMath::Clamp(RandomMinValue, 0.0f, 1.0f);

	for (FSDMutableColorSlot& Slot : ColorSlots)
	{
		const float Hue = RandomStream.FRand();
		const float Saturation = FMath::Lerp(ClampedMinSaturation, 1.0f, RandomStream.FRand());
		const float Value = FMath::Lerp(ClampedMinValue, 1.0f, RandomStream.FRand());

		Slot.Color = FLinearColor::MakeFromHSV8(
			static_cast<uint8>(FMath::RoundToInt(Hue * 255.0f)),
			static_cast<uint8>(FMath::RoundToInt(Saturation * 255.0f)),
			static_cast<uint8>(FMath::RoundToInt(Value * 255.0f)));
		Slot.bOverride = true;
	}
}

void FSDMutableColorPalette::ClearColorPaletteOverrides()
{
	EnsureColorSlotCount();

	for (FSDMutableColorSlot& Slot : ColorSlots)
	{
		Slot.bOverride = false;
		Slot.Color = DefaultColor;
	}
}

void USDMutableColorPreset::EnsureColorSlotCount()
{
	Palette.EnsureColorSlotCount();
}

bool USDMutableColorPreset::SetColorSlot(const int32 Index, const FLinearColor Color, const bool bOverride)
{
	Modify();
	const bool bResult = Palette.SetColorSlot(Index, Color, bOverride);
	if (bResult)
	{
		MarkPackageDirty();
	}

	return bResult;
}

void USDMutableColorPreset::GenerateRandomColorPalette()
{
	Modify();
	Palette.GenerateRandomColorPalette();
	MarkPackageDirty();
}

void USDMutableColorPreset::ClearColorPaletteOverrides()
{
	Modify();
	Palette.ClearColorPaletteOverrides();
	MarkPackageDirty();
}

void USDMutableColorPreset::BuildColorTextureAsset()
{
#if WITH_EDITOR
	if (TexturePackagePath.IsEmpty() || TextureAssetName.IsEmpty())
	{
		UE_LOG(LogSDMutableColorPreset, Warning, TEXT("Cannot build Sidekicks color texture. Package path or asset name is empty."));
		return;
	}

	Palette.EnsureColorSlotCount();

	const FString PackageName = MakeSidekicksColorTexturePackageName(TexturePackagePath, TextureAssetName);
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		UE_LOG(LogSDMutableColorPreset, Warning, TEXT("Cannot create Sidekicks color texture package: %s"), *PackageName);
		return;
	}

	UTexture2D* Texture = FindObject<UTexture2D>(Package, *TextureAssetName);
	if (Texture && !bOverwriteExistingTexture)
	{
		LastGeneratedTexture = Texture;
		UE_LOG(LogSDMutableColorPreset, Log, TEXT("Sidekicks color texture already exists and overwrite is disabled: %s"), *Texture->GetPathName());
		return;
	}

	const bool bCreatedTexture = Texture == nullptr;
	if (!Texture)
	{
		Texture = NewObject<UTexture2D>(Package, *TextureAssetName, RF_Public | RF_Standalone);
	}

	TArray<FColor> Pixels;
	BuildSidekicksColorTexturePixelData(Palette, Pixels);
	WriteSidekicksColorTextureAssetPixels(*Texture, Pixels);

	if (bCreatedTexture)
	{
		FAssetRegistryModule::AssetCreated(Texture);
	}

	Package->MarkPackageDirty();
	LastGeneratedTexture = Texture;
	MarkPackageDirty();
	SaveSidekicksColorTexturePackage(*Package, *Texture);

	UE_LOG(LogSDMutableColorPreset, Log, TEXT("Built Sidekicks color texture from preset %s: %s"), *GetName(), *Texture->GetPathName());
#else
	UE_LOG(LogSDMutableColorPreset, Warning, TEXT("BuildColorTextureAsset is editor-only."));
#endif
}

UTexture2D* USDMutableColorPreset::BuildTransientColorTexture()
{
	LastGeneratedTexture = BuildTransientColorTextureFromPalette(Palette, this);
	return LastGeneratedTexture;
}

void USDMutableColorPreset::BuildTransientColorTextureForPreview()
{
	BuildTransientColorTexture();
}

UTexture2D* USDMutableColorPreset::BuildTransientColorTextureFromPalette(const FSDMutableColorPalette& InPalette, UObject* Outer)
{
	UObject* TextureOuter = Outer ? Outer : GetTransientPackage();
	UTexture2D* Texture = UTexture2D::CreateTransient(SidekicksColorTextureSize, SidekicksColorTextureSize, PF_B8G8R8A8);
	if (!Texture)
	{
		UE_LOG(LogSDMutableColorPreset, Warning, TEXT("Failed to create transient Sidekicks color texture."));
		return nullptr;
	}

	if (TextureOuter && TextureOuter != Texture->GetOuter())
	{
		Texture->Rename(nullptr, TextureOuter, REN_DontCreateRedirectors | REN_NonTransactional);
	}

	TArray<FColor> Pixels;
	BuildSidekicksColorTexturePixelData(InPalette, Pixels);
	WriteSidekicksColorTexturePlatformPixels(*Texture, Pixels);

	return Texture;
}

FLinearColor USDMutableColorPreset::GetResolvedColorSlot(const int32 Index) const
{
	return Palette.GetResolvedColorSlot(Index);
}
