# MutableSidekicksPlugin

MutableSidekicksPlugin is an Unreal Engine plugin workflow for building Synty Sidekicks characters with Unreal Mutable. The plugin includes a Sidekicks Customizable Object, a root catalog DataAsset, bundled species/outfit pack catalogs, and editor tools for creating Sidekicks from `UCustomizableObjectInstance` assets.

## Requirements

- Unreal Engine 5.8 Preview 1 or a compatible 5.8 build.
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
/MutableSidekicksPlugin/Core/CO_Sidekicks.CO_Sidekicks
/MutableSidekicksPlugin/Core/SKM_Sidekicks_Empty.SKM_Sidekicks_Empty
/MutableSidekicksPlugin/Core/DataAssets/SD_RootCatalog.SD_RootCatalog
/MutableSidekicksPlugin/Core/DataAssets/Species/*
/MutableSidekicksPlugin/Core/DataAssets/Outfits/*
```

`SD_RootCatalog` already contains references to the bundled pack catalogs that ship with the plugin (This covers all available Synty Sidekicks as of 2026.06.13). Its import path arrays may be empty in a fresh install, so each project should add the mesh scan path that matches where its Synty Sidekicks content was imported. If no scan path is set, the catalog tools fall back to `/Game/Sidekicks`.

## Initial Setup In Your Project

1. Copy or install `MutableSidekicksPlugin` into your project's `Plugins` folder.
2. Open the project in Unreal Engine.
3. Enable these plugins:
   - `Mutable`
   - `MutableSidekicksPlugin`
4. Restart the editor if prompted.
5. Open `Edit > Project Settings > Plugins > Sidekicks Mutable`.
6. Set `Root Catalog` to:

```text
/MutableSidekicksPlugin/Core/DataAssets/SD_RootCatalog.SD_RootCatalog
```

7. Set `Sidekicks Customizable Object` to:

```text
/MutableSidekicksPlugin/Core/CO_Sidekicks.CO_Sidekicks
```

8. Open `SD_RootCatalog`.
9. In the `Import` section, add at least one `ScanRootPaths` entry for the folder where your Synty Sidekicks skeletal meshes are imported. A common value is:

```text
/Game/Sidekicks
```

10. Leave `PackCatalogScanRootPaths` empty unless you are using catalog discovery for custom or moved pack catalogs.
11. Save `SD_RootCatalog`.

`PackCatalogScanRootPaths` is only needed when you want the root catalog to discover additional or moved pack catalog DataAssets. The bundled pack catalogs are already referenced by `SD_RootCatalog`, so the default plugin setup does not require a pack catalog scan path.

Unreal asset paths use `/Game/...` for assets under a project's `Content` folder. Do not enter `/Content/...` in these fields.

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

These catalogs are intended to be used through the bundled `SD_RootCatalog`.

Recommended first refresh in a new project:

1. Open `SD_RootCatalog`.
2. Add `ScanRootPaths` entries for your imported Synty Sidekicks skeletal mesh folders.
3. If your imported meshes have extra `transform1`, `transform2`, or `transform3` hierarchy bones, click `FixSyntySidekickSkeletonTransformBones`.
4. Click `RebuildAllPackCatalogsFromAssetRegistry`.
5. Click `ValidateCatalogAgainstAssetRegistry`.
6. Save `SD_RootCatalog` and all changed pack catalogs.

Do not run `RebuildCatalogFromAssetRegistry` unless `PackCatalogScanRootPaths` points to the folders where you want pack catalogs discovered from. The bundled root catalog already knows about the bundled pack catalogs. If the discovery path is empty, the code falls back to `/Game/Sidekicks`, which is useful for mesh scans but usually not where pack catalog DataAssets live.

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
8. Add it to the appropriate pack array on `SD_RootCatalog`, or set `PackCatalogScanRootPaths` and run `RebuildCatalogFromAssetRegistry`.

Use `PackType` to decide where the pack appears:

- `Species`
- `Outfit`
- `Shared`
- `Unknown`

Use `AssetNameTokens` when several packs share the same scan root and need filename filtering.

## Root Catalog Actions

Open `SD_RootCatalog` and use the Details-panel buttons:

- `RebuildAllPackCatalogsFromAssetRegistry`: rebuilds every referenced pack catalog using the root scan paths and slot rules.
- `ValidateCatalogAgainstAssetRegistry`: compares meshes under the root scan paths against catalog entries and reports missing or stale references.
- `FixSyntySidekickSkeletonTransformBones`: repairs imported Synty Sidekicks skeletal meshes that contain extra `transform1`, `transform2`, or `transform3` hierarchy bones.
- `RebuildCatalogFromAssetRegistry`: discovers pack catalog assets under `PackCatalogScanRootPaths` and repopulates the root pack arrays by `PackType`.

For the bundled plugin catalogs, the usual maintenance actions are `FixSyntySidekickSkeletonTransformBones`, `RebuildAllPackCatalogsFromAssetRegistry`, and `ValidateCatalogAgainstAssetRegistry`.

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

or:

```text
Window > Sidekicks Mutable Window
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

1. Add or confirm `SD_RootCatalog > ScanRootPaths` entries.
2. Run `FixSyntySidekickSkeletonTransformBones` if the imported meshes have extra transform hierarchy bones.
3. Run `RebuildAllPackCatalogsFromAssetRegistry`.
4. Run `ValidateCatalogAgainstAssetRegistry`.
5. Save changed catalogs.

After adding custom pack catalogs:

1. Add the pack catalog to the correct array on `SD_RootCatalog`, or set `PackCatalogScanRootPaths` to the folder containing your pack catalogs.
2. If using discovery, run `RebuildCatalogFromAssetRegistry`.
3. Run `RebuildAllPackCatalogsFromAssetRegistry`.
4. Save changed catalogs.

After moving Sidekicks content:

1. Update `SD_RootCatalog > ScanRootPaths`.
2. Update affected pack catalog `ScanRootPaths` if they override the root.
3. Rebuild and validate catalogs.

## Troubleshooting

If the Sidekicks Mutable editor says no root catalog is configured, check:

```text
Edit > Project Settings > Plugins > Sidekicks Mutable > Root Catalog
```

If no mesh options appear:

- Confirm `Root Catalog` points to the plugin `SD_RootCatalog`.
- Confirm `SD_RootCatalog > ScanRootPaths` has at least one entry and points to your imported Sidekicks skeletal mesh folders.
- Confirm `SD_RootCatalog` references the bundled pack catalogs.
- Run `RebuildAllPackCatalogsFromAssetRegistry`.
- Run `ValidateCatalogAgainstAssetRegistry`.

If a newly created COI does not target the Sidekicks Mutable customizable object, check:

```text
Edit > Project Settings > Plugins > Sidekicks Mutable > Sidekicks Customizable Object
```

If a character opens without a recipe DataAsset, use `Save Recipe Asset As` to create the recipe, color texture, and COI asset set.
