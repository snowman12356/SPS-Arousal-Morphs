# SPS Arousal Morphs

A companion for Schlong Physics Swapper, for the player and for NPCs added
through its menu. While enabled, it reads SPS's live arousal (SPS API V2 for the
player, V3 for NPCs) and smoothly drives one or more RaceMenu BodyMorph sliders
configured in the INI.

The addon never controls FSMP/CBPC directly, changes bones, reloads XML files,
resets physics, or queries an arousal provider. If arousal jumps past SPS's
erect threshold before the length transition has finished, it makes one
temporary request through SPS's public API to keep SMP active. It releases the
request 0.2 seconds after full extension, then SPS performs its normal erection
transition. The short settle period lets the final RaceMenu morph update become
visible before physics changes.

## Requirements

- Skyrim AE 1.6.1170 with matching SKSE
- Address Library for SKSE Plugins
- Schlong Physics Swapper 2.1.0.2 or newer with compatibility API V2
- For NPC pages: a Schlong Physics Swapper build with compatibility API V3
  (without it the player features still work and the NPC pages say so)
- Recommended: a Schlong Physics Swapper build with compatibility API V5, for
  scene roles (player and NPCs) and a race-free SMP hold (see below). Older SPS
  still works.
- RaceMenu/SKEE BodyMorph interface V5
- SKSE Menu Framework 3
- A morph-enabled BodySlide build containing every configured morph

## Upgrading from SPS Retractable Addon

This addon was previously named SPS Retractable Addon. Remove the old
`SPSRetractableAddon.dll`; running both would drive the same morphs twice.
If `SPSArousalMorphs.ini` is missing, settings are read once from the old
`SPSRetractableAddon.ini` and saved under the new name. Morphs stored under the
old `SPSRetractableAddon` RaceMenu key are cleared automatically when a save is
loaded, so existing saves can be kept.

## Building

CommonLibSSE-NG is taken from the shared reference checkout at
`codex-references/native/CommonLibSSE-NG`, configurable through the
`COMMONLIB_SSE_SOURCE_DIR` cache entry (a `COMMONLIB_SSE_FOLDER` environment
variable still overrides it). If that path is missing, the older `.sps-deps`
copy is used with a warning.

The SKSE Menu Framework 3 header is taken from `codex-references/ui`,
configurable through the `SKSE_MENU_FRAMEWORK_INCLUDE_DIR` cache entry. If it is
missing, the older `.sps-deps/SKSE-Menu-Framework-3-Example/include` copy is
used instead.

The default build targets Skyrim SE and AE with CommonLibSSE-NG at v15.4.0.
Skyrim VR is opt-in because it pulls in the `openvr` submodule and a VR-only
menu dependency:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md -DVCPKG_MANIFEST_MODE=OFF `
  -DENABLE_SKYRIM_VR=ON
cmake --build build --config Release --target SPSArousalMorphs
```

VR support is build-complete but unverified in game: the addon needs
SKSE Menu Framework 3 with the ImGui VR Helper plugin installed for its page
to appear under headset UI.

## Behaviour

- At or below **Start Growing At**, every configured slider reaches its
  `RetractedValue`.
- Between **Start Growing At** and **Fully Extended At**, every slider
  interpolates progressively with SPS's live arousal.
- At or above **Fully Extended At**, every slider reaches its `ExtendedValue`.

These arousal points affect only the configured morphs. SPS continues its normal
SMP-to-CBPC behaviour using its own settings. For the natural sequence
"grow while soft, then become erect", set **Fully Extended At** below SPS's
erect threshold. A sudden arousal jump is also handled: SMP remains active
until the smoothed extension reaches 100%, then control is released to SPS.

Untick **Temporary SMP hold while extending** (INI `[General] SMPHold=false`)
to make no SMP request at all. SPS then switches physics on its own schedule,
so the shaft may go CBPC while the morphs are still extending, and SPS's
step-by-step CBPC rise then plays during the growth, which looks less smooth. The checkbox
saves immediately and releases any active hold.

During a SexLab or OStim scene, the addon makes no SMP request. What the sliders
do is set by **During a scene** on the Length Control page (INI `[General]
SceneMode`), for the player and for NPCs:

- **Follow role** (`SceneMode=0`, default): with SPS API V4 (player) and V5 (NPCs),
  the sliders are held at `ExtendedValue` only while SPS reports that actor as
  penetrating; while receiving, or while SPS has not identified a role, they stay
  at the length the arousal curve gave just before the scene started. With older
  SPS, every scene holds `ExtendedValue`.
- **Always fully extended** (`SceneMode=1`): `ExtendedValue` for the whole scene.
- **Keep the length from before the scene** (`SceneMode=2`): the length never
  changes during a scene.

