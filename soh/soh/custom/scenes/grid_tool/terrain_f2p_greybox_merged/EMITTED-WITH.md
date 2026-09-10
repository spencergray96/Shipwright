# terrain_f2p_greybox_merged - emitted with

**Backfilled by hand 2026-09-09, not written by the emitter.** The scene predates
`EMITTED-WITH.md`; this file records what it was emitted with and how that was established, so it
is not guessed at again. The scene's own C is untouched - re-emitting it would pick up changes the
emitter has had since, which is exactly what this file exists to make visible rather than silent.

```
npx tsx ../terrain/emit-scene.ts --merge --merge-tolerance=1
```

| | |
|---|---|
| field | `baked/field.json` |
| field sha256 (first 16) | `8c7021ad613b708a` (unchanged since 2026-08-26) |
| merge | on, **tolerance 1** - not the default 0 |
| collision polys / vertices | 12604 / 8840 |
| scene committed | `76c1b9c26`, 2026-08-26 |

**How this was identified.** Dry-running the emitter at each candidate setting and matching the two
counts the scene's own `_scene_col.c` declares. Tolerance 1 gives 12,604 / 8,840; tolerance 0 gives 25,784 / 13,689; tolerance 2 gives
9,792 / 7,155. No other setting on this field produces
those numbers, so the identification is exact rather than inferred.

> **This is the scene that visibly speckles**, and the tolerance is why: above 0 a merged rect's
> corners are not exactly coplanar, so a neighbouring cell's vertex sits genuinely *off* the merged
> edge and leaves a real sub-unit gap. See `docs/notes/2026-08-26-terrain-merge-factor.md`, which
> photographed it, and `docs/test-runs/2026-09-09-t-junction-acceptance/`, where it was briefly
> mistaken for the T-junction artifact of issue #95. T-junction closure does not fix it; a lower
> tolerance does.