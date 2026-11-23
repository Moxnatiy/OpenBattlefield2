# The level's reverb and our weapon's sounds

Code: `src/app/src/environment_audio.cpp`, `src/app/src/combat_feedback.cpp`.
Test: `test_environment_audio`.

## The game data

* A level names its environment in its `Sounds.con`. Gulf of Oman's
  (`Levels/Gulf_of_Oman/server.zip:Sounds.con`, line 3) reads
  `sound.setReverb "common/sound/levelambients/City.eax"`.
* The `.eax` file is EAX's reverb property set as text
  (`Common_client.zip:Sound/LevelAmbients/City.eax`):
  * the header `EAX ENVIRONMENT PROPERTIES`;
  * then each field's name and value on lines of their own: `dwEnvironment`,
    `flEnvironmentSize`, `flEnvironmentDiffusion`, `lRoom`, `lRoomHF`, `lRoomLF`,
    `flDecayTime`, `flDecayHFRatio`, `flDecayLFRatio`, `lReflections`,
    `flReflectionsDelay`, `vReflectionsPan` (three values), `lReverb`,
    `flReverbDelay`, `vReverbPan` (three), `flEchoTime`, `flEchoDepth`,
    `flModulationTime`, `flModulationDepth`, `flAirAbsorptionHF`,
    `flHFReference`, `flLFReference`, `flRoomRolloffFactor`, `dwFlags`;
  * the `l…` fields and `flAirAbsorptionHF` are millibels.
* A `Sound` template sends to the reverb by its `reverbLevel` (`1` on the grenade's,
  sound-template.md).

## The original's path

The original's sounds go through BF2Audio.dll to its own OpenAL, BF2OpenAL.dll,
with EAX (sound-template.md, "OpenAL's source parameters").

What is **not opened**:

* the client's handler of `sound.setReverb`;
* how BF2OpenAL renders the EAX environment.

## Ours

`EnvironmentAudio` reads the preset and renders it through **OpenAL Soft**.

* The device is OpenAL Soft's loopback device, with the EFX EAX-reverb effect.
* Each field maps to its EFX counterpart: millibels to gain as 10^(mB / 2000),
  and the environment size to density as size³ / 16. Both are OpenAL Soft's own
  EAX mapping (`al/effects/reverb.cpp`).
* `dwFlags` bit 5 is the decay-HF limit.
* `dwEnvironment` is not used.
* A sample is rendered once per send level, its decay tail included, and kept.
* Ours, **not measured**: the output, 16-bit stereo at 48000 Hz.

**Licence.** OpenAL Soft is **LGPL-2.1** and is linked **statically**
(`cmake/openal-soft.cmake`, release 1.24.3 pinned by SHA-256). The repository
itself is MIT. A binary distributed with OpenAL Soft linked statically must let
its users relink it against a modified OpenAL Soft: ship the object files or the
sources with it, or build OpenAL Soft as a shared library instead.

`CombatFeedback` plays our own weapon's sounds. They are the weapon's `Sound`
children.

* The fire sound is a child whose template name holds `fire1p`, played when a
  round leaves (`FirstPersonView::onAction` answers true).
* `reload1p` and `switchfirerate` are ready but not called yet.
* The name match is the data's naming, not a rule read in the binary.
* The volume is the template's `volume`, and the reverb send its `reverbLevel`.
* The sounds have no position, so they are 2-D at priority 2 (BF2Audio.dll
  0x10005510). They claim from the world view's one pool of sources
  (`SoundVoices`, sound-template.md), re-queued as 0x10006ba0 re-queues a
  stolen sample.
* Ours, **not measured**:
  * the 1 MB cap on a decoded sample;
  * the 12 streams allowed when there is no pool.

## Not done

* The other soldiers' weapons (`fire3p`, with a position).
* The reload's staged sounds.
* Footsteps and the soldier's own sounds.
* The reverb on the world view's own sounds (`SmokeFlare`, the vehicles).
