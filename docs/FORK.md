# PGRazor_Pokematrix fork

This fork keeps its changes as commits on top of the upstream project
(`jpitty03/PGRazor`). To take an upstream update, replay those commits on the
new upstream code; never merge upstream into the fork by hand-editing.

Remotes: `origin` = upstream `jpitty03/PGRazor`, `fork` = `hugoalves6/PGRazor_Pokematrix`.

## Taking an upstream update

```bash
git fetch origin --tags
git switch main
git rebase origin/main          # replays the fork commits on the new upstream
# conflicts: fix the file using the change list below, then
git add <file> && git rebase --continue
python -B tools/check.py
python -B pgrazor/build.py
git push --force-with-lease fork main
```

Version: upstream `vX.Y.Z` becomes fork `vX.Y.Z.N` (N starts at 1 and resets on
every new upstream version). Add the `CHANGELOG.md` entry and
`docs/releases/vX.Y.Z.N.md`, then build with `PGRAZOR_VERSION=vX.Y.Z.N`.

If upstream rewrote a file so much that a change no longer applies, drop that
hunk and re-add the feature by hand following its description here.

## Fork changes

Each item lists every place it touches, so it can be re-applied by hand.

1. **Four-part versions** (`v0.3.4.1`).
   `pgrazor/build.py` `release_version` regex and `.github/workflows/release.yml`
   tag check both accept an optional `(?:\.[0-9]+)?` after the third number.

2. **Compact Buddy Emotion badge** (`♥ 28 ★ ⏱11m`).
   `BuddyEmotionSettings`: field `compact` set in `publish`/`read`, getter
   `compactStatus()`. `Pad`: the `buddyEmotionStatus` view is single-line,
   wrap-content, shows `compactStatus()`; full `status()` goes to the content description.

3. **Smaller cooldown timer with the local time inside it.**
   `Pad`: `cooldownTimer` text size 11, layout 128x30 dp (42 dp when the clock
   is merged); in the heartbeat, when both cooldown and local-time overlays are
   on, the timer gets a second line (`RelativeSizeSpan(0.85f)`) from
   `LocalTimeOverlay.embeddedLabel()` and the separate clock is refreshed with
   `clockAllowed && !mergedClock`. `LocalTimeOverlay`: `embeddedLabel()` (1 s throttle).

4. **Gym feeder map icon switch in Settings > Gyms.**
   `GymAutomationSettings.setOverlayEnabled(boolean)`; `SettingsPanel` adds the
   "Show Gym feeder map icon" checkbox right after the Gym Slotter launch row.

5. **VGP status text toggle** (icon only).
   `Control`: `vgpStatusText` (default true, key `vgpStatusText`), getter/setter,
   load/save. `Pad`: `vgpActivity` visibility also requires `Control.vgpStatusText()`.
   `SettingsPanel`: "Show VGP status text" switch under the VGP overlay switch.
   `SettingsBackupRules`: key in `SETTINGS` and `BOOLEANS`.

6. **Double-tap: Walk / Teleport with "Don't ask again".**
   `Control`: `mapTapTeleportNoAsk` (key `mapTapTeleportNoAsk`), getter/setter, load/save.
   `TeleportDialog.confirmMapTap(...)` + `Remember` interface.
   `Pad.confirmMapTeleport`: mode 3 skips the dialog when remembered, else
   uses `confirmMapTap`; remembered Walk calls `Control.setMapTapMode(1)`.
   `SettingsPanel`: cycling "Game map tap" also clears the remembered choice.
   `SettingsBackupRules`: key in `SETTINGS` and `BOOLEANS`.

Conflict hot spots on upstream updates: `Pad.java` (setup line for the timer,
the heartbeat block), `Control.java` (settings load/save lines) and
`SettingsBackupRules.java` (the long key lists — re-add our two keys).
