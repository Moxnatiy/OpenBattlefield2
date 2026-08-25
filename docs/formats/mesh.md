# Меші: `.staticmesh` / `.bundledmesh` / `.skinnedmesh`

Статус: **реалізовано** — `src/mesh`. Розібрано **1635 з 1635** мешів BF2 1.5
(1105 static, 506 bundled, 24 skinned), 8.94 млн вершин, 2.22 млн трикутників
у lod0. Жодної помилки.

Джерело розкладки: [Project Dalian](https://github.com/chronic8000/ProjectDalian)
(MIT, `engine/formats/mesh`) та [BfMeshView](http://www.bytehazard.com/bfstuff/bfmeshview/).
Реалізація власна — з перевіркою меж на кожному читанні.

## Одне сімейство, три розширення

Усі три формати — той самий контейнер; тип **неможливо визначити з вмісту**,
лише з розширення файлу. Різниця зводиться до кількох гілок:

| | static | bundled | skinned |
|---|---|---|---|
| `alphaMode` у матеріалі | є | є | **нема** |
| вузли (матриці) у lod | є | лічильник є, матриць **нема** | нема |
| rig-и з кістками | нема | нема | є |
| `u2` після індексів | є | є | **нема** |
| `bounds` матеріалу (v11) | є | є | нема |

У BundledMesh трансформи частин (башта, ствол, колеса) лежать не в меші, а в
`.con` (`geometryPart`) — тому лічильник вузлів є, а матриць нема.

## Порядок читання (little-endian)

```
Header      u32 u1, u32 version, u32 u3, u32 u4, u32 u5
u8          маркер гри (1 = Battlefield Play4Free)
u32         geomCount
  u32         lodCount           ← лише лічильники; самі lod-и в кінці файлу
u32         attributeCount
  u16 flag, u16 offset, u16 vartype, u16 usage
u32         vertexFormat         ← розмір компонента, завжди 4
u32         vertexStride         ← байтів на вершину
u32         vertexCount
float[]     vertexCount * stride/format
u32         indexCount
u16[]       indexCount
u32         u2                   ← крім skinned
для кожного geom, для кожного lod:
  float3 min, float3 max
  float3 pivot                   ← лише version <= 6
  skinned: u32 rigCount, для кожного: u32 boneCount, {u32 id, float[16]}[]
  інакше: u32 nodeCount, float[16][nodeCount]   ← матриці лише в static
для кожного geom, для кожного lod:
  u32 materialCount
    u32 alphaMode                ← крім skinned
    string fxFile, string technique      (string = u32 довжина + байти)
    u32 mapCount, string maps[]
    u32 vertexStart, indexStart, indexCount, vertexCount
    u32 nodeIndex, u16 u5, u16 u6
    float3 boundsMin, float3 boundsMax   ← лише version == 11 і не skinned
```

Ключова несподіванка: **таблиці geom/lod розділені**. На початку файлу лежать
самі лічильники, а вміст lod-ів — аж наприкінці, двома окремими проходами
(спершу всі вузли, потім усі матеріали). Читати їх треба саме в такому порядку.

## Атрибути вершин

`usage` — це `D3DDECLUSAGE` з DirectX 9: `0` = POSITION, `3` = NORMAL,
`5` = TEXCOORD, `6` = TANGENT. `flag != 0` (у файлах трапляється 255) означає
вимкнений канал — такі атрибути треба пропускати, інакше зсуви поїдуть.

TEXCOORD буває до трьох: базова розгортка, детейл і запечена лайтмапа.
Для геометрії досить нульового.

Індекси в матеріалі відлічуються **від `vertexStart` цього матеріалу**, а не від
початку буфера — при розпакуванні їх треба зводити до абсолютних.

## Безпека

Файли приходять з архівів користувача, тому парсер побудований навколо читача
з перевіркою меж: будь-яке читання за межі переводить його в стан помилки
назавжди, а кожен лічильник звіряється з тим, скільки байтів фізично лишилося
у файлі. Тести перевіряють обидва випадки — обрізаний файл на кожному зсуві
й підмінений лічильник `0xFFFFFFFF`.

## Перевірка

```bash
./build/macos-arm64-debug/tools/mesh_info/mesh_info "Game Files/mods/bf2" --all
```

Один меш докладно:

```bash
./build/macos-arm64-debug/tools/mesh_info/mesh_info "Game Files/mods/bf2" \
  objects/water/meshes/waterplane_128.staticmesh
```

Показує 4 вершини, 2 трикутники, bbox `-64/0/-64 .. 64/0/64` — саме
128×128 площина води, як і має бути. Зручний спосіб переконатися, що парсер
не бреше.
