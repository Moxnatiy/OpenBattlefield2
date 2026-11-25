# M4 flash and decal investigation — Retail 1.50

Status: the decal geometry, placement and lifetime are in
(`app/decal_projection.h`, `decal_placement.h`, `decal_lifetime.h`). The renderer's
blended decal pass, the bullet decals that use it and the muzzle flash and
mesh-particle passes described below come with the renderer and effects changes.

The installed `BF2.exe` has SHA-256
`5a72e2396a441201a07b7a743643c04e750784673fdbc126a909ce2582c5f6a1`.
The copied `RendDX9.dll` has SHA-256
`66ec45717877706191fdb84e28452c3559c769310bb853a2495b5c296877780e`.
Both were checked against the installed files without changing them.
Local Ghidra 12.1.4 / REA evidence is under
`build/retail-captures/m4-effects-20261120/rea/`.

## Static bundle decal attachment, BF2.exe 0x628590

Evidence `ev_87601fecc6f7add649067ae5b00ce1023332a398669528f147f5b62dfccaaa6a`,
`static-decal-attachment.json`. Ghidra identifies 456 body bytes spread over
0x628590–0x628606, 0x62860a–0x62863b, 0x62863f–0x628658 and 0x62865c–0x628760.
The embedded assertion identifies `Game/Objects/Base/BundleTemplate.cpp`;
its text is “Can only add static decals to objects with collision”.
This is a static bundle-child path, **not a recovered bullet-impact dispatcher**.

| Observed field/interface | Use in this function |
| --- | --- |
| Owner +0x21c / +0x220 | Begin/end of a pointer sequence, walked one pointer at a time. Empty range returns. |
| Child record +4 | Name/reference input to global 0xa08f5c virtual +0x34 when the cached target is absent. |
| Child record +0x20 | Cached reference holder; its +4 supplies the resolved target. Resolution has success/failure reference-holder replacement branches and releases displaced/temporary holders by decrementing their first word. Exact holder type and failure sentinel are not established. |
| Resolved target virtual +0x10 | Tested twice against numeric ID 0x9487, after rejecting null and the owner itself. Exact interface/class binding is not established. |
| Owner +0xdc, bit 0x04 | If absent for an eligible child, emits the collision assertion (source line 124 / 0x7c). Diagnostic continuation behavior is not established. |
| Input object +0x28 | Next link walked while looking for an interface. Its precise relationship (parent/owner/etc.) is not established. |
| Object virtual +0x0c, argument 0xc4c5 | Interface lookup on each link. Null result advances to next link; reaching null ends the search. |
| Found interface virtual +0x118 | Receives the original input object. |
| Found interface virtual +0x120 | Receives child record +0x28. This payload is forwarded without being decoded here; its complete layout is not established. The first successful interface stops the chain walk. |

Every child is considered, then the function returns without a result. The
reference-holder release helper at 0x402d10 is marked nonreturning by Ghidra;
that classification is not independently verified and must not be used to
discard the rest of the function's control flow. All unresolved virtual calls
and type identities remain unresolved. No projectile material pair, atlas
choice, dynamic mark budget, fade or clipping algorithm is recovered here.

## Renderer console registrations, 0x100bca46 and 0x100bcb4a

`decal-normal-offset-registration.json`, evidence
`ev_f4403f24b6825bbc68aba1e21953dcc5bee353f7115e79b1591814617da34ae2`,
and `decal-clip-angle-registration.json`, evidence
`ev_04c9c6558f8125e0d72a8fc9700c8bb6d709a059f10bd3c33c3bb5ca4d804e80`.
Each function is 113 bytes and has no conditional branch in the pseudocode.
They construct console descriptors, not clipping/offset algorithms.

All direct descriptor writes: +0 vtable (0x10212f38 for normal offset,
0x10212fd0 for clip angle); +4 = 1; +8 copies global 0x10247c24; +0xc
is the corresponding command-name string; +0x10 copies 0x10247c28; byte +0x14
copies the low byte of 0x10247c2c; +0x18 = 0; +0x1c = 1; +0x4c and +0x50
contain `float` type strings; byte +0xa0 = 0, byte +0xa1 = 1; +0x78 receives
the result of 0x100520e0. Helper 0x100b25ec is also called. Exact descriptor
field semantics and helper effects are not established. Both return after
initialization; neither exposes a numerical offset or clip-angle default.

The first 128 bytes of the two descriptor vtables were compared in
`normal-offset-vtable.json` and `clip-angle-vtable.json`. This does not prove
the complete vtable boundary. Only slots +0x5c and +0x64 differ in that range:
normal offset points to 0x100bcb04 / 0x100bcab7; clip angle points to
0x100bcc08 / 0x100bcbbb. The +0x5c targets were not recognized as function
entries by the current Ghidra analysis; their `analyze_function` records
contain errors, not usable behavior evidence.

The +0x64 routines were recovered completely (`normal-offset-callback-64.json`,
evidence `ev_d7e49206e81caa6e2404a6ae302f6ee9e5c25c2b5081c0a275b3d79f3917ed8d`;
`clip-angle-callback-64.json`, evidence
`ev_32ef780f2d7182b8f48ca70620e27b604b524a03a42cca7a97a609ac38e0e7a9`).
Each conditionally extracts a float from the supplied standard stream when
argument count is 1, into scratch global **0x10258930** (normal) or
**0x10258934** (clip). Regardless of count it writes the corresponding global
address into descriptor **+0x24**, then returns. No explicit extraction-error
branch is present. Both globals have zero initial bytes in the file, but their
only recovered xrefs belong to these argument-parsing routines. They are **not
established renderer-state values or defaults**. Do not use their initial zero
as a clipping or normal-offset correction.

## Decal renderer initialization, RendDX9.dll 0x101aa570

`decal-renderer-initialization.json`, evidence
`ev_1d6039397b73e3b7b145e6ad6e14758c7af2bab9fb295f7a13f8a1eb571fe0d3`.
The function is 1169 bytes, with no conditional branch in its pseudocode;
caller 0x101257c0. It initializes state, requests the shader path
`mods/xpack/shaders/Decals`, creates a resource labelled `DecalRenderer`,
and caches named lookup results. No per-mark lifetime/eviction loop occurs here.

Complete direct-write inventory (offsets from the object):

| Offset(s) | Initialization / meaning established here |
| --- | --- |
| +4, +8, +0xc | Zero; purposes not established. |
| Byte +0x14 | Zero; purpose not established. |
| Dwords +0x18..+0x6c, step 4 | Initially zero. +0x30, +0x44, +0x58, +0x6c become float 1.0, an identity-matrix pattern at +0x30. |
| +0x70, +0x74, +0x78 | Zero; purposes not established. |
| +0x7c | Bits 0x43480000 = float 200.0. Consumer and units not established; **do not label this a fade time**. |
| Byte +0x90 | One; purpose not established. |
| +0x98 | Shader-path helper 0x100230e0 result. |
| +0x94 | Helper 0x10011e90 result, labelled `DecalRenderer`, with literal arguments 0x3000 and 0x2c. Exact resource API/units not established. |
| Dwords +0x118..+0x1b4, step 4 | Zero; complete array/block purpose not established. |
| +0x1b8 | Lookup `DecalFadeDistanceAndInterval`. |
| +0x1bc | `InstanceTransformations`. |
| +0x1c0, +0x1c4, +0x1c8 | `ShadowTransformations`, `ShadowTrapezoidMats`, `ShadowViewPortMaps`. |
| +0x1cc, +0x1d0, +0x1d4 | `AmbientColor`, `SunColor`, `SunDirection`. |
| +0x1d8 | `WorldViewProjection`. |
| +0x1dc, +0x1e0 | `TEXLAYER0`, `ShadowMapTex`. |
| +0x1e4, +0x1e8 | `fogRange`, `fogColor`. |
| +0x1ec, +0x1f0 | `WaterFogColor`, `WaterFogRange`. |
| +0x1f4, +0x1f8 | `CameraToWater`, `WorldSpaceCamPos`. |

The named lookups use helper 0x1001e5d0, followed by temporary-string cleanup.
Helper 0x10011250 is also invoked; its complete side effects are not decoded.
Fields not explicitly written by this function are not assigned inferred
meanings. Runtime parameter uploads and per-mark alpha remain **not measured**.

## Renderer teardown, RendDX9.dll 0x101aaa10

`decal-renderer-followup-101aaa10.json`, evidence
`ev_d417dccb4f05e78b53d2ed2f215f60cc2151888faac55729cd6ea530c82587d8`.
367 body bytes, caller 0x10125650. For every nonnull cached lookup at
+0x1b8..+0x1f8 (step 4), it calls virtual +8; it also does so for nonnull +0x98.
For nonnull +0x94 it decrements that referenced object's +4, and on zero calls
0x100146e0 followed by 0x1005fa30 with the object. For nonzero owner +4 it calls
0x101ac130 with the owner, then 0x1005fa30 with that stored pointer. Finally
owner +4/+8/+0xc are cleared and the function returns. Release/free meanings
are inferred from this pattern; concrete helper APIs are not bound. Ghidra's
nonreturning helper annotations are not independently verified. This is
resource teardown, **not a per-mark eviction or fade policy**.

## Renderer batch submission, RendDX9.dll 0x101aab90

`decal-renderer-followup-101aab90.json`, evidence
`ev_5ae0cdce95106c52412f5368c6e3a609748eeaec5929ea9ac1bcf00216c01f0c`.
445 body bytes; callers 0x101246e0 and 0x10124b80. Returns 0 unless enabled
byte +0x90 is nonzero, queue begin +4 is nonnull, and `(end +8 - begin)/0x30`
is nonzero. On that path:

1. Input-interface virtual +0x6c, then +0x68, supply arguments to 0x10078120.
   These interface methods' concrete identities are not established.
2. Copies global-object pointer 0x1024b908 fields **+0x2b0 / +0x2b4** into
   renderer **+0x70 / +0x74**, then calls setup helper 0x100265d0. The two
   copied values are passed to cached `DecalFadeDistanceAndInterval` (+0x1b8)
   virtual **+0x18**, with trailing argument 0. This establishes their binding
   to that shader parameter, not their runtime numerical defaults.
3. Global interface 0x1023b984 virtual +0x1c receives state pairs
   (0xc3, value at 0x1023ad9c), then (0xaf, value at 0x1023ad98).
   Their concrete API/state meanings are not established.
4. Clears counters +0x18/+0x1c, calls 0x101ac8c0 with queue begin/end, and
   traverses **0x30-byte entries**. Entry **+4** is a grouping key. Previous key
   starts at sentinel **3**. On a key change, calls 0x1001d990 if the old key
   was not 3, updates the key, then calls 0x101ab1f0 with the new key.
   Every entry calls 0x101aa380 with the original input interface and entry
   pointer. After traversal, flush helper 0x1001d990 is called if final key is
   not 3. Group-key meanings and helper effects are not established here.
5. If queue begin remains nonnull, calls cleanup 0x101ac130, then 0x1005fa30
   on the stored begin pointer. Clears +4/+8/+0xc, calls 0x100265e0 and returns 1.

This clears a submission queue after a successful render path. It **does not
establish permanent decal eviction** or the lifetime of the source objects.
Ghidra's nonreturning cleanup annotation is not independently verified.

## Submission entry consumer, RendDX9.dll 0x101aa380

`decal-queue-entry-consumer.json`, evidence
`ev_157d4265c7ecf75b1f389976150ebb06205581af4c23f4754eb7c6987e78111b`.
485 bytes, called by 0x101aab90. A null/empty entry pointer list returns 1.
Otherwise it calls 0x101aa080. Group key 0 performs no additional helper;
key 1 calls 0x101aa120; key 2 calls 0x101aa240; other nonzero keys perform
neither of those extra calls.

The 0x30-byte submission entry fields read here are: +0 requested output
count; +4 group key; +8 texture candidate; +0xc shadow texture candidate;
+0x24/+0x28 begin/end of a pointer list (count = byte difference / 4).
Other entry fields (+0x10..+0x20 and +0x2c) are not decoded here; helpers
receive the entry, so these are not claimed unused. A nonzero changed +0xc
is supplied to cached ShadowMapTex (+0x1e0) virtual +0x18 and saved in renderer
+0x1c. A nonzero changed +8 is supplied to TEXLAYER0 (+0x1dc) virtual +0x18
and saved in renderer +0x18. Null candidates leave those caches unchanged.

Renderer +0x9c is an output cursor. If cursor + requested count is strictly
less than **0x3000**, helper 0x10014800 receives the current cursor, requested
count and flag **0x1000**. Otherwise cursor is reset to zero and flag **0x2000**
is used. A null helper result returns 0. Concrete lock API/flag names are not
bound here. **This is a vertex-buffer capacity check, not a 12,288-mark budget.**

For each pointer in the entry list, source +0x50 is a vertex-data pointer,
+0x54 is the number of **0x14-byte** source vertices. A zero count skips that
source. Every copied output vertex is **0x2c bytes**, laid out as follows:

| Output offset | Source |
| --- | --- |
| +0/+4/+8 | Source vertex +0/+4/+8. |
| +0xc/+0x10/+0x14 | Source object +0x44/+0x48/+0x4c. |
| +0x18 | Source object +0x14. |
| +0x1c/+0x20 | Source vertex +0xc/+0x10. |
| +0x24 | Signed short at source object +0x26, converted to float. |
| +0x28 | Source object +0x28, copied unchanged. |

