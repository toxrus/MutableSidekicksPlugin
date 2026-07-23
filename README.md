# MutableSidekicksPlugin

## USE FOR TESTING/LEARNING ONLY - THIS IS VERY MUCH WIP, NOT RECOMMENDED FOR USE IN ACTUAL PRODUCTION!

MutableSidekicksPlugin is an Unreal Engine plugin workflow for building Synty Sidekicks characters with Unreal Mutable. It supports either the original Unity-package mesh hierarchy or the official Unreal-package mesh hierarchy. The plugin includes matching Customizable Objects and root catalogs, bundled species/outfit pack catalogs, a generic example character Blueprint, and editor tools for COI assets and actor-local recipes.

## Requirements

- Unreal Engine 5.8.
- Unreal Mutable plugin enabled.
- Synty Sidekicks skeletal mesh content imported into your project.
- This plugin installed in your project, for example:

```text
YourProject/Plugins/MutableSidekicksPlugin
```

## Plugin Content

Enable plugin content in the Content Browser to inspect the included assets.

Important included assets:

```text
/MutableSidekicksPlugin/Core/UnitySKM/CO_Sidekicks.CO_Sidekicks
/MutableSidekicksPlugin/Core/UnitySKM/SKM_BaseModel.SKM_BaseModel
/MutableSidekicksPlugin/Core/UnrealSKM/CO_Sidekicks_Unreal.CO_Sidekicks_Unreal
/MutableSidekicksPlugin/Core/UnrealSKM/SKM_BaseModel_Unreal.SKM_BaseModel_Unreal
/MutableSidekicksPlugin/Core/DataAssets/SD_RootCatalog.SD_RootCatalog
/MutableSidekicksPlugin/Core/DataAssets/SD_RootCatalog_Unreal.SD_RootCatalog_Unreal
/MutableSidekicksPlugin/Core/DataAssets/Species/*
/MutableSidekicksPlugin/Core/DataAssets/Outfits/*
/MutableSidekicksPlugin/Examples/BP_MutableSD.BP_MutableSD
```

`SKM_BaseModel` is a distributable placeholder that keeps the `CO_Sidekicks` reference valid when the plugin is first installed. It is not the proper Synty BaseModel. During setup, replace or reimport this placeholder in place using your licensed BaseModel so the asset keeps this exact path:

```text
/MutableSidekicksPlugin/Core/UnitySKM/SKM_BaseModel.SKM_BaseModel
```

Do not import the proper BaseModel as a differently named second asset. The Unity CO references the path above. The official Unreal pipeline uses its separate `UnrealSKM` reference mesh and CO.

## Choose One Project-Wide Pipeline

Open:

```text
Edit > Project Settings > Plugins > Sidekicks Mutable
```

Both settings are required. They are authoritative for all `USDMutableComponent` actors, the generic example Blueprint, and newly created Sidekick COIs.

For meshes imported from the Unity Sidekicks package, select:

```text
Root Catalog:
/MutableSidekicksPlugin/Core/DataAssets/SD_RootCatalog.SD_RootCatalog

Sidekicks Customizable Object:
/MutableSidekicksPlugin/Core/UnitySKM/CO_Sidekicks.CO_Sidekicks
```

For meshes from the official Unreal Sidekicks package, select:

```text
Root Catalog:
/MutableSidekicksPlugin/Core/DataAssets/SD_RootCatalog_Unreal.SD_RootCatalog_Unreal

Sidekicks Customizable Object:
/MutableSidekicksPlugin/Core/UnrealSKM/CO_Sidekicks_Unreal.CO_Sidekicks_Unreal
```

Do not mix the Unity catalog with the Unreal CO, or the Unreal catalog with the Unity CO. Their skeletal hierarchies are incompatible. Running both pipelines together in one project is not a supported workflow; choose one matching pair for the project.

If either setting is `None`, SD component actors cannot resolve a complete pipeline. A COI already stored on an actor, template, or recipe is reused only when its underlying CO matches the configured `Sidekicks Customizable Object`.

