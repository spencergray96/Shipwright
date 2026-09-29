#ifndef SOH_RS_SUNS_SONG_H
#define SOH_RS_SUNS_SONG_H

// ============================================================================================
//  SUN'S SONG, IN PLACE  (sturdy-bassoon#159)
// ============================================================================================
//
// Our custom scenes keep time still (envCtx.timeIncrement == 0), and in a time-still scene vanilla's
// Sun's Song does not speed time up: it RELOADS the scene at the other end of the day, through the
// entrance the player came in by (Interface_Update, z_parameter.c, after "handle suns song in areas
// where time moves"). In a one-scene overworld that entrance is the scene's single spawn, so the song
// put Link back at the spawn from anywhere in F2P.
//
// In a compiled-in custom scene (GridToolSceneRegistry) the song hands off here instead, and the time
// changes where Link stands, behind a fade in the colour vanilla's reload uses:
//
//   FadeOut (kFadeOutTicks) --> swap the clock --> Hold (kHoldTicks) --> FadeIn (kFadeInTicks) --> done
//
// The swap is what the reload did to the clock and nothing more: dayTime and skyboxTime to midnight
// when played by day, to 0x8001 by night - vanilla's own targets - and nightFlag by the engine's
// threshold. By night, the new day is counted the way Play_Init counts it after the reload (totalDays,
// bgsDayCount, dogIsLost), and the rooster or the dog is queued through nextDayTime (0xFFFE, 0xFFFD),
// which Environment_Update counts down to the sound. A hatching egg's message waits for the fade to
// finish. The rest of the reload is deliberately not reproduced: music and ambience carry on (the
// zone director owns music in these scenes), no actor is re-spawned, and there is no respawn point to
// move because Link never left.
//
// That holds while our scenes stay as they are: layer 0 always (z_play.c), no placed actor that reads
// the time at init, and nature ambience off (NATURE_ID_NONE, or 0xFF in one scene), which keeps
// Environment_Update's day-driven music and ambience machine (func_80075B44) off. A scene that gives
// any of those up needs this looked at again.
//
// Link is frozen for the fade with CsAction 1 and no cutscene actor - the staircase controller's
// freeze (Stairs.cpp) - with every other actor halted, as the reload halts them, and given back with
// CsAction 7.
//
// THE SEAM is VB_SUNS_SONG_RELOAD_SCENE, which Interface_Update asks before it reloads: this answers
// false in a scene the registry knows, and the engine never includes an rs/ header. Scenes the
// registry does not know, and rooms or scenes that forbid the song, keep vanilla's behaviour exactly:
// those checks are made before the question is asked. Nothing is declared here - the file is the
// contract, and SunsSong.cpp registers its answer from ShipInit.
//
// Markers, in every session (AgentTest_WriteMarker):
//   rs_suns event=begin scene=0x<hex> from=0x<hex> to=0x<hex> fill=<black|white> pos=<x>,<y>,<z>
//   rs_suns event=swap time=0x<hex> night=<0|1> total_days=<n>
//   rs_suns event=done ticks=<n> time=0x<hex> pos=<x>,<y>,<z>
//   rs_suns event=abort reason=scene_changed ticks=<n>
// `pos=` on begin and done is the claim: the same position twice is "no teleport".

#endif