The last four output words are consistent with the shipped shader's packed
UV/index/alpha input; this semantic binding is an **inference** until the vertex
declaration is checked. In particular, the last word's source is identified,
but its writers and temporal interpretation remain **not measured**.
The renderer cursor increments once per output vertex. After copying, helper
0x10014860 is called. If any vertices were copied, helper 0x100057e0 receives
`(copiedVertexCount / 3, 4, originalCursor)`. Success returns 1. Remainders,
helper internals and source-object geometry construction are not inferred.

## Shipped decal shader — distance fade is distinct from temporal alpha

`Shaders_client.zip:Decals.fx` line 21 declares a two-component
`DecalFadeDistanceAndInterval`, default (100, 30). Lines 63–65 and 115–117
compute alpha as one minus saturated `(HPos.z - distance) / interval`, then
multiply by input `TexCoordsInstanceIndexAndAlpha.w`. This uses pre-divide
projected HPos.z; do not substitute Euclidean range without checking the
projection and upload path. Pixel shaders multiply sampled alpha by that
vertex alpha (lines 82 and 151). The instance index and UV share the other
components of the input attribute.

Technique passes enable alpha test GREATER with reference 0 and conventional
SRCALPHA / INVSRCALPHA blending (lines 177–185, 196–204, 214–222). This is
authored shader behavior. Which values override the defaults, how the input
alpha changes over time, clipping/projection on CPU, and mark eviction remain
**not measured**. A shader distance fade must not be described as a recovered
temporal fade policy.

## Renderer anchors requiring follow-up

The copied renderer contains the strings `decalNormalOffset` at 0x10212fbc,
`decalGeomClipAngle` at 0x10213054, and `DecalFadeDistanceAndInterval` at
0x102217c0. These are search anchors, **not established default values or
behavior**. `Geom/DecalSystem/DecalManager.cpp`, `DecalObject.cpp` and
`DecalTemplate.cpp` source-path strings also occur in this renderer.
Follow the Ghidra xrefs and record the complete relevant function before
implementing dynamic decal changes. Runtime slot selection, projection,
clipping, fade and eviction remain **not measured**.


## Capture constraints recovered from M4 data, 20 November 2026

`Objects_client.zip:Weapons/Handheld/USRIF_M4/AnimationSystem1p.inc:120–129`
connects a RandomTrigger containing `idle1`/`idle2` to an IdleTrigger with
`idleTime 5/15`. Lines 18–23 assign the authored idle clip lengths 2.9583 and
2.33333 seconds. The reload duration is 4.7 seconds at `USRIF_M4.tweak:33`.
This explains why an eight-second test wait can enter a fidget window after
reload; it is not evidence for changing animation or flash lifetime. Complete
source lines/hash are in `m4-animation-source.json`. Existing trigger/system
semantics are documented in `animation-system.md` (IdleTrigger Linux 0x6ce370;
AnimationSystem update BF2.exe 0x7f7390). First two OpenBF2 full M4 captures
overlap a fidget; new runs 3/4 use a five-second test wait and avoid the observed
fidget at the single shot. No engine behavior changed.

The existing object_info registry tool resolves `e_muzz_m4` at
`0/0.0151088/0.496909` under `usrif_m4`; retained `m4-registry.txt` includes the
complete listed child hierarchy. This checks registry data availability, not
runtime muzzle projection, scale, draw eligibility or visibility.

Video/input QPC bounds and audio identity/count are retained independently.
WASAPI packet continuity does not establish absolute synchronization: new M4
packets can be timestamped after their own delivery/poll QPC. The actual
OpenBF2 run 3 interval has 458/458 selected packets ahead by 59.790–79.759 ms.
Absolute audio/video alignment, audio onset relative to fire, and engine output
latency are **not measured**. Do not fit an audio delay from these timestamps.


## Decal geometry generation, RendDX9.dll 0x101ae450

Evidence `ev_4247e9b49f168e84e42419d6685ebbfd9c22956ecbfaa62dc18fc7b1214fad73`,
`rea-decal-object/decal-object.json`, 1,299 bytes in two ranges
0x101ae450–0x101ae735 and 0x101ae746–0x101ae972; caller 0x101ae980.
The assertion path names `Geom/DecalSystem/DecalObject.cpp`, source line 0x172,
“Failed to allocate geometry mem”. This generates per-decal geometry; it is
not a recovered lifetime/eviction update. All indirect interface identities
below remain unverified rather than assigned names from their likely use.

| Input/state | Complete observed use |
| --- | --- |
| Argument 1 +0x18 float | Rounded to an unsigned atlas frame value when the template's byte +0x88 and dword +0x94 are both nonzero. |
| Argument 1 +0x50 pointer / +0x54 count | Initially uses scratch vertex buffer 0x10265838 with count zero. Projection helper fills them. Zero count clears +0x50 and returns. Nonzero count allocates count×0x14 bytes, zero-initializes each five-dword vertex, copies scratch output, and returns. Allocation failure emits the diagnostic and performs string cleanup. |
| Argument 2 | A float transform: affine position uses rows/columns at indices 0..2,4..6,8..10 and translation 12..14. Its second basis column (4,5,6), negated, becomes the mesh-path projection direction. Precise origin/orientation construction is a caller question. |
| Argument 3 virtual +0x44/+0x4c | Two scalar values combine as angle `(v4c + random×v44) × float_at_1023add0`; cosine/sine rotate prototype x/z. Accessor/property identities and degree/radian constant are not independently verified here. |
| Argument 3 virtual +0x14/+0x0c | A second random scalar multiplies v14 once, then each prototype vertex uses `(v0c + randomOffset) × input argument 4` as its position scale. This is evidence for additive scale variation, but the accessor-to-authored-property mapping still requires verification. |
| Argument 3 +0x34/+0x38 | Base UV offsets. |
| Argument 3 byte +0x88, dwords +0x94/+0x98/+0x9c | Atlas-enable gates; divisor at +0x98; multiplier at +0x9c. Modulo of the rounded frame selects the U term. The remaining frame value multiplies +0x9c and V height. Signed-to-unsigned float conversion correction branches are retained in the dossier; no presumed usual atlas formula replaces them. |
| Argument 3 +0xa0/+0xa4 floats | UV frame width/height: prototype UV is mapped through `(uv+1)×dimension×0.5` then base/frame offset added. |
| Argument 4 | Input geometry scale above; later the same local is overwritten with renderer-state global 0x1024b908 +0x2c4 for the mesh projection helper. Field meaning remains unverified. |
| Arguments 5/6 | Optional interface/contact record. Both present selects mesh path; either absent selects the alternate lookup path. Contact low byte selects virtual calls; its +2 unsigned short is forwarded as a projection-helper argument. No full contact-record layout is claimed. |
| Argument 5 virtual +0x28 | Receives contact low byte; only exact result 1 enables the mesh projection call. The eligibility/material predicate is unverified. |
| Argument 5 virtual +0x40/+0x4c/+0x48 | Each receives the same low byte; their three results are forwarded to 0x101adab0, together with the interface/contact pointers. Types and mesh/material semantics remain unverified. |

Two calls to 0x1009f860 generate scalar values through
`(((signed(bits|0x80)>>7)+1)×2^-23)-1`. The RNG algorithm and population/distribution
are not recovered by this caller, so no new RNG implementation is inferred.
Four prototype vertices at 0x1023aef8, each 0x14 bytes, are rotated/scaled into
scratch 0x102654d0, UV-adjusted, then transformed into world positions. The
prototype bytes and the scalar at 0x1023add0 are not decoded in this pass.

After setup, 0x101af810 is called. A temporary global collection rooted at
0x10265674 is walked/released through 0x101aefe0 and 0x1005fa30, then its
+0/+4/+8 links reset to the sentinel. This is temporary geometry work, **not
persistent-mark eviction**. Ghidra marks 0x1005fa30 nonreturning; that classification
is not independently verified and must not erase later control flow.

If either optional mesh argument is missing, 0x101ad280 looks up a surface using
transform translation x/z and scratch 0x10265680. Success calls 0x101adab0 with
zero direction/offset and globals 0x1023af4c, 0x1023ae68, literal 0x12, and null
optional pointers; failure leaves count zero. If both are present, the exact-1
predicate above gates 0x101adab0 using the negated transform basis direction and
renderer +0x2c4. The called projection/traversal helper is documented below; its clipper contract
and hidden plane outputs remain unresolved.
All zero-geometry, success, allocation-failure, atlas-disabled and predicate-false
branches are accounted for here. No effects-quality threshold is recovered by
this function. Flat-quadding every collision plane is not established Retail
behavior; actual eligible face selection, clipping, normal offset, accessor
bindings, atlas initialization, fade and eviction remain **not measured**.


## Decal triangle traversal/output, RendDX9.dll 0x101adab0

Evidence `ev_a45e98e5593215faa49b8bb2d3e7e1f9c7b7e81ef81787a210e41d75843e7fc4`,
`rea-decal-object/decal-projection.json`, 1,379 bytes at 0x101adab0–0x101ae012.
Called by 0x101ae450 and recursively by itself. No strings. This is geometry
traversal/output, **not a persistent-mark budget or temporal fade routine**.

Complete interface/state inventory: input 1 is the triangle index; input 2 is
the three-float projection direction forwarded by its caller; input 3 is the
original four-vertex candidate (five dwords per vertex); input 4 is the decal
instance (+0x50 output pointer, +0x54 output vertex count). Inputs 5 and 9 are
forwarded scalar/geometry values with unresolved exact identities. Input 6 is
recursion depth. Input 7 is a position base indexed at 12-byte stride. Input 8
indexes 8-byte triangle records, whose first three unsigned shorts select the
positions; the remaining word is not decoded here. Inputs 10/11 are the optional
interface/contact record from the caller. Contact low byte and byte +1 are
forwarded to virtual +0x38 along with triangle index and an output-neighbor buffer.
The record's other fields are not read here.

The function registers the triangle through helpers 0x101af690 / 0x101af810,
then 0x101ad860 gates the rest of traversal. Failure returns without output or
recursion. On success, 0x101ad510 receives the original candidate, count 4,
scratch 0x10265520, and an output pointer. Its result supplies clipped count and
candidate pointer; >2 vertices permits triangle-fan emission (first vertex,
current edge vertex, next edge vertex), copying all five dwords per vertex.
Counts 1/2 have no fan. The result's low/high-short decompiler locals and sentinel
−1 are retained in the dossier; the complete clipper/outcode sections below
recover its count/pointer and failure contract.

Global byte 0x1023af48 chooses two emission paths. When nonzero, 0x101aef20 is
called with the three receiver positions, followed by 0x100f9e60, and the emitted
positions follow an additional plane-projection calculation. The pseudocode
shows zero normal components and an apparently uninitialized `local_48`.
The later “Traversal plane binding resolved” section recovers its hidden outputs
and actual orthogonal projection arithmetic from the retained assembly. Do not
port the pseudocode zeros. The other path copies the clipped positions directly.
Both paths add `−renderer_state[+0x2c0] × direction` to every output position,
where renderer state is global pointer 0x1024b908. This locates a positional
separation consumer, but does not independently bind the field to a console
setting/default. UV components are copied with each position.

After each emitted fan triangle, if output count is **greater than 0x7f**, the
instance +0x54 count is reset to zero and the function returns. This is a
per-decal **output-vertex limit**, not evidence for 128 persistent marks.
Otherwise fan emission continues. No geometry is allocated here; the caller
allocates after traversal.

Recursion occurs only when depth>0, clipped pointer differs from original input,
and clip result is not −1. With either optional interface/contact absent, three
signed-short neighbors come from global table 0x1023adf8 + triangle×6. With both
present, virtual +0x38 must succeed before its three output neighbors are used.
Each neighbor equal to −1 is skipped. For every other neighbor, 0x101af210 checks
the temporary visitation collection against sentinel 0x10265674; only a not-yet
registered entry recurses with depth−1 and unchanged original candidate. The
interface-failure path returns; all three loops are bounded at three neighbors.
No per-mark age, eviction queue, effects-quality threshold or surface-material
name is established. The angle gate at 0x101ad860 is documented below; clipper 0x101ad510 and
hidden plane output bindings remain unresolved. Correct runtime face eligibility
and projection are **not measured**.


## Triangle angle gate / clip planes, RendDX9.dll 0x101ad860

Evidence `ev_57572c45e8637898e0b5c6bc1c117e79ae9b00390b83372ae4a8205e921e2fc1`,
`rea-decal-object/decal-face-predicate.json`, 589 bytes at 0x101ad860–0x101adaac;
sole caller 0x101adab0. No strings. All direct input reads are accounted for:
input 1 triangle index; input 2 three floats for the supplied projection direction;
input 3 angle threshold; input 4 position base with 12-byte stride; input 5
8-byte triangle records whose offsets +0/+2/+4 are the three ushort indices.
The remaining triangle word is not read. This function does not read a material
ID, effects-quality selector, persistent-mark count or age.

