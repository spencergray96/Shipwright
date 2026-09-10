# terrain_f2p_settlement — retained on purpose, superseded for testing

**This scene is stale and it is kept that way deliberately.** Do not re-emit it. If you want the
current bake of this content, use `terrain_f2p_settlement_sealed`
(`SCENE_TERRAIN_F2P_SETTLEMENT_SEALED`, 0x8A, `entrance 0x633`) — same field, same composite, same
placement, emitted 2026-09-10.

| | |
|---|---|
| this scene | `SCENE_TERRAIN_F2P_SETTLEMENT`, 0x83, `entrance 0x62C` |
| emitted | 2026-08-26, Shipwright `08f51dfd0` |
| superseded by | `terrain_f2p_settlement_sealed`, 0x8A, `entrance 0x633`, 2026-09-10 |
| still opted in for music? | **yes** — see `sturdy-bassoon/tools/music/zones.json` |

## Why it was kept

It tears along polygon seams — the artifact reported in
[#95](https://github.com/spencergray96/sturdy-bassoon/issues/95), where the skybox shows through
hairline gaps and the specks crawl when the camera moves. The re-bake fixes that, and this scene is
retained as a walkable specimen of the world *before* the fix rather than being overwritten by it.

**Verified 2026-09-10**, by a human looking at both: `0x633` has the corrected visuals, `0x62C`
still tears. Not inferred from the exporter change — seen.

It stays in the music zone table alongside its replacement, at the same coordinate anchor, so the
two can be compared back to back with the zone director running in both. Warp between `0x62C` and
`0x633` and only the geometry changes.

## What it is NOT: a clean T-junction control

**Read this before drawing a conclusion from the difference between the two scenes.** They are six
weeks of toolchain apart, not one fix apart. Between this bake and the re-bake, roughly a dozen
commits touched the emitters — wall-run and floor-slab merging, wall carving, world-space stone
tiling, corner-square absorption, and only latterly #95's T-junction closing. That is why the
re-baked mesh is the *smaller* of the two (135,655 lines against 178,804) even though stitching
adds vertices: the merging work more than paid for it.

So a visual difference between `0x62C` and `0x633` is evidence about **the toolchain as a whole**,
not about T-junctions specifically.

For an isolated T-junction A/B, use the pair built for exactly that purpose instead:
`terrain_f2p_seam_bad` and `terrain_f2p_seam_good`, emitted the same day from the same field with
nothing between them but `--no-stitch`. See `terrain_f2p_seam_bad/EMITTED-WITH.md`.

## What this scene is good for

- Seeing what the world looked like on 2026-08-26, walkable, without a checkout and a rebuild.
- The "before" half of a toolchain-wide before/after, with the caveat above stated out loud.
- A fixture that is *known* to exhibit the #95 artifact, if something needs to be tested against a
  scene that has it.

Emitted originally with (do **not** run this — it would overwrite the artifact this file exists to
preserve, and against a newer toolchain it would not reproduce it anyway):

```
npx tsx ../terrain/emit-scene.ts --merge --composite="Lumbridge Settlement X3@56,64" --name=terrain_f2p_settlement
```