The bundled pack catalogs cover all available Synty Sidekicks as of 2026.06.13. In a fresh install, use `RebuildCatalogFromAssetRegistry` on the selected root catalog before rebuilding its pack mesh entries. Each project must also add the scan path for the selected Unity or Unreal mesh tree. Catalog rebuild, validation, and skeleton-repair tools stop with a warning when no scan path is configured.

## Initial Setup In Your Project

1. Copy or install `MutableSidekicksPlugin` into your project's `Plugins` folder.
2. Open the project in Unreal Engine.
3. Enable these plugins:
   - `Mutable`
   - `MutableSidekicksPlugin`
4. Restart the editor if prompted.
5. In the Content Browser, enable `Show Plugin Content` if it is not already visible.
6. Choose the Unity or Unreal pipeline and prepare its corresponding reference mesh:

```text
Unity:
/MutableSidekicksPlugin/Core/UnitySKM/SKM_BaseModel.SKM_BaseModel

Official Unreal:
/MutableSidekicksPlugin/Core/UnrealSKM/SKM_BaseModel_Unreal.SKM_BaseModel_Unreal
```

7. Replace or reimport the selected placeholder in place using the matching licensed Synty BaseModel. Keep its plugin asset name and path unchanged. If Unreal prompts for a skeleton, use the skeleton belonging to that same Unity or Unreal mesh hierarchy.
8. Recompile and save the matching `CO_Sidekicks` or `CO_Sidekicks_Unreal`.
9. Open `Edit > Project Settings > Plugins > Sidekicks Mutable`.
10. Set both `Root Catalog` and `Sidekicks Customizable Object` to the matching pair in **Choose One Project-Wide Pipeline**.
11. Open the selected root catalog: `SD_RootCatalog` for Unity-package meshes or `SD_RootCatalog_Unreal` for official Unreal-package meshes.
12. In the `Import` section, add at least one `ScanRootPaths` entry for that hierarchy's imported skeletal meshes, for example:

```text
/Game/<your imported Sidekicks mesh folder>
```

13. Confirm that `PackCatalogScanRootPaths` contains the bundled pack-catalog folder:

```text
/MutableSidekicksPlugin/Core/DataAssets
```

14. Optional: set `Icon Scan Root Path` in Project Settings to the content folder containing `T_Icon_<SkeletalMeshAssetName>` thumbnails. Leave it empty to disable automatic thumbnail discovery.

14. Click `RebuildCatalogFromAssetRegistry` to populate the selected root's pack-catalog references.
15. If your imported meshes have extra `transform1`, `transform2`, or `transform3` hierarchy bones, click `FixSyntySidekickSkeletonTransformBones`.
16. Click `RebuildAllPackCatalogsFromAssetRegistry` to populate the referenced packs from the selected mesh tree.
17. Click `ValidateCatalogAgainstAssetRegistry`.
18. Save the selected root catalog and all changed pack catalogs.

The order of the two rebuild actions matters. `RebuildCatalogFromAssetRegistry` first discovers the pack catalog DataAssets and adds them to the selected root. Only then can `RebuildAllPackCatalogsFromAssetRegistry` process those referenced catalogs and add their skeletal mesh references.

Change or add `PackCatalogScanRootPaths` entries only when the bundled pack catalogs have moved or you want to discover custom pack catalog DataAssets.

Unreal asset paths use `/Game/...` for assets under a project's `Content` folder. Do not enter `/Content/...` in these fields.

## Generic Example Character

`/MutableSidekicksPlugin/Examples/BP_MutableSD` is pipeline-neutral. Its SD component does not store a per-actor catalog. It resolves `Root Catalog` from Project Settings and creates private transient editor/runtime COIs from configured `Sidekicks Customizable Object`.

Configure the matching settings pair before placing or copying the Blueprint. The same example then follows either the Unity or Unreal pipeline automatically.

## Catalog Model

The catalog system has two layers:

- **Root Catalog**: the shared plugin configuration and entry point.
- **Pack Catalogs**: per-pack DataAssets for species, outfits, shared parts, or custom groups.

The root catalog owns:

- Mesh scan roots in `ScanRootPaths`.
- Optional pack catalog discovery roots in `PackCatalogScanRootPaths`.
- Slot name rules that map Synty filename tokens to Sidekicks slots.
- Slot templates that map Sidekicks slots to Mutable parameter names.
- The global empty skeletal mesh used for `None` selections.
- Soft references to species, outfit, shared, and unknown pack catalogs.