It resolves the three positions, calls 0x101aef20, and uses resulting local
vector components in a dot product with input 2. When input 3>0, calls to
0x1000ebe0 pass bounds −1/+1, followed by `_CIacos`; if the result exceeds
`input 3 × [0x1023add0]`, the function returns 0. The helper's clamp identity is
independently recovered in the clamp-helper section below. Read-only
`angle-unit-bytes.json` verifies bytes **35 fa 8e 3c**, float
**0.01745329238474369**, so the compared threshold is in degrees converted to
radians. Input3≤0 skips the angle gate. Caller 0x101ae450 forwards renderer state
+0x2c4 here; no default threshold value is recovered.

On the accepted path, it constructs three planes from the oriented receiver
edges (v1−v0, v2−v1, v0−v2) crossed with the face-vector components. Each vector
passes through 0x10077e90, then is copied into a four-float plane with constant
term `−dot(vector, edgeStart)`. The three planes are at local offsets −0x5c,
−0x4c and −0x3c; all four components of each are set. Each then passes to
0x100fae90. Helper normalization and plane-array output binding are recovered in
the later plane/normalizer/plane-list sections; the original pseudocode omitted ECX.
Accepted path returns 1. There are no other conditional branches. The gate's
existence and angular units are recovered, but front/back orientation, plane
output binding, runtime clip threshold/default and individual Retail face
eligibility remain **not measured**; no clipping implementation is ported from
uninitialized-looking decompiler locals.


## Candidate polygon clipper, RendDX9.dll 0x101ad510

Evidence `ev_883fe959691f9405624fa60f9429c1364f245c5b352c732737cc02c64af3c1e7`,
`rea-decal-clipper/decal-clipper.json`, 825 bytes in ranges
0x101ad510–0x101ad55c, 0x101ad560–0x101ad5a8, 0x101ad5b0–0x101ad852.
Sole caller 0x101adab0; sole called helper 0x101af1a0. No strings. This
clips a candidate polygon against a plane set; it has no material, quality,
mark lifetime or eviction state.

Complete inputs/state: input 1 points to five-float vertices **x/y/z/u/v**, input
2 is vertex count, input 3 has plane begin/end pointers at **+4/+8**, input 4
receives the result vertex pointer. Each plane is four floats at **16-byte**
stride. An absent/empty plane range immediately returns the original pointer and
count. Otherwise each original vertex gets a byte outcode from 0x101af1a0.
Outcode buffers alternate at **0x102654c0 / 0x102654c8**. Vertex scratch buffers
alternate at **0x10265530 / 0x102655d0**, 0xa0 bytes apart (eight 20-byte vertices).
The caller supplies four candidate vertices and three receiver-edge planes;
there is no independent arbitrary-polygon capacity guarantee in this helper.

It ORs the tested per-plane bits over every original vertex. If that aggregate
is zero, it returns the unchanged original pointer/count. Otherwise it walks
every plane in order, with a byte mask from `1 << (planeIndex & 31)`; current
vertex is retained iff its outcode bit is zero. The next vertex index wraps
modulo the current count. Whenever current/next bits differ, it emits the edge
intersection. For position p, next q and plane (n,d), denominator is
`dot(q−p,n)`; if **abs(denominator)<1.1920929e−7** it returns **0xffffffff**
immediately. This is the recovered failure sentinel, not a clipped vertex count.
That error path does not assign the output pointer. Otherwise
`t=−(dot(p,n)+d)/denominator`, and all five components interpolate as
`p+t×(q−p)`, including UV. Every generated intersection receives a fresh outcode
from 0x101af1a0. Input coordinates are not otherwise modified.

After each plane it switches both scratch buffers and uses that plane's output
as the next input; zero vertices naturally stay zero through remaining planes.
After the last plane it writes the current result pointer and returns count.
The original pointer is retained on trivial acceptance, while clipped output
uses shared scratch. Return −1 suppresses emission and neighbor traversal in
0x101adab0. There is no guessed epsilon, UV reset, extra inflation or silent
clamping in these notes. Outcode sign/boundary semantics and its register calling
convention are recovered in 0x101af1a0 below; the receiver-plane/mesh binding and runtime
comparison remain **not measured**. The clipper itself is now recovered and
must not be re-investigated from scratch.


## Plane outcodes, RendDX9.dll 0x101af1a0

Evidence `ev_d84c7cfefc7b4b64a0e0df74fab74472ca85403802abf7face9b95281f3608ee`,
`rea-decal-clipper/decal-outcode.json`, 112 bytes at 0x101af1a0–0x101af20f.
Sole caller 0x101ad510; no callees or strings. This resolves the clipper's
previously implicit input convention: **ECX** points to three-float position,
**EDX** to the plane-range object. The latter's **+4/+8** are begin/end;
planes are four floats at 16-byte stride, x/y/z normal then constant term.

Starting from byte mask zero, each plane sets its bit iff
`dot(position,plane.xyz)+plane.w > 0`. Exact zero is inside; this helper adds
no tolerance or plane inflation. The bit expression is `1 << (index&31)` and
accumulates into a byte. A null begin pointer returns zero immediately; reaching
plane count returns the accumulated mask. No special material/quality check,
clamping, depth limit, randomization or temporal behavior is present. Callers
here supply three edge planes; behavior for more than eight effective mask
bits is not a supported general-purpose capacity claim. Together with
0x101ad510 this recovers the finite-input clipping primitive. Receiver-plane
construction/output binding and which render geometry receives the decal
remain **not measured**, so no world projection match is claimed.

## Decal insertion and ring recycling, RendDX9.dll 0x101ae980

Complete evidence `ev_01c50c788d7b50ba572708cd2e189c0a375f78a5a8e06ad8f25c3054669a8878`,
`rea-decal-receiver/decal-receiver-initialization.json`, 928 bytes in
0x101ae980–0x101aea09 and 0x101aea17–0x101aed2c. No resolved callers or strings;
11 direct callees plus unresolved virtual calls. This inserts an entry into an
owner's existing storage; the owner/template class names and authored bindings
remain unresolved. It does **not** establish a universal mark limit.

| Location (bytes) | Every observed use in this function |
| --- | --- |
| Renderer pointer global 0x1024b908, +0x2a6 (1) | Zero disables insertion and returns 0 before allocation. |
| Owner ECX +4 (1), +0x10 (4), +0xc (4) | If all are zero, save a boolean for the later renderer virtual +0x24 notification. +0xc is also entry count, decremented on recycling. |
| Owner +8 (4) | Unsigned limit compared with count +1. When limit <= count +1, recycle the entry at +0x28[+0x3c]. Exact initialization and effective mark capacity are not recovered. |
| Owner +0x1a (1), +0x1c (4) | When byte is zero, pointer non-null and template +0x20 byte zero, reject with 0. +0x1c also supplies virtual +0x84 transform input and the later parent-chain search. |
| Owner +0x20 (4) | Cached result of first nonzero chain-node +0x38; filled only when zero and +0x1c non-null. |
| Owner +0x28 (4) | Pointer-array base used for recycling; consumed slot is explicitly cleared. |
| Owner +0x30 (4) | If greater than 0xfe, reset to 0, then increment; its low byte becomes entry +0x1d for template +0x20 enabled. This is an identifier, not established atlas or eviction count. |
| Owner +0x3c/+0x40 (4 each) | Oldest-slot index advances as `(index + 1) & mask` when recycling. Mask initialization not recovered. |
| Renderer +0x290 (4) | Free-list head. Recycled entry is pushed via entry +0; new entry is popped via its +0. Decompiled null-head path still dereferences null later; do not invent an allocation fallback or valid empty-pool behavior. |
| Entry +0x50 (4) | Existing geometry pointer freed by 0x1005fa30 on recycling if nonzero; set to zero on acquisition. Ghidra incorrectly/non-independently marks this release helper nonreturning; continuation cannot be discarded. |
| Entry +0x34/+0x38 (4 each) | +0x34 set to zero; +0x38 stores float returned by renderer virtual +0x38. A time source/age binding is not established here. |
| Entry +0x24 (2) | Low short of template +0x58. Exact field identity unresolved. |
| Entry +0x30 (4) | Template +0x78 if template +0x7c null, otherwise nested pointer +0x10. Identity unresolved. |
| Entry +0x28 (4) | Float 1.0 initialization. Earlier batch consumer reads this as an alpha-like value; temporal evolution is not recovered. |
| Template +8/+0xc/+0x10 (4 each), virtual +0x1c | Float components converted to packed entry +0x14 using a shared random multiplier. Virtual +0x1c receives 0/1 floats; the product-plus-one passes through 0x1000ebe0 with 0/1 arguments before component multiplication by 255. Exact accessor/helper semantics and color channel names not recovered. Alpha byte is forced 0xff by the observed packing. |
| Template +0x80 (1), +0x94 (4) | If byte zero, entry +0x18 is zero. Otherwise RNG 0x1009f860 and unsigned `(template[0x94] - 1)` converted to float feed an integer conversion into entry +0x18. This establishes randomized initial frame input, not uniformity, endpoint inclusion or the authored property binding. |
| Contact param_1 +0x10/+0x14/+0x18 (4 each) | Copied to entry +0x44/+0x48/+0x4c when owner +0x1c null. Otherwise 0x1007cb30 transforms the vector after helpers 0x10031780/0x10078340 and virtual +0x84. Hidden matrix outputs/local_4c/local_8c calling conventions remain unresolved. |
| Global pointer 0x1024b900 +0x8c14 (4); renderer +0x2c4 (4) | For finite angle values, zero config or angle <=0 selects 0x101acdf0; otherwise 0x101ae450, the recovered projected-geometry builder. Config identity unresolved; no graphics-quality threshold claim. |
| Contact +0x30/+0x34/+0x38 (4 each) | Copied to entry +4/+8/+0xc after geometry generation. Vector identity unresolved. |
| Entry +0x54 (4) | Nonzero geometry count permits insertion; zero calls 0x101acaf0 and returns 0. |
| Template +0x20 (1), entry +0x1d (1) | Disabled: entry byte becomes zero, call 0x101acbf0. Enabled: assign incremented owner identifier and call 0x101acbc0. Their storage semantics remain unresolved. |
| Chain node +0x28/+0x38 (4 each) | Follow +0x28 until +0x38 nonzero or null; cache nonzero +0x38 in owner +0x20. Parent/renderer interface identity unresolved. |

The first RNG color multiplier is decompiled as
`(((int)(rng | 0x80) >> 7) + 1) * 1.1920929e-7 - 1`;
the atlas multiplier uses the same integer expression times `5.9604645e-8`.
Do not replace these with an asserted distribution without recovering the RNG
and conversion instructions. The packed frame is later consumed by the
geometry builder at 0x101ae450. No atlas texture/material pair is selected here.

After successful geometry insertion, the saved empty-owner boolean triggers
renderer virtual +0x24. If owner +0x20 is still absent, the chain is searched;
either a missing chain or a found value returns entry +0x1d. A successful
non-identifier insertion therefore also returns zero; zero alone is not a
failure measurement. Other early zero returns are disabled renderer, rejected
owner/template combination and empty generated geometry. No temporal fade
update is performed here, and no constant 128-mark limit appears. Ring size,
free-list provisioning, lifecycle update, material routing, template property
bindings and runtime eviction/fade matches remain **not measured**.

### Identifier insertion helper, 0x101acbc0

Evidence `ev_8f7afeab644eeea3db121ac39520b61137981e7e4b5fd3b3ede96ab6249ab4eb`,
`decal-insert-identifier.json`, all 38 bytes 0x101acbc0–0x101acbe5.
One resolved caller 0x101ae980, no callees or strings, no conditional branches.
ECX owner +0x2c (4 bytes) is an array base; +0x10 (4 bytes) is its append index.
Write the incoming 4-byte entry pointer at array[index], increment +0x10 by one,
then write a zero sentinel at the new index. No bounds check, allocation,
recycling, timer update or normal-ring +0xc count change occurs in this helper.
Capacity provisioning and identifier-list removal remain unresolved.

### Normal ring insertion helper, 0x101acbf0

Evidence `ev_244a1163bff59ada3d2721072d6110deecd46863ca3dc5cab467313eb429541f`,
`decal-insert-ring.json`, all 76 bytes 0x101acbf0–0x101acc3b. One resolved
caller 0x101ae980, no callees or strings. ECX owner +0xc (4 bytes) is the normal
ring count, +0x28 (4 bytes) its pointer-array base, +0x38 (4 bytes) its write
index, +0x3c (4 bytes) its oldest index, +0x40 (4 bytes) its index mask.
If count is zero: set write index and count to 1, oldest index to 0, write entry
pointer at array[0], return. Otherwise save old write index, advance it as
`(old + 1) & mask`, increment count, and write pointer at array[old], return.
No capacity check, recycling, allocation or temporal fade occurs here. Combined
with 0x101ae980, this establishes oldest-entry recycling when the owner's limit
would be reached; the limit/mask values and authored provisioning remain
unresolved. It is not evidence of a fixed 128-mark capacity.

## Decal size, rotation and atlas frame, 21 November 2026

Offline, from the probe-hashed binaries: `RendDX9.dll` sha256 `66ec4571…780e` and
`BF2.exe` sha256 `5a72e239…f5c6a1` (copies under `m4-effects-20261120/` and
`footsteps-resume-20261120/`). Ghidra was not installed for this pass. Bytes
and instructions came from `objdump` and `tools/pe_vtable_slots.py`. Control
flow came from the saved REA dossier `rea-decal-object/decal-object.json`.
These resolve the "not recovered" items in the two sections above.

