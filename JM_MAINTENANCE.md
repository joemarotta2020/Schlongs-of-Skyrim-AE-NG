# JM SOS AE-NG fork maintenance

This fork keeps the upstream SOS AE-NG code plus the JM DRIT corpse-presentation compatibility patch.

## JM patch surface

Most custom logic is isolated in:

- `src/JM_DRITCompat.cpp`
- `src/JM_DRITCompat.h`
- `papyrus/JM_SOS_DRIT_API.psc`

Only two upstream implementation files carry small integration hooks:

- `src/main.cpp` — registers the native Papyrus bridge.
- `src/Schlongification.cpp` — rejects delayed `SOS_Underwear` equips on dead actors.

The native helper is corpse-only. It does not modify living NPC underwear behavior and does not write `SOS_NPC_Override.JSON`.

## DLL builds

`.github/workflows/build.yml` builds the Windows x64 DLL and uploads a deployable artifact named **JM-SOS-AE-NG-DLL**.

CommonLibSSE-NG is pinned to commit:
`39f9d07a6ffabea8fb559eee87ab7d27cd463e8a` (10.1.0).

The workflow runs on:
- pushes to `main`
- pull requests
- manual dispatch

## Upstream updates

`.github/workflows/upstream-sync.yml` checks the original repository weekly and can also be run manually.

When upstream changes:
1. It merges upstream `main` into `jm/upstream-sync`.
2. It opens or refreshes a PR against this fork's `main`.
3. The normal DLL build runs against the PR.
4. Review the two hook files if upstream changed them.
5. Resolve conflicts without deleting the JM compatibility calls.
6. Merge only after the DLL build passes.

If the automated merge itself conflicts, the sync workflow fails intentionally. That is the signal to manually reconcile upstream changes with the JM hook rather than blindly replacing working code.

## DRIT side

DRIT calls:
`JM_SOS_DRIT_API.EnsureCorpseSchlong(actor, true)`

The matching `JM_SOS_DRIT_API.psc` declaration is included here for compiling the DRIT Papyrus source.
