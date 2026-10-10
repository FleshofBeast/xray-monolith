# Native world simulation rework

The requested model is a shared session with locally controlled players, native
client simulation, personal quests/rewards, and personal corpse/container loot.
The host supplies shared entity identities, placement, behavior and anomaly
placement. Existing location workers remain excluded.

## TES3MP source audit

The inspected 0.8.1 sources distinguish LocalActor from DedicatedActor. Cell
publishes position, animation flags/playback, speech, death, dynamic stats,
equipment, attacks, casts and cell changes. DedicatedActor advances movement and
animation flags locally and applies dynamic stats. Its AI handler invokes native
travel, wander and target-dependent AI packages. Authority is assigned explicitly;
running the same functions independently is not the synchronization mechanism.

Primary references:

- [Cell update and receive paths](https://github.com/TES3MP/TES3MP/blob/0.8.1/apps/openmw/mwmp/Cell.cpp)
- [LocalActor change capture](https://github.com/TES3MP/TES3MP/blob/0.8.1/apps/openmw/mwmp/LocalActor.cpp)
- [DedicatedActor native playback and AI instructions](https://github.com/TES3MP/TES3MP/blob/0.8.1/apps/openmw/mwmp/DedicatedActor.cpp)

## X-Ray gaps confirmed in the current release

- update_world_replica bypasses virtual UpdateCL, disables character physics and
  advances skeletal tracks through a substitute path.
- schedule_world_replica bypasses native NPC/mutant scheduled behavior.
- Native NPC binders, level scripts, task updates, local hits and inventory
  operations are suppressed or redirected to host transactions.
- Host quest snapshots replace the guest task/info registry; host inventory
  snapshots repeatedly replace guest gear, money and reputation.
- The standalone and fixture checks do not establish complete rendered NPC
  behavior, natural combat, interaction or personal quest progression.

## Development implementation

Confirmed gameplay policy: NPCs and mutants share host-coordinated positions,
behavior and deaths. Each character has separate corpse and container loot,
quests and rewards. Native guest playback must not make conflicting AI decisions.

The first native-world prototype reached connected native stalker frame updates
but crashed in CSoundMemoryManager::update on the guest. This test failed;
restoring native update calls alone is not sufficient and must not be released.

The -coop_native_world development flag retains canonical joined-world identity
but separates it from the passive simulation policy. It restores native frame,
script, hit, quest and inventory operations. Shared NPC scheduled planners remain
host-owned; anomaly and player schedules run locally. The initial character
inventory is imported once per actor incarnation; later host views do not remove
new local loot or overwrite personal money/progress. Personal character capture
reads actual native inventory. Host quest updates are ignored in this mode.
Quest packets arriving before baseline loading are ignored as well. A completed
inventory bootstrap is tracked separately from its revision, so asynchronous
item spawning cannot prematurely stop after changing actor incarnation.

This path is deliberately unpublished while integration continues. It is not a
finished TES3MP equivalent. Required work before making it the normal join path:

- Capture/restore the player's complete native quest and script state before a
  host baseline is loaded; persist it with the character and across travel.
- Relay client-owned health, equipment and hit results to shared representations.
- Apply host behavior instructions without competing independent NPC decisions;
  interpolate shared poses through the native movement/animation systems.
- Preserve per-character corpse/container contents and claims across rejoin,
  world restarts and location changes.
- Validate natural firing, NPC/mutant combat, electrical damage, rendered
  animations, NPC dialogue, separate quests/rewards and independent looting with
  two native clients before replacing the public package.

## Development checkpoint validation (2026-10-10)

DX11 build passed (`_build/native-world-build7.log`) and all 20 standalone suites
passed (`_build/native-world-unit1.log`). Two 120-second native runs passed after
the lifecycle fixes. The first (`_build/native-world-prototype3.log`) completed
native stalker frames, matched all 56 NPCs including three dynamically spawned
mutants, and closed both clients without changing private source saves.

The expanded run (`_build/native-world-prototype4.log`) verified host-created
enemy spawn/death/removal on the guest, correlated all three events to the same
shared anchor, recreated a visible native trader, excluded host quest snapshots,
and shut down normally with unchanged private saves. These checks exercise
existing lifecycle replication, not guest-originated shared combat or persistent
personal quests/loot. Native frame completion does not establish rendered
animation quality or internet behavior.

Run `powershell -ExecutionPolicy Bypass -File .\test-coopnet-native-world.ps1`
after a DX11 CoopNet build. This uses isolated appdata and the explicit
`-coop_native_world` development flag. Installed clients and release assets have
not been replaced by this checkpoint.
