# The coordinate system: the engine is left-handed

## How we found out

`RendDX9.dll` (BF2's renderer) imports exactly these D3DX functions:

```
D3DXMatrixLookAtLH
D3DXMatrixPerspectiveFovLH
D3DXMatrixOrthoLH
D3DXMatrixOrthoOffCenterLH
D3DXMatrixRotationYawPitchRoll
```

Not a single `*RH` among them. Refractor 2 is a DirectX 9 engine and its
coordinate system is **left-handed**: X right, Y up, **Z into the
screen**.

## Why that gave us a mirrored world

Our pipeline was right-handed and self-consistent, so depth, culling and
fog all worked. But "right" is built differently:

| | the screen's right axis |
|---|---|
| `D3DXMatrixLookAtLH` | `cross(up, forward)` |
| right-handed `lookAt` | `cross(forward, up)` |

Those are exactly opposite vectors. With the same data and the same
camera, a right-handed pipeline produces a **horizontally mirrored
frame**. Left and right swap across the whole world at once — and that is
exactly how it looked.

## What we changed

- `perspective` — by the `PerspectiveFovLH` formula (`m[11] = +1`, depth
  stays in the [0,1] range, as Metal/D3D want);
- `lookAt` — by `LookAtLH`: `s = cross(up, f)`, third row `+f`;
- the pipeline's front-face winding — **clockwise**: the mesh data did not
  change, the side we look at them from did;
- a zero rotation now looks along **+Z**, both in the camera and in
  movement on the server.

The last one is not a guess either: in the level's data the carrier stands
at zero rotation, and its bow (`us_carrier_wasp_front`) has a **larger** Z
than the stern (`us_carrier_wasp_back`). So the model's front is +Z.

## What was checked separately

Object rotations (`rotationYawPitchRoll`) already matched
`D3DXMatrixRotationYawPitchRoll`: our matrix is the same one, transposed
for multiplication by a column vector.
