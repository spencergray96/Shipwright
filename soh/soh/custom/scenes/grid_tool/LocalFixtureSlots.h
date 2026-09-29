#pragma once

// Marker for local fixtures (sturdy-bassoon tools/fixtures): its presence says this branch's scene tables
// carry the reserved SCENE_LOCAL_FIXTURE_n slots. A fixture's folder is gitignored, so it stays in the tree
// when an older branch is checked out; every source file in it is wrapped in
// `#if __has_include("../LocalFixtureSlots.h")`, so on a branch without this header it compiles to nothing
// rather than failing on an undeclared SCENE_LOCAL_FIXTURE_n.
#define LOCAL_FIXTURE_SLOT_COUNT 8
