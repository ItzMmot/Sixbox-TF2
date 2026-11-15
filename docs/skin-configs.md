# Skin and backpack persistence

Normal configs now contain a `SkinChanger` section with weapon overrides,
Backpack Changer items and the loadout. Saving an ordinary config therefore
preserves its appearance settings along with the other client settings.
Existing main configs without this section leave the active inventory data
unchanged when loaded.

Dedicated configs in **Settings → Configs → Skins** contain the same inventory
payload plus the Skin Changer enabled/unsupported-paint options. Each backpack
entry keeps its unique serial, definition, style, unusual effect and both paint
colors. Identical definitions remain separate items; 64-bit equipped item IDs
are written as decimal strings. Main configs keep their per-bind options in the
regular Vars maps; embedded skin data never overwrite those maps.

The client remembers which source was selected last. Saving/loading a main
config with embedded skins selects that source. Saving/loading a separate skin
profile selects that profile. Startup restores the main/visual configs and then
the selected skin profile if appropriate. Old `active.cfg` files without the
source flag retain separate-profile behavior. Missing or malformed standalone
skin sections fail without clearing the active inventory; an explicitly empty
skin section clears it.

Skin overrides, backpack and loadout are captured and replaced together under
the inventory mutex. Failed config loads restore the previous inventory along
with their other state. Config writes flush a temporary file before replacement;
failed Windows replacement preserves the original file instead of deleting it.
Filesystem streams also keep native filesystem paths through reading/writing.

Resave a normal config once to add its skin section. Data omitted by an older
version cannot be reconstructed from that config alone; an existing separate
skin profile can still be loaded and resaved.

Validation uses the actual serialization, active-state and startup functions:
all skin attributes, duplicate backpack definitions, paints/styles/effects,
loadout IDs, legacy/empty/corrupt sections, independent bind options and source
selection. Windows/Wine checks also exercise overwrite, a locked destination
that rejects replacement, and a Unicode filename. Live inventory restoration
still needs testing after restarting with the updated DLL.
