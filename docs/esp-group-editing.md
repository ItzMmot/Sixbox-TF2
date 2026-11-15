# ESP group editing while connected

The reported failure happens when creating a group while already on a map:
Active groups shows corrupt/question-mark labels and the game can crash.

Two raw-pointer lifetimes crossed the editor/runtime boundary. Menu entries
borrowed `std::string::c_str()` values before the create button could relocate
the group's vector in the same frame. Entity/model caches, and Chams material
pointers retained for later rendering, also pointed into that mutable vector.

Dropdown names now have separate owned string storage and refresh after a
same-frame create. Runtime groups come from a published copy that the editor
never mutates. Publication runs under the config/bind mutex after menu edits,
config/visual loads, rollbacks and resets. A publication allocates only when
values actually change. The game acquires the published snapshot and active
mask at the next group Store; acquisition and unchanged publication do not
allocate. Chams pins the owner of its material-style pointers until those draw
records are cleared. Aura indexes the same runtime snapshot as group lookup.

Moving a group uses checked, allocation-free rotation. The dropdown uses
unsigned mask shifts, including group 32. User profiles and group order/masks
retain their existing representation; no automatic config edits are needed.

`tests/group_snapshots_test.cpp` exercises short and long labels, same-frame
vector relocation, adding/deleting/reordering/renaming, old Chams-style pointer
ownership, empty reload/reset, the 32-group bound, concurrent publication/reads,
and allocation-free unchanged publication/acquisition/label refresh. It uses
the exact storage/label helpers used by the client. Native gameplay still needs
testing: create groups on a loaded map, then rename, duplicate, move, delete and
reload a profile with ESP, Chams, Glow and Aura enabled.
