#pragma once

// Marker for local fixtures (sturdy-bassoon tools/fixtures): this branch's scene tables carry the reserved
// SCENE_LOCAL_FIXTURE_n slots. A fixture's folder is gitignored, so it stays in the tree when an older branch
// is checked out. Every source file in it includes ../GridToolSceneData.h (which includes this) and is
// wrapped in `#ifdef LOCAL_FIXTURE_SLOT_COUNT`, so on a branch without the slots it compiles to nothing
// rather than failing on an undeclared SCENE_LOCAL_FIXTURE_n. See GridToolSceneData.h for why the test is
// a macro and not __has_include.
#define LOCAL_FIXTURE_SLOT_COUNT 8