**Template property bindings.** The renderer DecalTemplate vtable is
**0x10221928**. It sits beside the `DecalTemplate.cpp` path string and is
referenced at 0x101afc86/0x101aff48. The BF2.exe property commands
(registration 0x560600–0x560c00, names at 0x8cfeac…) forward through wrappers
0x646f10–0x6473c0, which call `[template+0x64]`'s slot. Every pair:

| Property | BF2.exe set / get | Renderer slot set / get | Field |
| --- | --- | --- | --- |
| `size` | 0x646f10 / 0x646f40 | +0x08 / +0x0c | +0x18 float |
| `randomSize` | 0x647030 / 0x647060 | +0x10 / +0x14 | +0x1c float |
| `randomColorIntensity` | 0x647170 / 0x647190 | +0x18 / +0x1c | +0x04 float |
| `color` | 0x647000 / 0x647460 | +0x20 (0x101af960) / +0x24 | +0x08/+0x0c/+0x10 (+0x14) |
| `isStatic` | 0x6470d0 / 0x647100 | +0x28 / +0x2c | +0x20 byte |
| `usedOnTransparentMaterials` | 0x647080 / 0x6470b0 | +0x30 / +0x34 | +0x21 byte |
| `randomRotation` | 0x646fb0 / 0x646fe0 | +0x40 / +0x44 | +0x24 float, degrees |
| `rotation` | 0x646f60 / 0x646f90 | +0x48 / +0x4c | +0x28 float, degrees |
| `decalTextureName` | 0x647120 / 0x647150 | +0x50 / +0x54 | string at +0x3c |
| `animationEnable` | 0x6471b0 / 0x6471e0 | +0x58 / +0x5c | +0x88 byte |
| `animationRandomizedStartFrame` | 0x6471f0 / 0x647220 | +0x60 / +0x64 | +0x80 byte |
| `animationSpeed` | 0x647230 / 0x647260 | +0x68 / +0x6c | +0x84 float |
| `animationFrameWidth` (pixels) | 0x647270 / 0x6472a0 | +0x70 (0x101afa40) / +0x74 | +0x8c = pixels / texture width |
| `animationFrameHeight` (pixels) | 0x6472b0 / 0x647340 | +0x78 (0x101afae0) / +0x7c | +0x90 = pixels / texture height |
| `setAnimationFrameWidthRelative` | 0x6472e0 | +0x80 / +0x84 | +0x8c float |
| `setAnimationFrameHeightRelative` | 0x647310 | +0x88 / +0x8c | +0x90 float |
| `animationFrameCount` | 0x647350 / 0x647380 | +0x90 / +0x94 | +0x94 dword |
| `animationFrameCountX` | 0x647390 / 0x6473c0 | +0x98 (0x101af8b0) / +0x9c | +0x98 dword; +0x9c = 1 / count, 0 for a zero count |

So geometry accessors v0c/v14/v44/v4c in 0x101ae450 are size, randomSize,
randomRotation and rotation, and insertion slot +0x1c is
randomColorIntensity. The template's isStatic byte (+0x20) chooses between
0x101ae980's identifier list (static) and the normal ring (dynamic).

**Frame size, 0x101af9c0.** The base UV (+0x34/+0x38) is the texture atlas
entry's (+0x7c) +8/+0xc, or 0 with no entry. With animationEnable, the frame
size is relative (+0x8c, +0x90) times the entry's size (+0, +4), or just the
relative values with no entry. Without animationEnable it is the entry's
size, or (1, 1). The result is stored through slot +0x38 into +0xa0/+0xa4.
The entry's source (+0x7c) is not traced.

**RNG, 0x1009f860.** This is Park–Miller: two Schrage steps by 0x5e30 and
0x661f modulo 2^31 − 1 (combined multiplier 630360016). The state is at
ECX, and every value lies in [1, 2^31 − 2]. All four decal draws pass
ECX = **0x1023b618**. Its static `.data` value is **0x759dc150**. Every call
site converts the value as `s = ((int)(v | 0x80) >> 7) + 1`, which lies in
[2, 2^24].
- Signed unit: `s × 2^-23 − 1` (0x10221810, 0x101ddde4), in **(−1, 1]**.
- Start frame: `s × float(unsigned(frameCount − 1)) × 2^-24` (0x1020cf68), in
  (0, frameCount − 1].

Other users of 0x1023b618 are not enumerated, so Retail's sequence is not
reproduced.

**Draw order for one insertion:**
1. Colour, at 0x101aea9d.
2. Start frame, at 0x101aeb75, only when +0x80 is set. Otherwise entry +0x18 = 0.
3. Rotation, at 0x101ae475.
4. Size, at 0x101ae4c8.

Rotation and size are drawn only on the 0x101ae450 path. The 0x101acdf0
alternative, chosen when 0x1024b900+0x8c14 is zero or renderer +0x2c4 ≤ 0, is
not read.

**Placement.** The prototype at 0x1023aef8 is four 0x14-byte vertices:
(±1, 0, ±1) with UV ±1, in the order (−1,−1 | −1,1), (1,−1 | 1,1),
(1,1 | 1,−1), (−1,1 | −1,−1). It is the unit square, so `size` is a
**half-extent**. Each vertex is:
- rotated about the local Y axis by `(rotation + r × randomRotation) × π/180`
  (0x1023add0 = 0.017453292);
- scaled by `(size + r′ × randomSize) × arg 4`, where arg 4 is the caller's
  [ebp+0x10] (0x101aec4d) and is **not measured**;
- given UV `(uv + 1) × frameSize × 0.5 + offset`.

When `animationEnable && frameCount ≠ 0`, `offset = base + (f % countX) ×
width, (f − f % countX) × (1 / countX) × height`, with f = x87 `ROUND(entry
+0x18)`. That is column × width and row × height. The control word at that
call is not sampled. For decal_s_rock (count 4) the start frame is
round(U(0, 3]): frames 0 and 3 take one sixth each, and frames 1 and 2 one
third each.

**Colour.** `clamp(1 + r × randomColorIntensity, 0, 1)` (0x1000ebe0 with
0/1) multiplies `color` × 255 per channel. Alpha is forced to 0xff, and the
result is packed into entry +0x14. Stock small decals use intensity 0 and
colour 1/1/1.

**Ring.** Insertion recycles when `count + 1 >= limit` (0x101ae9c4), so a
receiver keeps at most **limit − 1** live normal marks: 63 for a null owner,
31 otherwise (0x101acc80). Which receivers have a null owner is **not
measured**.

Ported in `src/app/include/obf2/app/decal_placement.h` and
`test_decal_placement`. The atlas gate, the frame-size choice, randomSize,
base rotation and the start-frame distribution now follow these rules. Not
ported:
- the colour tint (scene draw items carry none);
- arg 4;
- the 0x101acdf0 path;
- pixel frame sizes;
- the per-receiver ring, which keeps the global 128 FIFO.

No pixel, size or atlas match against Retail is claimed from this offline
pass.

## Receiver interface and provisioning — resumed 2026-11-20

The literal address search `tools/pe_address_refs.py` finds insertion
`0x101ae980` at `RendDX9.dll` **0x10221878**, slot **+0x18** of the table at
**0x10221860**. Table entries +0..+0x28 are, in order: 0x101af150,
0x101acb40, 0x101acb50, 0x101acb60, 0x101acb70, 0x101acd10,
0x101ae980, 0x101ad380, 0x101aedf0, 0x101ad400, 0x101acb80.
Literal references alone do not prove virtual-call bindings or an interface name.
Evidence files are under `rea-decal-binding-resumed/session2/`.

### Constructor 0x101acc80

Evidence `ev_389f6245f94b848d789ad929fe72fda2736c831f4c0881574ee88c5b749fd003`,
`receiver-constructor.json`. Resolved caller 0x10124be0; no strings.
The pseudocode traverses the full initialization below, but Ghidra's recorded
body/assembly covers only the initial 11 bytes. The independently decoded full
bytes from REA `receiver-constructor-bytes.json` (evidence
`ev_9b0672855193434b3d39a3be98cb03602a12128016d72b8c38b1c7ef3043245a`)
verify all instructions **0x101acc80–0x101acd09**, including `ret 4` at
0x101acd07. Full listing: `receiver-constructor-instructions.txt` one level up.
Do not silently treat Ghidra's initial-body metadata as the full boundary.

| Owner offset (bytes) | Observed pseudocode initialization |
| --- | --- |
| +0 (4) | Vtable 0x10221860. |
| +4 (1) | Zero. |
| +8 (4) | **64** when incoming owner parameter is null, **32** otherwise. |
| +0xc/+0x10/+0x14 (4 each) | Zero. +0xc/+0x10 are the previously recovered normal/identifier counts; +0x14 purpose unresolved. |
| +0x18/+0x19/+0x1a (1 each) | 0/1/1 respectively; concrete property bindings unresolved. |
| +0x1c (4) | Incoming owner parameter. |
| +0x20 (4) | Zero cached chain binding. |
| +0x24/+0x2c (4 each) | Same allocation result from helper 0x1005fa10, requested size `limit*4+0x20`. Allocation API semantics not independently recovered. |
| +0x28 (4) | Allocation base +0x20; first word zeroed. |
| +0x2c pointed first word (4) | Zero identifier sentinel. |
| +0x30/+0x34 (4 each) | 1/0. |
| +0x38/+0x3c (4 each) | Zero write/oldest indices. |
| +0x40 (4) | `limit-1`, the normal ring mask. |

These branches provision two receiver limits, not a universal 128-mark budget.
Effective visible capacity and owner classification remain **not measured**
until live receiver checks pass. The instruction check now verifies these limits.

### Factory 0x10124be0

Evidence `ev_b302be8bfbb6b05a66d1ad5834ddc4eb6cee6f6df64947945a673d11a07ac953`,
`receiver-factory.json`, all 91 bytes 0x10124be0–0x10124c3a. No resolved callers
or strings. ECX renderer **+0x2a6 byte** zero returns null. Otherwise helper
0x1005f9f0 receives **0x44** bytes; null allocation selects null result,
non-null invokes 0x101acc80 with the incoming parameter. The result is copied
to two local words, then helper 0x10126720 receives the eight-byte local region
and one copy's address. Returns the constructed pointer/null. Exact container
registration by 0x10126720 remains unresolved.

The complete factory instruction listing resolves its hidden ECX argument:
0x10124c23 passes **manager+0x28** to 0x10126720. Its payload words are both the
constructed receiver pointer. This establishes the receiver registry binding.

### Clear entries 0x101acd10

Evidence `ev_c7119be71e8ddd0d5b6916a4cea1915d53896a2dfab102f10edc30fb5433ae98`,
`receiver-update.json`, 185 recorded bytes across five ranges ending 0x101acde2.
Callers 0x101ad4e0/0x101af150; helper 0x1005fa30; no strings. This is a clear
operation, not the temporal update suggested by the exploratory filename.
While normal count+0xc is nonzero, take array+0x28[oldest+0x3c], clear that
slot, decrement count and advance oldest with mask+0x40. Nonzero entry+0x50
is passed to 0x1005fa30; entry+0 becomes renderer-global 0x1024b908's +0x290
free-list head and that head becomes the entry. Then consume identifier
array+0x2c until its first pointer is null: decrement count+0x10, replace the
first slot with the former last pointer, zero the new trailing sentinel,
release nonzero geometry and recycle identically. Finally zero write+0x38
and oldest+0x3c. Ghidra labels the release helper nonreturning and truncates
the nonzero-geometry identifier continuation; that annotation is not verified.
No fade formula or elapsed-time comparison is recovered here.

### Identifier removal 0x101ad380

Evidence `ev_5b9e14d97172ade8e6171f6f30df71c82dac560059790a606f957c41516a6fd4`,
`receiver-remove-identifier.json`, 118 recorded bytes ending 0x101ad3ff.
No resolved callers/strings; helper 0x1005fa30. Zero input identifier returns.
Otherwise scan receiver+0x2c pointers until a zero sentinel or entry byte+0x1d
equals the input. A miss returns. On a hit, decrement identifier count+0x10,
replace the found pointer with the former last pointer, zero the trailing
sentinel, release nonzero entry+0x50 and push the entry onto manager+0x290
through entry+0. No normal-ring count, age or alpha update occurs here.
The release helper's nonreturning annotation remains unverified.

### Receiver time/update slot 0x101ad400

Evidence `ev_d9f0dea0b9825f8b630f25457c4c9d9ad8b31e8014c75dd90f69694557003c64`,
`receiver-state-update.json`, 213 recorded bytes across three ranges ending
0x101ad4de. No resolved callers/strings; helper 0x1005fa30. Null owner+0x1c
forces byte+0x19=1; copy that byte to+0x1a. If+0x19 is zero, double the incoming
float. For normal-ring indices `(oldest+0x3c & mask+0x40)` through write+0x38,
add the float to each entry+0x34. Manager virtual+0x38 returns a float. While
normal count+0xc is nonzero and the oldest entry+0x34 is at least that float,
clear its slot, decrement count, advance oldest with the mask, release nonzero
geometry+0x50 and recycle via entry+0/manager+0x290. If normal+identifier counts
sum to zero, invoke manager virtual+0x28. Return 1. The concrete incoming time
units, scheduling and float meaning remain **not measured**. Entry+0x28 alpha
is not changed by this function. Nonreturning-release annotation is unverified.

