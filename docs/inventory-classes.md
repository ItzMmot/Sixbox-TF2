# Inventory Changer class filter

The **Classes** multi-select appears above the Backpack and Catalogue columns.
Choose any combination of Scout, Soldier, Pyro, Demoman, Heavy, Engineer, Medic,
Sniper and Spy. Multiple classes form a union: a shared weapon/cosmetic appears
if any selected class can use it. **All-class** alone selects items usable by
all nine classes. **Tools / other** includes items without a class assignment.
Selecting nothing shows no items; selecting everything preserves the full list.

Class availability comes from the definition's `used_by_classes` schema data,
including numeric flags, named slots such as `primary`/`secondary`, and
case-insensitive class names.
Reskins without an explicit mask can inherit their canonical weapon's mask;
item-specific tools can inherit their target item's mask. Localized display
names do not decide class ownership. Catalogue metadata is built once per schema
revision, rather than rediscovered for each filter pass.

The filter applies to both columns and combines with search and the existing
Weapons/Hats/Misc/Tools category selector. Changing classes clears only the menu
selection, not added items or equipment. The filter is a saved, non-bind visual
setting in main/visual configs. It does not change skin-profile inventory data.

Filtered lists invalidate on class selection, schema/catalogue revision and
actual owned-item identities, including same-size inventory replacements.
`tests/inventory_classes_test.cpp` covers single/multiple classes, all-class and
unassigned items, empty/all selections, schema-key reading and union behavior.
