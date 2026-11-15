# Shared melee and local gold kills

Saxxy and other schema weapons with `item_class = saxxy` select the stock melee
family for the equipped class. The server receives an owned matching weapon,
with the native default-stock selection as a fallback. The client displays the
selected shared melee. This includes Scout's definition 0 and Spy's knife;
shared melee cannot accidentally select a primary or secondary weapon.

With Inventory/Skin Changer enabled, Saxxy, Golden Wrench, Golden Frying Pan and
weapons with the applied Australium attribute turn confirmed local victims into
gold statues, using the engine's gold material, rigid physics and turn-gold
sound. Explosive deaths take the corpse path instead of producing gibs.
Saxxy, Golden Wrench and Golden Frying Pan use their own native killfeed icons;
Australiums retain their base weapon's native icon. Native headshot/backstab
icon overrides and genuine crit, assist and domination data remain intact.

This is local visual emulation. No forged deaths, inventory messages, damage,
attack rotations or other outbound packets are generated. Other players see
what their server sends them. Dead Ringer fake deaths, suicides, other attackers
and deaths attributed to another weapon are excluded.

Weapon identity is cached from all local weapon handles, rather than assuming
that a delayed projectile kill came from the currently active slot. Corpse
initialization is intercepted before the gib/ragdoll choice. If entity data
arrives before its death event, a handle-validated fixed-size queue waits up to
100 ms. A consumed death cannot goldify a second corpse; user IDs, respawn,
round/map transitions, screenshot suppression, disabling and unload prevent
stale attribution. Server `customkill` stays unchanged; the corpse's cosmetic
damage field uses the gold type to bypass plasma dissolution.

Engine behavior was checked against Valve's
[ragdoll implementation](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/client/tf/c_tf_player.cpp)
and [death notice handling](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/client/tf/tf_hud_deathnotice.cpp).
The new OnDataChanged signature was checked for one match in the installed
64-bit TF2 client. A future game update can require a signature refresh.

Regression coverage: `inventory_weapon_policy_test.cpp`,
`gold_kill_policy_test.cpp`, and `gold_kill_integration_test.py`. The latter
compiles the production event, corpse-finalization and queue methods against an
engine fixture and checks both packet orders, gib suppression, native death
metadata, incorrect killers/weapons, feigns, respawns and expired handles.

## Pan fallback and model refresh

Golden Frying Pan prefers a real, owned ordinary Frying Pan (definition 264).
If none is available for the equipped class, selection falls back to the real
stock melee family and then the native default stock selection. Local fake pans
never count as owned server weapons. The ordinary pan's catalogue class mask
wins; the conservative fallback excludes Spy and Engineer. Saxxy continues to
use each class's stock melee directly.

Weapon replacements now invalidate the engine's cached world-model index,
register the selected display model, update the class hand model and rebuild
viewmodel attachments. The private cache field is decoded from a verified
instruction sequence with matching load/store offsets and bounds checks;
it is not a fixed offset blindly written after updates. Cached expected model
indices repair server/prediction replay without rebuilding assets every frame.
Inactive weapons cannot replace the held viewmodel, and taunt props are left
alone. Removing a replacement refreshes models even if a server update already
restored the original econ view.

The local econ item is also prepared before native weapon Deploy selects its
hand animation and emits draw sounds. This fixes both pan and Saxxy using the
stock shovel draw animation/sound. For example, the installed Soldier animation
`melee_allclass_draw` contains `Weapon_Scatter_Gun.Draw`, while the shovel uses
`Weapon_Shovel.Draw`; no invented Saxxy-specific sound is added.

`inventory_owned_weapon_test.py` and `inventory_weapon_models_test.py` compile
production selection/model methods against fixtures. They cover pan priority,
all nine classes, fake-item exclusion, first/third-person model indices, server
replay, inactive weapons, taunts and restoration. `weapon_model_policy_test.cpp`
checks that malformed cache instructions cannot yield a writable field offset.
