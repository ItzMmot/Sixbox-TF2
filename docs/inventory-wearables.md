# Inventory Changer cosmetic loadout

Changing a cosmetic slot makes the current class's saved Inventory Changer
selection authoritative for clothing. The three slots are **Head (7), Misc (8)
and Misc2 (10)**. Action (9) and Taunt (11) are excluded. Slots not explicitly
changed follow the inventory UI's currently equipped item; explicit empty slots
stay empty. Without any cosmetic override, the normal server outfit remains active.

Wearable item views are resolved from the `CTFWearable` definition-index netvar.
Their econ container is not assumed to have the weapon layout. Native
`InitializeAsClientEntity` sets the client-side flag without a private byte write.

Server cosmetic entities are temporarily hidden, unequipped locally and removed
from the player's wearable list. Selected real inventory cosmetics and virtual
items are then displayed together as client wearables. Selecting the same item
in multiple slots displays it once. Real selections copy the original item view;
virtual selections retain their paint/style/unusual handling. HUD carried items
are filtered and deduplicated as well.

Unequipping an original removes its attribute provision and invalidates the
player's attribute cache. Removing Pyrovision Goggles therefore removes their
vision contribution; selected virtual goggles contribute it normally. Vision
from weapons, spectator settings or game rules is not forcibly cleared.
The engine's normal vision-change handling updates the scene on its next think.

Native weapon wearables, including shields, Gunboats, Mantreads, Razorback and
weapon attachments, are excluded using the item's cosmetic schema slot.
Repeated server updates are reconciled without accumulating more client copies.
Disabling the changer, death, class/team changes and unload remove client models
and restore surviving native entities, attribute providers and their previous
visibility. An entity transferred to a different owner is never taken back.

This reconciles the client presentation and attribute providers. It does not
delete owned items or send fabricated cosmetic equipment to the server.

Implementation follows Valve's [wearable Equip/UnEquip lifecycle](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/econ/econ_wearable.cpp)
and [attribute provider cache invalidation](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/econ/attribute_manager.cpp).

Run `python3 tests/inventory_wearables_test.py`. The fixture compiles the actual
production reconciliation methods and checks real + virtual outfits, Pyrovision
removal/restoration, slot boundaries, repeated frames, server reinsertion,
failed creation/retry, ownership transfers and stale handles. Actual particles,
bodygroups and map-material transitions still require an in-game check.