Normal arousal behaviour returns when SPS's scene-end delay finishes. This needs
SPS's `SexLabPPlusOverride` or `OStimOverride` setting on for that framework.

With SPS API V4 the SMP hold also asks SPS to delay its CBPC rise, and the addon
listens for SPS's arousal events, so a sudden arousal jump is held before SPS
acts instead of up to 50 ms later.

Extension and retraction use separate time settings. The controller runs only
while **Enable addon** is checked in **SPS Arousal Morphs → Length Control**.
Turning it off immediately clears only the addon's own BodyMorph source.

The supplied defaults are:

```text
Start Growing At: 20
Fully Extended At: 60
Extension Time: 3.0 seconds
Retraction Time: 3.0 seconds
Morph1: Penis_Length, retracted -0.50, extended 0.00
```

## NPCs

Look at an NPC, open **SPS Arousal Morphs -> NPCs** and press **Add NPC in
crosshair**. The NPC gets its own **NPC: Name** page with its own arousal points,
times and morph list. Arousal points and times start from the player's settings;
the morph list starts empty, so add the sliders that NPC's body supports. Press **Apply
settings** on that page to save it, or **Remove this NPC** to remove it and clear
its morphs.

NPCs read arousal from SPS API V3. In a SexLab or OStim scene they follow the
same rule as the player (see Behaviour), using SPS API V5 for their scene role.
The addon makes no physics requests for NPCs. NPCs from a plugin are stored in
the INI, shared by all saves; up to 32 NPCs can be added. NPCs created at runtime
(generated or spawned by other mods) are stored in a per-save SKSE cosave instead,
written when you save the game, and shown as "Runtime-created, this save only".
Removing the mod leaves no trace in the save beyond that ignorable cosave record. The body mesh the
NPC wears must include each morph.

## Adding BodyMorph sliders

Sliders can be added in game or by editing the INI.

### In game

Open the SKSE Menu Framework menu and go to **SPS Arousal Morphs → Length
Control**. Each BodySlide is its own group, headed by the slider name. The
**Retracted** and **Extended** sliders under that heading belong to that
BodySlide. **Enabled** turns that one slider off without deleting it. **Remove**
deletes the group. To add one, type a slider name into the field and press
**Add**.

Press **Apply settings** to write the INI and start driving the new morph
immediately. Nothing is written until you apply, so you can experiment freely.

Duplicate names and empty names are highlighted while editing. Only the first
entry of a duplicated name is kept when applied, and up to 32 unique morphs are
accepted.

### By editing the INI

Edit `Data/SKSE/Plugins/SPSArousalMorphs.ini`. Increase `Count`, then add a
numbered section for each BodySlide/RaceMenu morph:

```ini
[Morphs]
Count=3

[Morph1]
Name=Penis_Length
Enabled=true
RetractedValue=-0.50
ExtendedValue=0.00

[Morph2]
Name=SPS_RootBlend
Enabled=true
RetractedValue=1.00
ExtendedValue=0.00

[Morph3]
Name=SPS_SheathOpen
Enabled=false
RetractedValue=1.00
ExtendedValue=0.00
```

Names must match the BodySlide slider names exactly. Values are direct final
BodyMorph targets, not percentages, and may range from `-3.00` to `3.00`.
`Enabled=false` keeps the slider in the list but does not drive it. A missing
`Enabled` line is treated as on, so older INI files keep working.

Use **Reload INI** on the SKSE Menu Framework page to load external edits
without restarting Skyrim.

## In-game diagnostics and recovery

The SKSE Menu Framework page displays SPS's live arousal, its configured erect
threshold, the current and target extension, and whether the temporary SMP hold
is active or in its settle period. It warns when **Fully Extended At** is above
the SPS erect threshold, because that configuration requires the addon to keep
SMP held until extension can finish.

**Emergency: disable and clear addon morphs** turns the addon off, releases its
SMP request, and clears every BodyMorph contribution owned by the
`SPSArousalMorphs` source key. It does not clear morphs belonging to other
mods.

Settings are saved in `Data/SKSE/Plugins/SPSArousalMorphs.ini` and are not
stored in the savegame.

## Morph ownership

The addon uses the unique RaceMenu source key `SPSArousalMorphs`. For each
configured slider it compensates for other sources so the combined result
matches the requested target without clearing those sources. This compensation
requires RaceMenu's default additive morph mode, `iBodyMorphMode=0` in
`skee64.ini`. With the averaged (`1`) or maximum (`2`) modes the final morph
values will not match the configured targets. Disabling the
addon clears every morph owned by its key, including sliders removed from the
INI. Other RaceMenu morph sources are never cleared.

## Deliberately not included

- direct SMP/CBPC switching
- physics resets, XML switching, collisions, or split physics
- bone transforms or freezing
- direct OSL/SLO/SLA integration
- arousal or erection calculations independent of SPS