Manager vtable is **0x1021cad0**, assigned at 0x101257ce; its +0x38 target is
**0x101247a0**. REA did not recognize this four-byte getter as a function;
`renderer-age-limit-bytes.json` and the direct instructions verify precisely
`fld dword ptr [ecx+0x10]; ret` (0x101247a0–0x101247a3). No other fields/branches.
Do not label manager+0x10 as wall-clock time or lifetime solely from this getter.
Similarly receiver virtual+0x20 is **0x101aedf0**, seven bytes verified in
`receiver-lifecycle-bytes.json`/`receiver-count-instructions.txt`: load+0x10,
add+0xc, return. This is total entry count, with no branches or mutation.

### Receiver registry tree — complete helper observations

0x10126720, evidence
`ev_1fe0a65bfda502aa21fafe199ec8580abeb981418aa83506c6937213efefb0cc`,
`receiver-registration.json`, all 181 bytes, callers 0x10124be0/0x10124c40,
callees 0x101265a0/0x10193e70, no strings: tree+4 is the header; header+4 is
the root; node+0/+8 are left/right, node+0xc is the unsigned ordering key,
node+0x15 is the sentinel byte. Walk left if incoming key is smaller, right
otherwise until sentinel. If the left-insertion candidate equals header's
leftmost, insert immediately; otherwise invoke predecessor helper 0x10193e70.
Insert when the candidate key is smaller than the incoming key; otherwise
return the existing iterator with false. Output iterator is at output+0,
inserted byte at output+4. New inserts delegate to 0x101265a0.

0x101265a0, evidence
`ev_375720ed3d25cce6fa6aeb1430dec6798720ae37970ff46b0ad6c8cbcacc17b4`,
`receiver-tree-insert.json`, all 380 bytes: tree+8 is count, tree+4 header;
count greater than 0x1ffffffd throws `map/set<T> too long` through the observed
standard-library exception construction path. Otherwise allocate via 0x10125bb0
with header/parent/header/payload/color 0 and increment count. Header insertion
sets root/leftmost/rightmost to the node; right insertion sets parent+8 and
updates header+8 if needed; left insertion sets parent+0 and updates header+0
if needed. Rebalance while parent color+0x14 is zero: red uncle recolors parent
and uncle to 1, grandparent to 0 and moves upward; black uncle performs the
appropriate inner rotation, recolors parent 1/grandparent 0 and outer rotation.
Mirror branches use helpers 0x101916b0/0x101c0ea0. Set root color 1 on completion;
output receives new node. Concrete rotation helpers were not reopened.

0x10125bb0, evidence
`ev_18857d003e90eb1c49416b6db594455960c5c56bb3c0339c551c5e3829d1cec1`,
`receiver-tree-node.json`, all 117 bytes: install SEH frame, request 0x18 bytes
via 0x1005f9f0, call 0x100d61a0 with five incoming values only if non-null,
restore ExceptionList and return allocation. No other field is initialized here.

0x100d61a0, evidence
`ev_b3e06a38d002f3fbf9b0a5c55f6db917c130464896a8df57b730d203a461f691`,
`receiver-tree-node-constructor.json`, all 50 bytes, no calls/strings/branches:
node+0=arg 1, +4=arg 2, +8=arg 3, +0xc=payload[0], +0x10=payload[1],
+0x14 byte=arg 5, +0x15 byte=0. Factory's equal pointer payload makes both
key and value receiver addresses. Thus manager+0x2c is registry header and
+0x30 count. A bounded read-only walk can now identify the live receiver,
counts and newly created entry without injection or breakpoints.

## Graphics-quality application and decal gate, 0x1000fb50

Evidence `ev_212713e1e86fb5c73b8742e7aa7855e395c43e144255935ba818d0bf1858d559`,
`graphics-quality-application.json`, all 1823 bytes 0x1000fb50–0x1001026e,
callers 0x10010290/0x100103a0. The sole string is `GSSkipMipLevels`.
The relevant direct instruction is **0x100101c3** compare settings+0x3c to 1,
**0x100101c7** signed-set-greater and **0x100101ca** store result to global
manager 0x1024b908 **+0x2a6**. Thus insertion and receiver creation are disabled
when that pending tier is <=1. Which saved option supplies it is checked by
changing one normal graphics option and reading the live flag, not guessed.

All local settings offsets: current+0x20/pending+0x40 set dirty byte+0x114;
current+0x24/pending+0x44 set dirty+0x116/+0x117/+0x118;
current+0x10/pending+0x30 set dirty+0x117/+0x115;
current+8/pending+0x28 set dirty+0x118;
current+0xc/pending+0x2c update+0x115 only for an existing dirty state or a
transition into/out of tier 1;
current+0x14/pending+0x34 and current+0x18/pending+0x38 control virtual renderer
options; pending+0x3c supplies the particle table and decal gate. Override
+0x110 can replace the derived mip count. Display selection uses current
+0xd0/+0xd4, pending+0xd8/+0xdc, table base+0xe4, stride 0x28, inhibited by
byte+0x119. Unnamed setting identities remain unresolved rather than assigned
from their order.

Complete branch groups, including work outside decals:

* Pending+0x40 tiers 1/2/3 derive mip skip 2/1/0 unless+0x110 is nonnegative.
  Writes global 0x1023b974 offsets+0x3c/+0x44/+0x54/+0x6c/+0x74 through the
  observed clamped derived levels; `GSSkipMipLevels` propagates via the resource
  interface when present. Concrete resource interfaces remain unresolved.
* Pending+0x30 tiers 1/2/3 call 0x10028bd0 with 3/1/0 when global 0x1023d098 exists,
  zero global 0x1023d954+0x40; tier 1 additionally invokes settings virtual+0(4,0).
* Pending+0x28 tiers 1/2/3: global 0x1024b930 helpers 0x100e35c0 receive 64/80/100,
  0x100df790 receives 1/0/0. Global 0x1024b8f0 virtual+0x40 receives 1/1/0 and
  +0x68 receives 0.5/0.75/1. Global 0x1023d098 byte+0x6a4 becomes 0/1/1 and
  global 0x1024b90c virtual+0x18 receives 0/1/1. Global 0x1024b918 virtual+0x94
  receives 0/1/2. Each global's presence guard is preserved in the evidence.
* Pending+0x2c tiers 1/2/3 write global 0x1023d098 floats at
  +0x6c0/+0x6c4/+0x6c8/+0x84c/+0x8b0 respectively
  `(6,6,40,0.5,2)/(7,8,60,0.8,1)/(8,10,80,1,1)`;
  +0x7e4 becomes 1/0/0, +0x7ec becomes 0/0/-1.
  Global 0x10258f38+0xf4 becomes 0.4/0.7/1 when present.
* If global 0x1024b914 is absent, jump directly to the particle-table/gate part.
  Otherwise pending+0x34 tiers 0/1/2/3 set its virtual+0x64/+0x5c/+0x6c to
  the tier and virtual+0x54 to 0/50/100/200. Other tiers only update current.
  Pending+0x38 tiers 0 versus 1/2/3 set virtual+0xec to 0 versus 1; other tiers
  only update current. Each block runs only on a current/pending change.
* Global 0x1024b900 virtual+0x3c receives table 0x10237db8 plus tier*0x3c;
  global 0x1024b908 gets the gate above; global 0x1024b930+0x41c is set 1.
* If not inhibited by+0x119, display-index change calls 0x1000f770 with
  base+pendingIndex*0x28 and copies both pending fields. A remaining second
  field change repeats that call and copies it. Finally call 0x10027cd0.

No draw parameters or profile files are written by the read-only probe.

## MeshParticleMesh material/shader load, 0x10128390

Follow-up material read wrapper 0x10193a70: complete 9-byte body
0x10193a70–0x10193a78, evidence
`ev_2c8ca0670906686c569ba22b139760a62ce744122eb7dba4434b91bc681cbedf`,
`m4-completion-20261121/rea/mesh-particle-material.json`. Callers 0x100eb010
and 0x10128390; sole callee 0x10193480, no strings. This wrapper delegates
material decoding to 0x10193480; it does not independently establish particle
blend or lighting parameters.

Material decoder 0x10193480: complete 1511-byte body 0x10193480–0x10193a66,
evidence `ev_e04f0e7f102b4cc68e7d29ccc8ce2dd69dc12bbe938bba52fce4f0834c58ea81`,
`m4-completion-20261121/rea/material-read.json`. Sole recorded caller is the
wrapper above;23 callees (complete list retained in dossier),11 referenced
strings. It reads a length/string through stream virtual+0x10, null terminates
it, retains it as state key, then reads texture count and sizes pointer storage
through 0x100ec4e0. Each texture has a length: zero selects prefix+manager+0x1c+
`default`; nonzero reads/name-terminates the supplied path. A path lacking `/`
gets the supplied prefix+manager+0x1c. `default_sdgbmfbf_color_lut` triggers the
BundledMeshStateManager.cpp obsolete-export diagnostic and is rewritten to
`_SpecularLUT_pow36_`; any `SpecularLUT_pow36` occurrence becomes
`@@ABSOLUTE@@Common/Textures/SpecularLUT_pow36.dds`.

For argument 5==0, renderer global 0x1023d098+0x6cc can suppress `_b.dds`
loads. Otherwise 0x10019430 resolves each texture. Slot1 ending `_b.dds` sets
state byte+0x35; a positive slot without `SpecularLUT` sets byte+0x36. The
remaining flag bytes are initialized zero in this body. Temporary names and
read buffers are released on each loop. After 0x10193080 builds the cache key,
0x101931a0 may return an existing state: virtual+0 addrefs it, cleanup through
0x101933e0, then return. Otherwise allocate a state, initialize vtable 0x10220c94,
reference+4=1, string+8, pointer-vector+0x28/+0x2c/+0x30 and bytes+0x34..+0x3d
to zero. Copy key/string and texture pointers, addref each nonnull texture via
virtual+8; copy all 10 flag bytes+0x34..+0x3d. With argument 4==0, a successful
`EnvMap` lookup increments the local texture count. Append the state to manager
vector+0x10/+0x14/+0x18 (fast available capacity or 0x10193a80), clean temporary
state and return. The decompiler does not correctly model every register/hidden
argument to the cache/allocator helpers; their internal layout is not inferred
from uninitialized locals. This decoder establishes texture/state loading, not
MeshParticleMesh technique dispatch or particle lighting defaults.
The explicit allocation-null branch retains a null state before subsequent
state use; no graceful allocation-failure recovery is inferred without the
allocator helper's semantics.

Reused `mesh_info` confirms the M4 plane has alphaMode0, empty serialized
technique, diffuse texture slot 0 and SpecularLUT slot 1
(`m4-completion-20261121/m4-mesh-material.txt`). The mode must be bound to the
actual MeshParticleMesh dispatch; do not infer Additive from a bright flash.

BF2.exe 0x71d140: complete 30-byte body 0x71d140–0x71d15d,
evidence `ev_a020c15064765471b91d0cb67bff581fb02a0888a95cf9f0c3b0a5aa25f0f0be`,
`m4-completion-20261121/rea-bf2/object-geometry-access.json`.766 recorded
callers; no direct callees/strings. Read global 0xa08f5c, call virtual+0x54;
return null unchanged if its result is null, otherwise call result virtual+0x4c
and return its result. It is a current-selection/console bridge, not evidence
that receiver owner+0x4c universally returns a geometry pointer. No inferred
receiver layout is implemented from this wrapper.

Active owner slot+0x4c in the captured primary vtable is BF2.exe 0x6d8770:
complete 13-byte body 0x6d8770–0x6d877c, evidence
`ev_5e03e6257736a0056ec582fd0b8d0b860bf4442d64595ed344370898b3bcc5ee`,
`m4-completion-20261121/rea-bf2/owner-slot4c.json`. It writes its one stack
argument to incoming ECX+0x20 and returns; no branches, calls or strings.
Thus the owner's primary virtual+0x4c is **not** the mesh geometry accessor
used by the renderer's separate contact interface. No target calls were made.

Actual static render surface diagnostic: reused `mesh_info --surface-ray`
reads the intact original staticmesh, not a collision proxy. For the placement
`Levels/fushe_pass/server.zip:StaticObjects.con`, template
fence_corrugated_3x12m_broken at **142.991/78.902/159.832**, zero rotation,
LOD 0 front triangle 34 crosses local Z=**0.168134689**, giving world
Z=**160.000134689**, consistent with Retail entry 160.000137. Rear triangle 28
crosses local Z=**0.114613056**, giving 159.946613056, consistent with Retail
exit 159.946609. The saved exit output vertices at local Z=**0.104613056**
equal the render plane minus measured separation 0.01 exactly. Original
staticmesh and same planes at LOD 1 are retained in
`m4-completion-20261121/fence-render-intersections.csv`; LOD 2 differs slightly.
The Native hit 159.9815 is between these two actual surfaces. This establishes
the proxy/render-plane difference, not yet pixel visibility after correction.

