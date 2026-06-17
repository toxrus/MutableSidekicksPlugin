#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture.h"
#include "GameplayTagContainer.h"
#include "SDMutableTypes.generated.h"

/** Slot names are the plugin-side contract for the skeletal mesh parameters authored in CO_Sidekicks. */
UENUM(BlueprintType)
enum class ESDMutablePartSlot : uint8
{
	None,
	Hair,
	Head,
	EyebrowLeft,
	EyebrowRight,
	EyeLeft,
	EyeRight,
	EarLeft,
	EarRight,
	FacialHair,
	Nose,
	Teeth,
	Tongue,
	Torso,
	ArmUpperLeft,
	ArmUpperRight,
	ArmLowerLeft,
	ArmLowerRight,
	HandLeft,
	HandRight,
	Hips,
	LegLeft,
	LegRight,
	FootLeft,
	FootRight,
	AttachmentFace,
	AttachmentHead,
	AttachmentBack,
	AttachmentShoulderLeft,
	AttachmentShoulderRight,
	AttachmentElbowLeft,
	AttachmentElbowRight,
	AttachmentHipsBack,
	AttachmentHipsFront,
	AttachmentHipsLeft,
	AttachmentHipsRight,
	AttachmentKneeLeft,
	AttachmentKneeRight,
	AttachmentWrap
};

/** One reconstructable mesh choice in a recipe. NAME_None represents the catalog's explicit empty mesh option. */
USTRUCT(BlueprintType)
struct FSDMutablePartSelection
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable")
	ESDMutablePartSlot Slot = ESDMutablePartSlot::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable")
	FName OptionId = NAME_None;
};

/** Mutable morph sliders that shape the shared Sidekicks body. */
USTRUCT(BlueprintType)
struct FSDMutableBodyShape
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MaleFemale = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Heavy = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Buff = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Skinny = 0.0f;
};

/** Material controls stored with a recipe; the palette can override BaseColor with a generated texture at apply time. */
USTRUCT(BlueprintType)
struct FSDMutableMaterialSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable")
	TSoftObjectPtr<UTexture> BaseColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable")
	FLinearColor SkinColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable")
	FLinearColor EyeColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable")
	FLinearColor DirtColor = FLinearColor::Black;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable", meta=(ClampMin="0.0", ClampMax="1.0"))
	float CutWeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable", meta=(ClampMin="0.0", ClampMax="1.0"))
	float DirtWeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable", meta=(ClampMin="0.0", ClampMax="1.0"))
	float DarkWeight = 0.0f;
};

/** Lightweight UI option used by editor selectors without forcing mesh assets to load. */
USTRUCT(BlueprintType)
struct FSDMutableOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable")
	FName OptionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable")
	FGameplayTagContainer Tags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDMutable")
	bool bIsNoneOption = false;
};

/** The durable character recipe: mesh choices, morph values, and material controls, but not derived generated assets. */
USTRUCT(BlueprintType)
struct FSDMutableSidekickRecipe
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable")
	int32 Version = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable")
	TArray<FSDMutablePartSelection> Parts;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable")
	FSDMutableBodyShape BodyShape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SDMutable")
	FSDMutableMaterialSettings Material;
};

struct MUTABLESIDEKICKSPLUGIN_API FSDMutableHelpers
{
	static FText GetColorSlotDisplayName(int32 ColorSlotIndex);
};
