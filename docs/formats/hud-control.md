# What drives the HUD

`hudBuilder` only **builds** the node tree: it creates nodes, gives them
coordinates, textures and show conditions. It moves nothing. What makes
the bottom bar slide out while entering a vehicle, and the panels fade
smoothly, is another layer — **the action graph in the `MemeFile 2.0`
file** (`Menu/Ingame`).

That is visible from the class list in the file itself:

| class | count | what it does |
|---|---|---|
| `SetVariableSineAction` | 2 | drives a variable to a target along a sine |
| `SetVariableSoftAction` | 3 | drives a variable to a target smoothly |
| `SetVariableAction` | 2 | sets the value at once |
| `ActionListAction` | 3 | a list of actions run together |
| `CullVariableActionNode` | 5 | runs actions when a variable changes |
| `ToggleData` | 2 | switches between two values |
| `EqualData`, `AndData`, `OrData`, `NotData` | 6 | logic |
| `AlphaFadeEffect`, `VariableColorEffect` | 12 | fading and colour |

The same four operations — `EQUAL`, `AND`, `OR`, `NOT` — also appear in
`setNodeLogicShowVariable` in the `.con` files. So the condition language
is shared by both layers.

## The bottom regions' movement

The two regions from the developers' `Readme.txt`, `BottomLeftAnimate` and
`BottomRightAnimate`, are `BfTransformNode`s (400×64 and 600×100). Their X
position is not written down as a number: it comes from a variable, and an
action drives that variable.

The values stored in the file:

```
BottomRight/BottomRight_XPos      503     current
BottomRight/BottomRight_oldXPos   201
BottomRight/BottomRight_newXPos   503
BottomLeft/BottomLeft_XPos       -295
BottomLeft/BottomLeft_nextXPos   -295
SetVariableSineAction  Speed 600
SetVariableSoftAction  Speed 10
```

So the right-hand region travels between **201** and **503**. It is 600
wide, and 201 + 600 = 801, meaning 201 is the shown position flush with
the screen's right edge at 800, and 503 is hidden past that edge. The left
one at −295 (400 wide) is hidden past the left edge, since −295 + 400 =
105.

Movement speed is 600 (sine), fade speed is 10.

## What is missing

`BfTransformNode` has `X` and `Y` fields of type "data", but they are
empty in the file: variables are bound by name at run time. Because of
that the file **does not show the Y** of these regions — it has to be
taken from the game's appearance rather than from the data. For their
static neighbours Y is written down directly: `BottomLeftStatic` X=−1
Y=563, `BottomRightStatic` X=401 Y=563, both 400×64.

For two classes — `FloatRefData` and `AlphaFadeEffect` — we have no field
table: they export no `onStream` of their own, so they are read through
the parent's. The file still parses whole, with nothing left over, but it
is exactly those two that hold the "reference a variable by name" link.
That is the next step should the animation ever need reproducing in full.