Active decal owner constructor BF2.exe 0x627ac0: complete 629-byte body
0x627ac0–0x627d34, evidence
`ev_2ce78844d6ee38b9aca4154b782eb9e3847d2a11c76e2cd612b16cae43eaed52`,
`m4-completion-20261121/rea-bf2/decal-owner-constructor.json`.16 recorded
callers,12 callees and five strings; assertion names SimpleObject.cpp.
Call base 0x720520(template), set primary vtable 0x8fc4c0 and secondary interface
at object+**0x80** to 0x8edfb8. Clear object+0x144/+0x148/+0x14c; copy
template+0xec to object+0x124; call template virtual+0x7c/+0x84. Template+0xdc
bit 4 calls 0x71d680(0x100,0). Any template+0x118/+0x139/+0x14d nonzero allocates
0x20-byte auxiliary storage at object+0x144 (zero seven words, byte+0x1c=1;
null allocation is retained) and finally sets object flag 0x1000 through 0x71d680.
Template+0x184 nonzero and false 0x71b150 invokes 0x440510 (meaning unresolved).
Template+0x1bc nonzero resolves CollisionManager through singleton 0x9ff960
virtual+0x48; template virtual+0x98(template+0x6c) supplies its lookup argument.
Non-null collision mesh gets virtual+0x24(template), attachment 0x7201d0 with
class ID 0xcaa32 and release virtual+8. Failed attachment reports the complete
collision/template diagnostic at SimpleObject.cpp line 0xa0.
If `(template[0xdc]&0x18)==0`, bit 4 is set and 0x440510 is false, resolve
DecalManager and call virtual+0x14, storing the result at object+**0x148**.
This binds the per-SimpleObject receiver, not the rendering contact interface
yet. The secondary+0x80 interface is a concrete next binding to inspect.

Evidence `ev_dc9a9434121a828260da491fcddaa9a0aed213070095361f029d627f0f1eb943`,
`mesh-particle-shader-binding.json`, all 1576 bytes 0x10128390–0x101289b7,
no resolved callers. Source assertion names `MeshParticleMeshTemplate.cpp`.
This overrides the serialized shader name with **MeshParticleMesh**; using
the bundled mesh's `StaticMesh.fx` string alone does not establish its draw path.

Complete direct field/block inventory: this+0x54/+0x58 pointer range yields
geometry count `(end-begin)/4` or 0 for null begin; helper 0x1012b370 sizes the
output. For each nonzero geometry count, helper 0x1012abc0 sizes its output,
then each LOD uses this+0xd0 with outer stride 0x10 and inner stride 0x10;
this+0xec uses outer stride 0x10, inner stride 0x48. Set LOD output+0x40=3 and
byte+0x44=0. Read material count and size via 0x10129810. Each material has
stride 0x18 and initializes all six words to 0. Version this+0xa8<5 derives
mode 0 except second material mode 1; newer versions read mode. Mode 1 changes
LOD+0x40 to 2; mode 2 sets LOD+0x44 byte 1. Their downstream technique mapping
is not recovered here.

Read serialized shader-name length. On first use allocate length+1 bytes,
read and terminate the temporary string as observed, release it, then load
global shader+path `MeshParticleMesh` through 0x100230e0 into 0x1025bee0.
On cached use skip those bytes. Lazily cache lookups through 0x1001e5d0:
0x1025bee4 WorldViewProjection (assert/report if still absent),
0x1025beec COLOR1,0x1025bef0 COLOR2,0x1025bef4 COLORBLENDGRAPH,
0x1025bef8 fogRange,0x1025befc fogColor,0x1025bf00 EffectSunColor,
0x1025bf04 EffectShadowColor,0x1025bf08 Texture0,0x1025bf0c Texture1,
0x1025bf10 matONEBONESKINNING. String temporaries are destroyed after use.
Helper 0x10193a70 receives shader, this+0x30 and stream and returns material+0.
Read material+8/+0xc, then a count: primitive type this+0xac=4 stores count/3
at material+0x10; type 5 stores count-2; other types retain its initialized 0.
Read material+0x14, skip two four-byte fields, continue all materials/LODs/geoms.
No particle instance color, technique dispatch or lighting-factor default is
recovered by this loader. Those remain unresolved; no unlit approximation is
justified solely from the mesh string.

## Default time parameters and receiver collection

### Manager constructor 0x101257c0

Evidence `ev_4a08c62846709223c15ebca93a573e1e5542d11d1d54943986f647dd8f8a3440`,
`manager-defaults-constructor.json`, all 510 bytes 0x101257c0–0x101259bd.
No resolved callers/strings. Primary vtable+0=0x1021cad0; secondary+4 is
first 0x101dffdc then 0x1021cab4. Manager+c=1, **+0x10=20.0** (instruction
**0x101257e7**, bits 0x41a00000), **+0x1c=0.85** (**0x101257ee**, bits 0x3f59999a),
+0x20=0, byte+0x24=0. It later computes **+0x14=20×0.85=17** and
**+0x18=20−17=3**. The live normal-entry age/alpha traces independently agree.

All remaining direct initialization: six self-linked tree headers at
+0x2c/+0x38/+0x44/+0x70/+0x7c/+0x88, counts+0x30/+0x3c/+0x48/+0x74/+0x80/+0x8c=0.
Helpers 0x10125e10/0x10125c40/0x10125d70 supply these headers; sentinel bytes
are+0x15 for headers2c/38/70/88, +0x2d for 44, +0x35 for 7c. Each header's
+0/+4/+8 points to itself. Queue triples+0x50/+0x54/+0x58 and
+0x60/+0x64/+0x68 are 0. Helper 0x101aa570 initializes the nested renderer.
Manager+0x290/+0x294=0, ushort+0x298=0, +0x29c/+0x2a0=1024,
bytes+0x2a4/+0x2a5=0, byte+0x2a6=1. Floats+0x2b0/+0x2b4/+0x2b8/+0x2bc/
+0x2c0/+0x2c4 initially 50/30/0.05/0.85/0.01/60; +0x2b4 is subsequently
recomputed as 50×0.85=42.5. +0x2c8 starts 0; allocation helper 0x1005f9f0(0x14)
and, on non-null only,0x10016080 produce its final pointer/null. Helper 0x10124a20
is called at the end; its full pool-provisioning behavior is not recovered here.
No conditional default for the 20-second normal-entry limit appears here.

### Distance-setting method 0x10124800

Evidence `ev_db565e4e464912646dff8421f1bb6c204305742c1f84583fa8a0c2b88584f161`,
`manager-fade-settings.json`, all 50 bytes, no calls/branches/strings. Stores
incoming float in manager+0x2b0, recomputes time fade start+0x14 as+0x1c×+0x10,
time fade interval+0x18 as+0x10 minus that product, and distance interval+0x2b4
as incoming float×+0x2bc. Thus time fade and shader distance fade are separate.

### Manager update 0x10124eb0 and pending queues

Evidence `ev_3d67b0e36fcf31e438853c9151e3fb0199a86dea3c89f7d2100bca187bf9ffcf`,
`manager-entry-update.json`, all 140 bytes, no resolved callers/strings.
Calls 0x10124d80 then 0x10124e50, invokes global 0x1023b970 virtual+0x50, stores
incoming float at manager+0x20. Walks the active receiver tree headed at+0x38;
node+0x10 receiver receives virtual+0x24 with that same float. Successor walk
uses node+0 left/+4 parent/+8 right/+0x15 sentinel. After the full walk,
calls 0x10124e50 again and returns. Receiver 0x101ad400 establishes accumulation
and removal; wall-clock traces establish seconds for this visible test.

0x10124d80 (`manager-alpha-update.json`, exploratory filename is misleading),
evidence `ev_32beaa8a06b8272d3ab64c5563989061c9631c5883dca852dcd084ce4bd3feae`,
all 109 bytes: walk pending pointer queue manager+0x50..+0x54; receiver byte+4
nonzero invokes manager virtual+0x28 with receiver. Lookup via 0x100ebb90;
if returned node differs from registry header+0x2c, erase via 0x101c17b0 and
invoke receiver virtual+0 with 1. Advance queue pointer 4 bytes and finish with
0x10126830(0,0). No alpha update occurs here.

0x10124e50 (`manager-pending-removals.json`), evidence
`ev_1f23a6e69f98b8c01dfe86a257ddd7145badcc574441b0d9181fe459aabbc6d1`,
all 89 bytes: walk pending pointer queue+0x60..+0x64; lookup through 0x100ebb90,
erase via 0x101c17b0 only when result differs from active-tree header+0x38;
finish with 0x10126830(0,0). No receiver deletion or alpha write here.

### Template lookup/create 0x101251b0

Evidence `ev_cd0ff81d92bffc78989a722f3fb09e7a998b07b4c64a4b33be58fae65f5604ed`,
`manager-render-preparation.json` (exploratory filename), all 246 bytes.
No resolved callers. Lookup input via 0x1015e170; a found node other than
manager+0x44 header returns node+0x28. Otherwise ushort template counter+0x298
greater than 511 emits the `DecalManager.cpp`/`To many decalManager templates`
diagnostic and string cleanup. Request 0xa8 bytes via 0x1005f9f0; non-null calls
0x101aff40, null selects 0. Lookup/insert name via 0x10127270 and store pointer;
template ushort+0x58 receives counter, increment counter, lookup/insert that
short via 0x10127300 and store pointer, return pointer. Allocation-failure
continuation still dereferences null in the recovered code; no fallback invented.

### Per-receiver alpha and collection 0x101aae80

Evidence `ev_148dd7a46d661d39a834486c3ba99a15b27152f980355f06b22c0ee6e95a396e`,
`receiver-draw-alpha-1.json`, all 287 recorded bytes ending 0x101aafa4,
caller 0x101aafb0, callees 0x1000ebe0/0x101aad50/0x101274c0, no strings.
Output batch receiver-list+0x14/+0x18 yields a pointer count, or 0 for null begin.
For every identifier entry in receiver+0x2c[0..count+0x10), set entry+0x28=1
and pass output/entry/receiver/count-address to 0x101aad50. For every normal
ring entry from(oldest+0x3c & mask+0x40) to write+0x38, write
**alpha=1−clamp((entry+0x34 − manager+0x14)/manager+0x18,0,1)** and collect
identically. If output entry-list+0x24/+0x28 has nonzero length, append receiver
to output receiver-list using available+0x1c capacity or helper 0x101274c0.
No fade for identifier entries, removal or age accumulation occurs in this call.

### Renderer traversal 0x101aafb0

Evidence `ev_8cf9efbbca0d346d7ef599585bea8374a8a210e20e2bece790bf5cb5dbd8f29f`,
`receiver-draw-alpha-2.json`,509 recorded bytes ending 0x101ab1e8,
callers 0x10124b40/0x10124b80, no strings. Disabled renderer byte+0x90 or
zero input tree+8 count returns. Otherwise initializes three local pointer
vectors, walks input tree headed at+4, reads receiver byte+0x19 then clears it.
Only a previously nonzero byte and receiver virtual+0x20 nonzero count invoke
0x101aa030(receiver) and 0x10127a20(receiver-address). Tree successor helper
0x100eb490 advances through all nodes. Input camera virtual+0x68 supplies
four values at+8/+0x18/+0x28/+0x38, copied into renderer+0x20/+0x24/+0x28/+0x2c.
For each of three 0x10-byte vector descriptors, reset renderer caches+0x18/+0x1c,
initialize local batch storage, and collect each receiver through 0x101aae80.
Exactly 10 collected entry pointers triggers 0x101aca60 submission and clears/
releases that temporary list; nonempty remainder is also submitted. After each
pass release the temporary receiver/entry storage and zero the three vectors
on completion. All 0x1005fa30 nonreturning annotations remain unverified; they
must not be used to discard the continuation or the final two batch passes.
This establishes the alpha writer, not a visual mark match on the capture machine.

### Clamp helper 0x1000ebe0

Evidence `ev_275a59e3643891489d744fafea4850969f9b271426a51cd5a280ef62307fd00d`,
`rea-decal-binding-resumed/session2/float-clamp.json`: complete
34-byte body 0x1000ebe0–0x1000ec01, no calls or strings. Cap the input value
at the supplied upper bound, then floor it at the supplied lower bound and
return the float. This confirms the normal-entry alpha writer's clamp to 0..1;
it introduces no independent timer or opacity parameter.

## Triangle plane output, RendDX9.dll 0x101aef20

Evidence `ev_75bfd1d71846f381e723b755e8eab8de65c5ad788fb238097b8ddb9adeec1d58`,
`rea-projection-resumed/triangle-plane.json`, complete 177-byte body
0x101aef20–0x101aefd0. Callers 0x101ad860/0x101adab0/0x101ae020,
sole callee 0x10077e90, no strings or conditional branches. Incoming ECX is
the four-float plane output (saved in ESI), not a zero-valued normal input.
The three stack arguments are position pointers a,b,c. Clear output XYZ;
form cross(b−a,c−a) in a temporary three-float vector, call 0x10077e90 on
that temporary, then copy its resulting XYZ to the output and write
W=−dot(result,a). Thus the zero locals in the traversal pseudocode must
not be ported literally. The helper's normalization/degenerate behavior
still requires its own complete recovery before claiming exact projection.
No radius, clip angle, receiver ownership/material predicate or time state
is read by this function.
Assembly returns the output pointer in EAX.

### Vector normalization 0x10077e90

