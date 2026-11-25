# Blended materials

Some materials are drawn by their texture's alpha over what lies behind them:
vehicle glass, a scope's lens, a glow. `obf2::mesh::materialAlphaBlend` already
says which ones (`alphaMode` 1, or "alpha" in the technique without
"alpha_test"). The renderer (`gfx::MeshRenderer`) draws them in a pass of their
own, after everything opaque.

## Which materials blend

`mesh_info <mod> --alpha` over every mesh of the mod lists the kind, the shader,
the technique and `alphaMode` of every material. The rows the rule picks up:

| shader | technique (material) | alphaMode | materials | example |
|---|---|---|---|---|
| `BundledMesh.fx` | `Alpha` | 1 | 117 | `mobileradar_dest.bundledmesh` |
| `BundledMesh.fx` | `AlphaEnvMap` | 1 | 103 | `jep_technical.bundledmesh` |
| `BundledMesh.fx` | `AlphaNoHemiLight` | 1 | 7 | `aav_tunguska.bundledmesh` |
| `BundledMesh.fx` | `Alpha` | 0 | 2 | `rulmg_pkm.bundledmesh` |
| `StaticMesh.fx` | `Alpha_One` | 1 | 11 | `glow.bundledmesh` |
| `StaticMesh.fx` | `alpha_one` | 1 | 1 | `p_tracer_tank.staticmesh` |

No `SkinnedMesh.fx` material blends, and neither does any static mesh drawn by
`RaShaderSTM.fx`: its only blend is the point-light pass (ONE / ONE).

The M4's mesh (`usrif_m4.bundledmesh`) has two materials: `EnvMapColormapGloss`
for the weapon, and `Alpha`, 32 triangles, for the scope's lens.

## The states

From the effect files in `Shaders_client.zip`:

| technique | where | ZWriteEnable | CullMode | SrcBlend / DestBlend | AlphaTest |
|---|---|---|---|---|---|
| `alpha` | BundledMesh.fx:668 | FALSE | NONE | SRCALPHA / INVSRCALPHA | TRUE, GREATER 0 |
| `alphascope` | BundledMesh.fx:756 | FALSE | NONE | SRCALPHA / INVSRCALPHA | FALSE |
| `alpha_one` | StaticMesh.fx:423 | false | NONE | SRCALPHA / ONE | true, GREATER 0 |

`psAlpha` (BundledMesh.fx) returns the diffuse map's alpha as it is; the light
only scales the colour.

So ours: a `BundledMesh.fx` blend material mixes (SRCALPHA / INVSRCALPHA), a
`StaticMesh.fx` one adds (SRCALPHA / ONE), and both are drawn from both sides
with no depth write and an alpha test at 0. A bundled mesh drawn posed (the
weapon in hand) takes the skinned vertex stage with the same states.

## The order

A blend without depth writes is only right over what is already behind it. The
renderer draws the sky, the solid and skinned meshes and the roads first, then
every item with a blended range, the farthest first (by the clip-space w of its
bounds' centre).

## Not established

- When the engine picks `alphascope` over `alpha`: no material names it, so
  the engine chooses it. Ours always takes `alpha`; the two differ only in the
  alpha test.
- The order the original sorts its blended geometry in. Ours is per item, not
  per triangle.
- A frame dump of the original with glass in view, to compare against. The
  screenshots in the pull request compare our own before and after.