Pack catalogs own:

- `PackId`
- `DisplayName`
- `PackType`
- `PackTags`
- Optional `ScanRootPaths`
- Optional `AssetNameTokens`
- Slot-grouped mesh entries
- Optional UI thumbnails
- Color metadata used by the Sidekicks Mutable editor

Pack entries use soft references to skeletal meshes and thumbnails, so loading a catalog does not force-load every mesh in the project.

## Using The Bundled Pack Catalogs

The plugin ships with pack catalogs under:

```text
/MutableSidekicksPlugin/Core/DataAssets/Species
/MutableSidekicksPlugin/Core/DataAssets/Outfits
```

These catalogs are shared by the two pipelines and are used through the root catalog selected in Project Settings.

Recommended first refresh in a new project:

1. Open the selected Unity or Unreal root catalog.
2. Add `ScanRootPaths` entries for your imported Synty Sidekicks skeletal mesh folders.
3. Confirm `PackCatalogScanRootPaths` includes `/MutableSidekicksPlugin/Core/DataAssets`.
4. Click `RebuildCatalogFromAssetRegistry` to populate the root catalog's pack references.
5. If your imported meshes have extra `transform1`, `transform2`, or `transform3` hierarchy bones, click `FixSyntySidekickSkeletonTransformBones`.
6. Click `RebuildAllPackCatalogsFromAssetRegistry` to populate the referenced pack catalogs.
7. Click `ValidateCatalogAgainstAssetRegistry`.
8. Save the selected root catalog and all changed pack catalogs.

Always run `RebuildCatalogFromAssetRegistry` before `RebuildAllPackCatalogsFromAssetRegistry` during initial setup. The first action populates the root catalog's pack references; the second action can only rebuild packs that the root catalog references.

## Creating Or Editing Pack Catalogs

To create a custom pack catalog:

1. In the Content Browser, right-click in the target folder.
2. Choose `Sidekicks Mutable > Sidekick Pack Catalog`.
3. Name the asset.
4. Open the new pack catalog.
5. Set:
   - `PackId`
   - `DisplayName`
   - `PackType`
   - `PackTags`
   - `AssetNameTokens`, if the pack should only capture meshes with specific filename tokens
6. Click `RebuildPackCatalogFromAssetRegistry` in the Details panel.
7. Save the pack catalog.
8. Add it to the appropriate pack array on the selected root catalog, or set `PackCatalogScanRootPaths` and run `RebuildCatalogFromAssetRegistry`.

Use `PackType` to decide where the pack appears:

- `Species`
- `Outfit`
- `Shared`
- `Unknown`

Use `AssetNameTokens` when several packs share the same scan root and need filename filtering.

## Root Catalog Actions

Open the root catalog selected in Project Settings and use the Details-panel buttons:

- `RebuildCatalogFromAssetRegistry`: discovers pack catalog assets under `PackCatalogScanRootPaths` and repopulates the root pack arrays by `PackType`.
- `RebuildAllPackCatalogsFromAssetRegistry`: rebuilds every referenced pack catalog using the root scan paths and slot rules. During initial setup, run this only after `RebuildCatalogFromAssetRegistry`.
- `ValidateCatalogAgainstAssetRegistry`: compares meshes under the root scan paths against catalog entries and reports missing or stale references.
- `FixSyntySidekickSkeletonTransformBones`: repairs imported Synty Sidekicks skeletal meshes that contain extra `transform1`, `transform2`, or `transform3` hierarchy bones.

For initial setup with the bundled plugin catalogs, use `RebuildCatalogFromAssetRegistry`, then `RebuildAllPackCatalogsFromAssetRegistry`, followed by `ValidateCatalogAgainstAssetRegistry`.

## Creating A New Sidekick With A COI

The editor workflow is based around a `UCustomizableObjectInstance` asset.

To create a new Sidekick COI:

1. In the Content Browser, right-click in the folder where you want the character assets.
2. Choose `Sidekicks Mutable > Sidekick Customizable Object Instance`.
3. Name the new COI asset.
4. Open the COI asset.
5. The `Sidekicks Mutable` editor tab should open automatically. You can also open it manually from:

```text
Window > Sidekicks Mutable
```

6. Select the COI asset in the Content Browser if needed.
7. In the Sidekicks Mutable editor, click `Use Selected COI`.
8. Use the pack filters, slot list, and mesh dropdown to choose Sidekicks parts.
9. Use the material color, material scalar, and morph controls to adjust the character.
10. Use `None` for any slot that should use the catalog empty mesh.
11. Click `Save Recipe Asset As` when you are ready to create a persistent set of assets.

Saving creates a recipe DataAsset, generated color texture, and saved COI in the selected folder. The default generated names use these prefixes:

```text
DA_<Name>
T_<Name>
COI_<Name>
```

After a recipe DataAsset exists, the `Save` button updates the associated recipe, color texture, and COI packages.

## Recipe Assets And JSON Presets

`Sidekick Recipe DataAsset` stores the reconstructable character state:

- Mesh selections
- Color palette
- Generated color texture reference
- Associated COI reference

To create one manually:

1. Right-click in the Content Browser.
2. Choose `Sidekicks Mutable > Sidekick Recipe DataAsset`.
3. Open the asset and assign or edit its fields as needed.

The Sidekicks Mutable editor also supports:

- `Export JSON`: exports the current live character recipe as a shareable JSON preset.
- `Import JSON`: imports a JSON preset onto the current COI or active recipe target.

JSON import does not require creating a recipe asset first, but saving a final reusable character should use `Save Recipe Asset As`.

## Color Texture Notes

Sidekicks characters use a fixed 32x32 color texture:

- 256 color slots.
- Each slot occupies a 2x2 texel patch.
- The Sidekicks Mutable editor rebuilds the texture from the saved color palette.

Treat the generated texture as derived output. The recipe DataAsset and palette state are the important saved data.

## Common Maintenance Tasks

After importing new Sidekicks meshes:

1. Add or confirm the selected root catalog's `ScanRootPaths` entries.
2. Run `FixSyntySidekickSkeletonTransformBones` if the imported meshes have extra transform hierarchy bones.
3. Run `RebuildAllPackCatalogsFromAssetRegistry`.
4. Run `ValidateCatalogAgainstAssetRegistry`.
5. Save changed catalogs.

After adding custom pack catalogs:

1. Add the pack catalog to the correct array on the selected root catalog, or set `PackCatalogScanRootPaths` to the folder containing your pack catalogs.
2. If using discovery, run `RebuildCatalogFromAssetRegistry`.
3. Run `RebuildAllPackCatalogsFromAssetRegistry`.
4. Save changed catalogs.

After moving Sidekicks content:

1. Update the selected root catalog's `ScanRootPaths`.
2. Update affected pack catalog `ScanRootPaths` if they override the root.
3. Rebuild and validate catalogs.

## Troubleshooting

If the Sidekicks Mutable editor says no root catalog is configured, check:

```text
Edit > Project Settings > Plugins > Sidekicks Mutable > Root Catalog
```

If an SD component actor has no preview or runtime COI, verify that both authoritative settings are populated and form one matching pair:

```text
Edit > Project Settings > Plugins > Sidekicks Mutable
  Root Catalog
  Sidekicks Customizable Object
```

If no mesh options appear:

- Confirm `Root Catalog` points to the catalog for the selected Unity or Unreal pipeline.
- Confirm the selected root catalog's `ScanRootPaths` points to the matching imported Sidekicks mesh tree.
- Confirm `PackCatalogScanRootPaths` contains `/MutableSidekicksPlugin/Core/DataAssets`.
- Run `RebuildCatalogFromAssetRegistry` and confirm the selected root now references the bundled pack catalogs.
- Run `RebuildAllPackCatalogsFromAssetRegistry`.
- Run `ValidateCatalogAgainstAssetRegistry`.

If a newly created COI does not target the expected Unity or Unreal Customizable Object, check:

```text
Edit > Project Settings > Plugins > Sidekicks Mutable > Sidekicks Customizable Object
```

If a character opens without a recipe DataAsset, use `Save Recipe Asset As` to create the recipe, color texture, and COI asset set.