Evidence `ev_14255fa3dd2b4ea5d13e72e2bc292b049a2d39a1ff43d8e57aef2727e76c7b6b`,
`rea-projection-resumed/vector-normalize.json`, complete 135-byte body
0x10077e90–0x10077f16,78 recorded callers, no callees or strings.
Incoming ECX points to three floats XYZ. Compute squared length s. If
abs(s−1)<**1.1920929e−7**, leave XYZ unchanged and return 0. Otherwise, if
abs(s)<that epsilon, clear all three floats and return **0xffffb1df**.
The remaining branch multiplies XYZ by 1/sqrt(s) and returns 0. No additional
state is touched. The triangle-plane caller ignores the return code and uses
the resulting vector to write its plane. This resolves that caller's finite
normal/degenerate behavior; receiver selection and emitted-plane binding in
the traversal still require their own evidence.

### Plane copy/normalization 0x100f9e60

Evidence `ev_5f7ed04051d18e5fdbae0ee9fadb2599d5887ed205d591e1f9107d7eae2e79ee`,
`rea-projection-resumed/plane-helper.json`, complete 51-byte body
0x100f9e60–0x100f9e92. Sole caller 0x101adab0, sole callee 0x10077e90,
no strings/conditional branches. Incoming ECX is the output; one stack
argument points to four input floats. Copy XYZ to output, normalize output
XYZ through 0x10077e90, then copy input W **unchanged** to output+0xc.
This is not a general four-component plane rescale: W is not divided by the
normal length. The traversal's input comes from the already-normalized
triangle-plane builder. No material, timer or receiver identity is accessed.
Assembly returns the output pointer in EAX.

### Traversal plane binding resolved from the retained assembly

The original complete 0x101adab0 dossier remains the source; no repeat import
of that function was needed. At **0x101adb91**, ECX=&stack[-0x40] supplies
the triangle-plane builder's output. Its EAX result is pushed at 0x101adb99;
ECX=&stack[-0x50] at **0x101adb9a** is the copy helper's output. Hence
stack[-0x50..-0x44] contains normal XYZ and plane W. At
**0x101adc21–0x101adc67**, the first emitted vertex uses
`distance=dot(normal,position)+W; position-=distance*normal`.
The next two fan vertices repeat this operation. This is orthogonal projection
onto the receiver triangle plane, **not** a constant offset from a collision
proxy and not division by dot(projectionDirection,normal). UVs are retained.
The separately recovered **−manager[0x2c0]×direction** separation is then
added at 0x101adc70–0x101adc8d and repeated for all emitted vertices.
The projection operation is conditional on global byte 0x1023af48. Its live
value, exact visual-receiver/contact binding and general face eligibility
remain **not measured**. The earlier uninitialized-local warning is resolved
for this plane calculation only; it does not authorize a guessed receiver lookup.

### Plane-list append 0x100fae90 and gate output binding

Evidence `ev_aae779681b6ba0ebc30ab6a21d04845e6f5a11b82b620dbc510a604096aeb973`,
`rea-projection-resumed/plane-list-append.json`, complete 100-byte body
0x100fae90–0x100faef3; callers 0x100f9790/0x101ad860/0x101ae020,
callees 0x100fa310/0x100fa890, no strings. Incoming ECX is a vector descriptor:
+4 begin,+8 end,+0xc capacity-end, element stride 16 bytes. If begin is nonnull
and unsigned element count<capacity count, invoke 0x100fa310 on the supplied
plane and current end, then advance end by 16. Otherwise invoke 0x100fa890
with end, count 1 and the supplied plane. No other descriptor fields are read.
The retained complete 0x101ad860 assembly supplies ECX=**0x10265520** for all
three appends, establishing the three-edge-plane vector used by 0x101ad510.
This resolves the previously hidden output binding; the normal builder and
normalizer are documented above. Receiver/contact eligibility and neighbor
ownership remain separate unresolved bindings.

### Particle light property registration and serialization

BF2.exe **0x56d208**, complete 113-byte registration body,
`ev_0cf7d7a3353f0f19be5480a13a3203cc540ec9dcfd82854f25e89c57d4fec7fb`
(`m4-completion-20261121/rea-bf2/light-affection-registration.json`): descriptor
vtable 0x8d5b28; +4=1,+8=0x9a7430,+c=`LightAffectionFactor`,+10=0x9a7434,
+14=byte 0x9a7438,+18=0,+1c=1; +4c/+50=`float`,+78=type supplied by 0x40fe9c;
+a0=0,+a1=1. Calls 0x55f93c and temporary-string initialization. These shared
static defaults are not a particle-instance getter binding or an M4 value.

BF2.exe **0x650540**, complete 930-byte serialization body,
`ev_0a037333bff7fe1eb82b166b888a6caf8d232870ea32f68308d30a87347e8fee`
(`rea-bf2/particle-script.json`), callers 0x64c310/0x64ce10/0x64d510/0x64e0f0:
base 0x64fb30 failure returns false. Interface pointer at this+0x94 provides
virtual getters +170 randomSize,+168 particleMaxSize,+180 sizeGraph,+178
randomIntensity,+1b4 lightMapIntensityOffset,+190 lightAffectionFactor,+198
color 1,+1a0 color 2,+1ac colorBlendGraph,+188 alphaCull. Color getters return
three floats; blend graph four values. Light affection is serialized only for
nonnegative values; the negative sentinel is not its effective lighting value.
Relative this−8 virtual+120 supplies isStaticSystem, emitted only when true.
Build the output string, choose inline storage for capacity<16, call stream
virtual+14(buffer,length,extra argument), clean temporaries, return whether
bytes written equal length. Interface offsets are not particle storage offsets.
The M4's effective light affection remains **not measured**.

### Renewed live projection check,21 November

`m4-completion-20261121/owner-interfaces.json` confirms projection byte 1,
separation 0.01 m and clip angle 60 degrees on the pinned Retail renderer. No active
mark survived this later read, so it does not establish reciprocal owner fields.
The console-toggle experiment did not produce a visible console; no commands
were typed and no console-derived light value is claimed. Retail was closed
and its Video/Audio profiles byte-verified before offline implementation.
Initial PE data at RendDX9.dll **0x1023af4c** is integer 4, the traversal-depth
argument forwarded by 0x101ae450. Its live value must still be recorded in the
next preflight; it is not an inferred 18 from the separate terrain stride 0x12.

### Particle color consumer RendDX9.dll 0x10174740

Complete 574-byte body 0x10174740–0x10174980, evidence
`ev_92901eb893f9caf8d1c6411e9723c92fc4281775bdeaaa38960b355bdd99140f`,
`m4-completion-20261121/rea-flash-draw-live/particle-color-consumer-1.json`.
Caller 0x101755a0. Select a12-byte group at argument 4+argument 3*12: +0 owner,
+4 cursor,+8 count; zero count returns 0. Call argument 1 virtual+14, get owner
virtual+8c template. Template+90==1 chooses mode 1; else nonzero byte+1f2
chooses mode 2; otherwise 0. Cached COLOR1/COLOR2/COLORBLENDGRAPH globals
0x1025beec/ef0/ef4 upload template+1ac/+1bc/+12c respectively. Getter virtual
+1b4 uploads LightmapIntensityOffset via name lookup 0x1001e550. This consumer
never reads a vertex normal or establishes the effective LightAffectionFactor.
If template+8c buffer is nonnull, read its capacity through buffer+44 pointer;
walk group backward by 4-byte pointers, stopping when record+bc<threshold
argument 2. Decrement remaining buffer capacity, virtual+4c(record+4,slot),
0x101b00f0(record+5c,record+6c,slot), flush via virtual+50 and 0x101b02a0(mode,0)
when slot 0, then reset capacity. Each consumed record decrements group+8 and
increments hidden renderer-context+1c14. A partial batch pads while slots>1
using 0x10079980/virtual+4c, closes last slot via virtual+50 and flushes.
Finally subtract consumed count from group+4 and return 1. The decompiler's
hidden renderer-context/local 10 convention remains unresolved; these record
fields are not labeled as mesh-particle storage. No blend/lighting correction
is justified solely by this consumer.

### Auxiliary color consumer RendDX9.dll 0x10175330

Complete 428-byte body 0x10175330–0x101754db, evidence
`ev_fd926c48e44b09f05737265be00cce8cff2886748c5d24f6d6e76c8f6d80ce2a`,
`rea-flash-draw-live/particle-color-consumer-2.json`, caller 0x101754e0.
Walk ECX+1d84/+1d88 pointer vector. Each item virtual+38; argument byte and
item+7d choose global 0x1023b970 virtual+50 or+d4. Item+7c low byte selects
whether to draw in the requested branch. Optional cached WVP 0x1025bee4 is
uploaded from selected renderer virtual+84(0). COLOR1 and COLOR2 both upload
four 1.0 floats; COLORBLENDGRAPH uploads three zeros followed by float from
global 0x1024b914 virtual+270. LightmapIntensityOffset lookup uploads 1.0.
Call 0x101b00f0(1.0,item+80,0) and again withslot 1, then 0x101b02a0(1,0).
Advance each pointer; nonzero argument finally calls 0x100d7b10(0,0).
No geometry material or light-affection value is read here. This auxiliary
path is not established as the M4 mesh particle draw and does not justify
changing its lighting.

### MeshParticleMesh submission 0x101b02a0

Complete 712-byte body 0x101b02a0–0x101b0567, evidence
`ev_9f42802f2e53736a585c541571d2426c993f8ff2ffbd054f6bddb6a5da1dfa56`,
`rea-flash-draw-live/mesh-particle-submit.json`, callers 0x10174740/0x10175330.
Mesh material vector comes from this+48, descriptor+4/+8, stride 0x18. Calls
0x10014990,0x10014890(0),0x10014670 and global renderer virtual+50. Each
material+4 mode 1 is a transparent-material branch: argument 2 nonzero reports
MeshParticleMesh.cpp line 99; zero skips it. For other modes, argument 2 zero
selects technique by argument 1 **0=Diffuse,1=Additive,2=DiffuseWithZWrite**;
other values leave prior technique unchanged. Begin 0x1001d930, upload matrices
from this+38 with count through this+44 to cached 0x1025bf10 virtual+28;
upload AgeAndAlphaArray from 0x10266258 with same count and GlobalScale from
this+5c. Material+0 state+28 first texture goes to cached 0x1025bf08.
Begin pass 0 via 0x1001dd60; draw 0x10005790 receives material+10,+14,
geometry-template(this+84)+ac primitive type, material+8,0,material+c; end via
0x1001d990. Advance all materials. No normal lighting or light-affection
setter appears here. AlphaMode 0 does not itself determine the system's
technique; it is the separately passed argument 1 above.

### Legacy mesh-particle pass 0x101754e0

Complete 188-byte body 0x101754e0–0x1017559b, evidence
`ev_6b317963b4540301abbe6ab8eb4ea3bd3ea81fa6d047e7d4349872c0c8e12934`,
`rea-flash-draw-live/mesh-particle-system-draw.json`, caller 0x100d3440.
Return 0 unless vector this+1d84/+1d88 is nonnull/nonempty and enable byte+1d6c
is nonzero. Lazily initialize MeshParticles profiler using global 10261710 bit 1
and register teardown101da260; begin100479f0,100265d0, prepare 10175760,
call 10175330 with unchanged argument, then 100265e0/10047a00, return 1.
This names the legacy MeshParticles branch. It does not establish that every
ObjectTemplate Particle uses it; M4 legacy object binding remains to verify.

### Mesh-particle globals preparation 0x10175760

Complete 402-byte body 0x10175760–0x101758f1, evidence
`ev_433d5451ef19c4f5bacee7b6462dc939832a5fbc5186ed399af1d49d004e97a8`,
`rea-flash-draw-live/mesh-particle-prepare.json`, callers 0x101754e0/0x101755a0.
Optional cached WVP uploads this+1cbc. Global 0x1024b904 virtual+18 supplies
hemi data. Null selects Texture1 fallback100f1bf0(0). Nonnull uploads
HemiMapInfo via virtual+1c(data,0), HemiShadowAltitude from data+10 and Texture1
from data+14. Cached fog range/color upload global1023ddbc+a8/+bc. EffectSunColor
and EffectShadowColor upload global1024b914 virtual+260(0)/+268(0). No per-item
normal, blend-mode or light-factor default is set here. This establishes the
MeshParticleMesh hemisphere/light-manager bindings without inventing them.

### Renderer particle-pass dispatcher 0x100d3440

Complete 159-byte body 0x100d3440–0x100d34de, evidence
`ev_2cdf8bae34ea9e0e1677ba783d7ee58657bb178e03f5d62358f4b956c933ece0`,
`rea-flash-draw-live/renderer-particle-dispatch.json`, caller 0x100d4ea0.
Incoming ECX is particle-pass context (its root binding is not established here). Calls 1016f8f0(0) on this+108;
1016d740(0) on this+85f8;10174670(0) on this+5f30;
1016ec80(argument 1) on this+85f8;101755a0(argument 1,0) on this+5f30.
Argument 2 zero additionally calls 101754e0(0) on this+5f30 and 10172cc0(0),
10173f20(argument 1) on this+7ccc. Both branches then call 10171fd0(argument 1)
on this+108 and 101755a0(argument 1,1) on this+5f30. Argument 2 nonzero finally
calls 101754e0(1) on this+5f30. No other fields or branches. This establishes
that the legacy mesh-particle vector is at this-context+**0x7cb4/+0x7cb8**
(5f30+1d84/1d88), not at the renderer root+1d84. Root runtime singleton
binding still needs verification before sampling that vector.

