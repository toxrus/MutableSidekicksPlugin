#include "SDMutable/SDMutableTextureBuilderLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"
#include "SDMutable/SDMutableColorPreset.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace SDMutableTextureBuilderPrivate
{
	// Must match USDMutableColorPreset's runtime builder: 256 logical palette slots, 2x2 pixels per slot.
	constexpr int32 TextureSize = 32;
	constexpr int32 PatchSize = 2;
	constexpr int32 PatchesPerRow = TextureSize / PatchSize;

	FString MakePackageName(const FString& PackagePath, const FString& AssetName)
	{
		FString CleanPath = PackagePath;
		CleanPath.ReplaceInline(TEXT("\\"), TEXT("/"));
		CleanPath.RemoveFromEnd(TEXT("/"));

		return FString::Printf(TEXT("%s/%s"), *CleanPath, *AssetName);
	}

	void ConfigureSidekicksTexture(UTexture2D& Texture)
	{
		Texture.SRGB = true;
		Texture.CompressionSettings = TC_VectorDisplacementmap;
		Texture.MipGenSettings = TMGS_NoMipmaps;
		Texture.Filter = TF_Nearest;
	}

	void BuildPixelData(const FSDMutableColorPalette& Palette, TArray<FColor>& OutPixels)
	{
		OutPixels.SetNumZeroed(TextureSize * TextureSize);

		for (int32 ColorIndex = 0; ColorIndex < FSDMutableColorPalette::NumSidekicksColorSlots; ++ColorIndex)
		{
			const FColor PixelColor = Palette.GetResolvedColorSlot(ColorIndex).ToFColor(false);

			const int32 PatchX = ColorIndex % PatchesPerRow;
			const int32 PatchY = ColorIndex / PatchesPerRow;
			const int32 StartX = PatchX * PatchSize;
			const int32 StartY = PatchY * PatchSize;

			for (int32 Y = 0; Y < PatchSize; ++Y)
			{
				for (int32 X = 0; X < PatchSize; ++X)
				{
					const int32 PixelIndex = (StartY + Y) * TextureSize + (StartX + X);
					OutPixels[PixelIndex] = PixelColor;
				}
			}
		}
	}

	void WritePixelDataToTexture(UTexture2D& Texture, const TArray<FColor>& Pixels)
	{
		// Persistent editor assets need both platform data for preview and source data for saving/cooking.
		FTexturePlatformData* PlatformData = Texture.GetPlatformData();
		if (!PlatformData)
		{
			PlatformData = new FTexturePlatformData();
			Texture.SetPlatformData(PlatformData);
		}

		PlatformData->SizeX = TextureSize;
		PlatformData->SizeY = TextureSize;
		PlatformData->SetNumSlices(1);
		PlatformData->PixelFormat = PF_B8G8R8A8;

		if (PlatformData->Mips.Num() == 0)
		{
			PlatformData->Mips.Add(new FTexture2DMipMap(TextureSize, TextureSize, 1));
		}

		FTexture2DMipMap& MipMap = PlatformData->Mips[0];
		MipMap.SizeX = TextureSize;
		MipMap.SizeY = TextureSize;
		MipMap.BulkData.SetBulkDataFlags(BULKDATA_ForceInlinePayload);

		void* TextureData = MipMap.BulkData.Lock(LOCK_READ_WRITE);
		TextureData = MipMap.BulkData.Realloc(TextureSize * TextureSize * sizeof(FColor));
		FMemory::Memcpy(TextureData, Pixels.GetData(), TextureSize * TextureSize * sizeof(FColor));
		MipMap.BulkData.Unlock();

		Texture.Source.Init(TextureSize, TextureSize, 1, 1, TSF_BGRA8);
		uint8* SourceData = Texture.Source.LockMip(0);
		FMemory::Memcpy(SourceData, Pixels.GetData(), TextureSize * TextureSize * sizeof(FColor));
		Texture.Source.UnlockMip(0);

		ConfigureSidekicksTexture(Texture);
		Texture.MarkPackageDirty();
		Texture.PostEditChange();
		Texture.UpdateResource();
	}

	bool SaveTexturePackage(UPackage& Package, UTexture2D& Texture)
	{
		const FString PackageFileName = FPackageName::LongPackageNameToFilename(
			Package.GetName(),
			FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;

		return UPackage::SavePackage(&Package, &Texture, *PackageFileName, SaveArgs);
	}
}

UTexture2D* USDMutableTextureBuilderLibrary::BuildSidekicksColorTextureAsset(
	USDMutableColorPreset* ColorPreset,
	const FString& PackagePath,
	const FString& AssetName,
	const bool bOverwriteExisting)
{
	if (!ColorPreset || PackagePath.IsEmpty() || AssetName.IsEmpty())
	{
		return nullptr;
	}

	const FString PackageName = SDMutableTextureBuilderPrivate::MakePackageName(PackagePath, AssetName);
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return nullptr;
	}

	UTexture2D* Texture = FindObject<UTexture2D>(Package, *AssetName);
	if (Texture && !bOverwriteExisting)
	{
		return Texture;
	}

	const bool bCreatedTexture = Texture == nullptr;
	if (!Texture)
	{
		Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone);
	}

	if (!WriteSidekicksColorTexture(Texture, ColorPreset))
	{
		return nullptr;
	}

	if (bCreatedTexture)
	{
		FAssetRegistryModule::AssetCreated(Texture);
	}

	Package->MarkPackageDirty();
	SDMutableTextureBuilderPrivate::SaveTexturePackage(*Package, *Texture);
	return Texture;
}

bool USDMutableTextureBuilderLibrary::WriteSidekicksColorTexture(
	UTexture2D* Texture,
	const USDMutableColorPreset* ColorPreset)
{
	if (!Texture || !ColorPreset)
	{
		return false;
	}

	return WriteSidekicksColorTextureFromPalette(Texture, ColorPreset->Palette);
}

bool USDMutableTextureBuilderLibrary::WriteSidekicksColorTextureFromPalette(
	UTexture2D* Texture,
	const FSDMutableColorPalette& Palette)
{
	if (!Texture)
	{
		return false;
	}

	TArray<FColor> Pixels;
	SDMutableTextureBuilderPrivate::BuildPixelData(Palette, Pixels);
	SDMutableTextureBuilderPrivate::WritePixelDataToTexture(*Texture, Pixels);
	return true;
}
