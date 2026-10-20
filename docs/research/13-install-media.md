# Install media, patches, and source identity

OpenBattlefield2 reads an installed BF2 tree and its archives in place; it does
not need to ship game data. These notes record independently observed source
identities and package limits that can help reproduce asset checks. They do
not add support for opening these installers in OpenBattlefield2, and they do
not establish that a sample is an official or complete retail edition. No
installer or game asset is included here.

## InstallShield base sample

A Windows InstallShield 10.5 source set was inspected without running its
installer. The base revision and edition could not be established from the
available evidence. The three core CAB/HDR source files have these SHA-256
identities:

| File | SHA-256 |
| --- | --- |
| `data1.cab` | `6ac0645f56d4107b78c4f4c66658a19dff0630f72c0fc13ae86cf9f430f40181` |
| `data1.hdr` | `bf2ffc9c72c119c79639df5138c1a3bac2f7df78ae0adb9a124796e1665c5377` |
| `data2.cab` | `6061bbfa63ed6e6cb98d90dd3cedd1db80a94b85e40bd556449a59e6c94448dc` |

A read-only cabinet probe indexed 539 non-placeholder, group-qualified
members. A separate, explicit BF2 destination mapping exposed
`mods/bf2/levels/strike_at_karkand/server.zip`; the decoded ZIP is 2,741,542
bytes with SHA-256
`aeeaa2633da2bc347a5654e063c0353a40c83eba4c76b2c287238a1427116867`. Its
50 members loaded the 32-player `gpm_cq` layer, with 7 control points and 37
spawn points. Requested cabinet payloads passed their stored MD5 checks.

This is useful as a content-identity reference, not as proof that the whole
installation matches a particular patch level. Installer cabinet enumeration
and decoding alone also do not show that the original installer would produce
a particular installed tree.

## Patch 1.41

The observed `bf2_patch_1.41.exe` has SHA-256
`7bfd6236b67c0380acb522d237273d51a2764769a8f9c223098f93350bdd5c64`. Static
range inspection found separate `PATCH.RTD` and `PATCH.RTP` records for `bf2`,
`bp1`, `bp2`, and `xp1`, alongside setup CAB/HDR data. The BF2 RTP payload
starts at byte offset 601,427 and is 510,227,636 bytes. Its header identifies
RTPatch 3.20. The RTD includes `MODIFY`, `ADD`, and `DELETE` operations,
history variants, and `Ignore Missing Files: ON`.

This is a version-aware binary update, not a directory of replacement files.
Correct application needs the matching base files and RTPatch history and
deletion semantics. The package was not applied, and no base precondition or
output hashes were established. A filename or embedded version label is not
evidence that a tree has been updated successfully.

## Patch 1.50

The observed `bf2patch150.exe` has SHA-256
`c5b150c96916a4c642daff03da3e9e52a7bccec6a7d6c6492494b57733c77977`. It
contains embedded InstallShield data rather than an Inno payload: `data1.cab`
starts at byte 113,194 (2,909,281 bytes), `data1.hdr` at 3,022,515 (61,426
bytes), and `data2.cab` at 3,083,986 (2,063,171,934 bytes). The header describes
381 entries, including BF2/xpack assets, levels, localization, binaries,
support files, and setup placeholders.

Those cabinet ranges can be indexed, but that does not reconstruct the update.
Its installer behavior, required starting revision, and resulting file hashes
remain unknown. The 1.41 → 1.50 sequence was not applied or validated. Do not
infer a successfully patched 1.50 installation from the presence of either
patch executable.

## Optional mod package observations

The separately observed AIX 2.0 CORE package
`AIX_2.0_CORE_MOD.exe` has SHA-256
`8e77137a8ae3ceba25d8027298144f155cefb35f2e0c572eab9c765413c78c68`. It is an
NSIS 2 non-solid deflate package. Static destination analysis indexed 3,359
files; selected reads succeeded for `mods/aix2/ClientArchives.con` and
`mods/aix2/AI/AIDefault.ai`. The separate core-maps and hotfix packages were
not applied. These observations establish access to selected assets, not full
mod installation, native engine bindings, or compatibility with an official
1.50 tree.

Standalone Forgotten Hope 2 2.45 and 2.57 packages were observed as 7z SFX
sets with numbered `.fh2data` members. Their destination mapping was not
established, and those packages were not declared supported. A bundled
`mods/fh2` namespace in another combined distribution does not prove its exact
release or that it is compatible with a separate package.

## Evidence limits

The reported hashes identify the examined files. The checks used static byte
ranges, cabinet/member reads, decoded ZIP entries, and map-layer counts; no
installer was executed. No REA Evidence ID was produced for the patch follow-up:
the PE-resource scanner's 512 MiB input limit excluded the 1.41 executable, so
its conclusions come from bounded direct range inspection. Full patch
application, exact edition provenance, and equivalence across complete
installations remain unverified.