### Implemented static receiver adapter and repeat evidence

`decal_projection.h` now supplies the recovered finite triangle clipper,
UV interpolation, orthogonal projection, measured 0.01 separation,60-degree
face gate, initial traversal depth 4 and 127-output-vertex guard. The local
static adapter carries template/root-transform identity from collision building
to the corresponding Scene placement; it ray-tests only that owner's visible
mesh. Exact-position seam adjacency is local plumbing, not a claim that the
unresolved Retail contact interface has identical topology/LOD selection.
Dynamic owners/terrain retain the existing fallback. No guessed normal offset
on the collision proxy, RNG distribution, new mark budget or blend mode is added.
Each projected GPU mesh is released on expiration/round reset/local eviction.

On the original fence asset (`fence_corrugated_3x12m_broken.staticmesh`), the
projected mark lies at local Z 0.178134695 against Retail's measured 0.178134689
(`tests/test_decal_projection.cpp` with `OBF2_GAME_DATA` set), and the rear
output at 0.104613056. Drawn, the old proxy quad at depth 0.1495 was hidden
(0 red diagnostic pixels) and the projected mark visible (1156). In two
25-second game runs the mark was found at the entry point and faded out; the
mean-absolute RGB differences of the stable, fading and late frames (24.45 /
13.05 / 0 and 19.84 / 10.27 / 0) are visibility measures, not a recovered alpha
or gain. Exact Retail pixel, size and atlas matching, camera equivalence and
global receiver eligibility remain **not measured**.

### Live receiver identity closed,21 November

`retail-mesh-binding-preflight/receiver-history.json` confirms traversal depth 4.
The active fence SimpleObject owner 0x1a2aafe4 has primary vtable 0x8fc4c0 and
secondary vtable 0x8edfb8 at+80. Its+148 is 0x0f715444, **exactly the active receiver
address**. These are process-local diagnostic addresses, not engine constants.
This confirms the constructor's per-placement receiver binding; optional
render-contact virtual-method/neighbor semantics remain **not measured**.
The first 15 s mesh-particle queue probe sampled 956 times and observed no live
items (root 0x0ef5ef9c, begin=end 0). This is insufficient to bind the M4 draw path:
its scheduling was about 16ms and a short/transient queue can be missed. No
blend/light correction relies on this empty observation. A bounded denser burst
probe records the root vtable as well; its performance is diagnostic only.

### Frame dispatcher 0x100300c0 — complete context inventory

The literal 0x1024b910 xref falls inside the **5,276-byte/802-line** frame
dispatcher 0x100300c0–0x10031561, not a small manager getter. Complete dossier
`rea-flash-draw-live/particle-manager-global.json`, evidence
`ev_5713aab8d1530e24ebb7f091977952d3a1f5b7affb82b7bc4ca094e2d560c24b`.
All blocks were reviewed; unresolved virtual/helper identities below remain
unresolved. This function alone does **not** identify global 0x1024b910 as the
particle-pass context, and the attempted live vtable guard rejected it.

Direct renderer-relative fields (byte offsets; widths unless stated 4):
+c device interface; +594/+598 first-person near/far inputs; +5a0/+5a1 branch
bytes; +5b8 capture flag,+5bc counter; +5c0 optional manager pointer;
+5d0 optional final manager,+5d4/+5d8 paired managers; +5e8 double frame delta;
+600 double FPS,+608 double accumulated interval,+610 frame count;
+69d/+69f rendering feature bytes; +6a0 decal branch byte,+6a4 particle branch
byte,+6a5/+6a6/+6a7/+6a8/+6aa feature bytes; +6ac optional helper flag;
+6b0 statistics visibility byte,+6b1/+6b2/+6b3/+6b4/+6b5 feature bytes;
+6c0/+6c4/+6c8 saved optional-manager state; +6d4 helper flag,+6dd final-helper
byte; +774 viewport/helper input; +820 histogram step,+824..+848 ten histogram
counts; +888 statistics-enable byte,+88c accumulated count,+890 float timing
sum; +894/+898/+89c display interfaces; +8a0/+8a8 double start/result clocks.
Virtual calls through this are+48/+50 world camera,+d4 first-person camera,
+ac predicate,+c8 begin branch,+cc finish branch,+d0 nonzero-original-delta
branch,+58 input-gated final branch. No semantic name is invented for an
unresolved feature byte or optional interface.

Complete control-flow outline: validate state-manager restoration, report
BF2Render.cpp assertion and call 10026650 on failure; clear optional global
1024b8f8+5c and begin10244ac0+28. Reset camera near/far and copy world to 1p
camera. Call 10038c30/10077d30; preserve original delta. Optional1000a580
branch establishes rendering state. Global1023b970+3f4 plus100032c0 selects
one or 100032d0 views. Initialize scratch matrices. For each view, optional
multi-view helpers 10031830/1007b800/100798d0/10031780/10078520/10078870
produce camera transforms; later views use delta 0. Save delta+5e8. The+ac
predicate and 1002bf50 gate100f2ea0. Update1004e720, optional WindManager+10;
+69d temporarily changes10244ad8 state around manager+5c0 camera/update;
+6b2 updates1024b918+10; nonnull+5d4/+5d8 and+69f call their+18;
+6a6/non-null1024b8f0 calls+24; always update1024b914+10;
+69f updates1024b900+28; always1024b908+30(delta),10029410(argument 2),
optional10267a34/101c0c60. The shared postprocessing gate is+6b5,
nonzero1023f7c0, and either !+5a1 or 1004f9b0.

If this virtual+c8(0,0) succeeds:100f03e0/1004e700, light manager+19c,
10005750 clear, optional100d3400 and 10039120. Populate cached shader globals
below; optional1024b930+78/10039530 and 1024b90c+b0/10039560. Optional+69d
manager draw,+6a5/global1024b930+b8; update cached resource+284 with release/
addref and compute viewport helper10084ea0, lazily initialize two coordinate
vectors. Nonnull hemisphere data1024b904+18 updates cached texture+138 with
refcount/release/addref, and metadata+140 through manager+1c. The conditional
10238249/1000a580/1023b970+516 state toggles wrap1003bad0 and optional
1024b8f0+20; optional+6a7/10258f38+34 draws against camera. Call
1024b930+bc when+6a5. +6a4 calls 1002abb0, then optional1024b90c secondary+24
virtual+c(0) and optional1024b910 virtual+18. +6b2 draws1024b918 secondary+4
virtual+c(0); +6b3 adds its+a8(0).

Draw-stage global1023e408 interfaces+3c/+40/+c/+1c/+10/+20 receive virtual+8;
+8/+48/+18/+58 and+28/+4c/+5c/+50/+60 receive virtual+c. +69d and
100f7f60 choose whether the+8/+48/+18/+58/+2c group is before or after+28;
+2c receives+8(1). +6a0 draws decal secondary interface1024b908+4 virtual+c(0).
+69f draws1024b900+2c in successive 0/1 modes. Optional+5d4/+5d8 close with
+1c. +6b2/+6b3 use1024b918+a8(1). The postprocessing gate calls 100504c0;
+5a0 selects clear mode 2 and is forwarded to four draw interfaces. Optional
1000a580 saves device query through+c virtual+a0 and changes virtual+9c.
The postprocessing gate lazily creates PostProduction profiler and wraps
10051ab0. +6a8 finds optional NametagManager and calls+18; +6b2/+6b3 draw
a8(2); +6b1/non-null+5d0 calls+1c; +6dd calls 10029500.

Statistics branch+888 increments+88c and adds unsigned 10047960(0) to+890.
Display+898 receives +8a8*1000,+894 receives 1/delta,+89c receives unsigned
10047960(0). Global10244aac+10 receives (9,0,0,0),(5,5,0,0),(4,6000,0,0).
Save/restore1000d250 state; optional10247c3c uses AI service predicate to emit
(3,4,0,0), then its+10. +6d4 calls 1002ebf0. Accumulate interval/count;
when interval>.33 recompute FPS, reset interval/count. Zero FPS uses 1/delta.
Increment the first matching descending histogram bucket 9..0 comparing
bucket*+820 with FPS. +6ac calls 1002a230; global1025a39c+70 calls 1002a190;
three statistic interfaces receive+28(+6b0). +6aa calls 1002f2e0;
10244acc+a4 and+6b4 gate this+58. Optional device-state restoration closes
the saved query; +69f calls 100d3420. Clear *1023de08; this+cc; optional
100034e0. Original nonzero delta calls this+d0 even if begin+c8 failed.
After all views, restore camera for multi-view; +5b8 invokes 10004000,
increments+5bc,100040c0. Finish1002e6d0,10244ac0+2c/+18, save measured double
result+8a8, return 1. Ghidra's query pointer/hidden temporaries and nonreturning
classification of 1005fa30 are not trusted as data identities.

All directly written shader-cache fields under global1023ddbc: +138
hemisphere texture,+140..+14c four metadata words,+154/+158/+15c half of
light-manager virtual+1d0 triple,+1f8/+1fc/+200 from+1e8,+214/+218/+21c
from+1e0,+230/+234/+238 from+1f0,+24c/+250/+254 from+1f8,+25c low byte
renderer+ac,+264 light-manager+2e8,+284 resource pointer,+294..+2a0 and
+2a8..+2b4 lazily initialized viewport vectors. Global1023d948 bits 1/2/4
initialize vectors/profiler. No M4 template name or per-particle color is
resolved in this dispatcher. It is retained whole to avoid another import.

### Particle-context singleton verified by vtable identity

`mesh-context-state.txt` uses bounded manager-global headers, matching only
vtable **RendDX9.dll 0x10219978**, whose+2c is the recovered dispatcher
0x100d4ea0 and whose downstream method is 0x100d3440. Exactly one match:
**global 0x1024b900**, object 0x0ef8a074, offset 0, live vtable 0x10a29978 at module
base 0x10810000. Its queue at+7cb4/+7cb8 is empty at rest. This resolves context
identity by the original method table, not a nearest-address guess.
Earlier probes at renderer-global 0x1023d098 and candidate 0x1024b910 were wrong
contexts and are **diagnostic only**; their empty results say nothing about M4
routing. The corrected probe rejects ambiguous/missing exact vtable matches
before any queue interpretation. A following bounded burst tests live names;
no shader correction is made merely from the singleton binding.

### M4 legacy route observed; Additive is now established

`retail-mesh-bound-burst/mesh-particles.txt` records 5,053,368 bounded reads,
no rejected samples, and 3,943 live queue records. Exactly one geometry-template
name appears: **p_muzz_m4_1p**, template vtable 0x1021cc68, item vtable 0x10221b90
(preferred addresses). The raw object/template bytes and summary
`mesh-particles.json` are retained. The queue's exact vtable binding and
0x100d3440 ->0x101754e0 ->0x10175330 show the M4 uses the **legacy** path.
0x10175330 passes **(1,0)** to 0x101b02a0, whose technique branch is Additive.
Therefore MeshParticleMesh.fx:88–95/120–138 applies: texture RGB multiplied by
texture alpha, ONE/ONE blend, no culling, no fog, depth tested/no depth writes,
and **no normal/hemisphere lighting**. The previous ordinary static-mesh
lighting/standard alpha path is unsupported for this M4 plane.

Instance scalar writer 0x101b00f0, all 31 bytes 0x101b00f0–0x101b010e, evidence
`ev_3ab8666af39324f164b915a344ceec4ee90d3ca4a82a9c61bc4476518857631e`,
`rea-flash-draw-live/mesh-particle-instance.json`: write input 1 to global
0x10266258+index*16 and input 2 to 0x1026625c+index*16; remaining two words
untouched. No branches/calls/strings. Shader binds these as age and alpha.
Thus the legacy call writes **age 1.0 and alpha=item+80**, not a measured item
age. Both slots 0/1 receive the same values. Do not infer TTL from item+80.
Probe performance/flash timing is diagnostic only; clean reference repeats
`retail-clean-flash-1/2` omit it.

### Supported flash correction staged in source

The authored planar first-person `Particle` now uses an additive, unlit,
two-sided pipeline with depth testing and no depth writes. GPU SRC_ALPHA/ONE
on unlit RGB is algebraically equivalent to MeshParticleMesh.fx masking RGB
by alpha then ONE/ONE. Do not duplicate the authored triangles under CullNone.
The shader's existing unlit branch returns before fog/normal lighting. The
main Particle's CRD_NONE timeToLive is read from its authored template; for M4
it is 0.02 s (`p_muzz_m4_1p.tweak:10`), replacing the unsupported 45 ms fallback
only for supported positive constant lifetimes. Unsupported lifetime distributions
retain the old fallback. Authored expiry uses monotonic elapsed action time so
skipped/acquisition frames cannot lengthen the particle. No 20 ms lifetime is
asserted for the complete bundle or overlapping microglow/smoke children.

Independent diagnostic queue objects 0x179044f4/0x17904034/0x1790432c were
observed for 19.358/19.431/18.618ms respectively; others reuse addresses across
shots, so their full first/last ranges are not lifetimes. All observed legacy
item+80 values equal 1, agreeing with constant opacity. The clean no-probe
references are saved under `retail-clean-flash-1/2`; after captures and GPU blend
verification remain pending. This section is implementation status, not a new
visual parity claim.
