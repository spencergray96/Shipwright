# terrain_f2p_greybox - emitted with

**Backfilled by hand 2026-09-09, not written by the emitter.** The scene predates
`EMITTED-WITH.md`; this file records what it was emitted with and how that was established, so it
is not guessed at again. The scene's own C is untouched - re-emitting it would pick up changes the
emitter has had since, which is exactly what this file exists to make visible rather than silent.

```
npx tsx ../terrain/emit-scene.ts (no flags)
```

| | |
|---|---|
| field | `baked/field.json` |
| field sha256 (first 16) | `8c7021ad613b708a` (unchanged since 2026-08-26) |
| merge | **off** - one quad per cell |
| collision polys / vertices | 32856 / 16697 |
| scene committed | 2026-08-25/26 |

**How this was identified.** Dry-running the emitter at each candidate setting and matching the two
counts the scene's own `_scene_col.c` declares. A raw bake gives 32,856 / 16,697, which is what the emitter reports as the
`raw would have been` baseline on every merged run of this field. No other setting on this field produces
those numbers, so the identification is exact rather than inferred.

The unmerged baseline every merged variant of this field is measured against. Having no merge, it
has no merged/unmerged boundaries and therefore no T-junctions - `npm run audit:seams -- --merge-off`
reports zero for it.