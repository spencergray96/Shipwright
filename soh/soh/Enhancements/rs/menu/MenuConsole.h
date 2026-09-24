#ifndef SOH_RS_MENU_CONSOLE_H
#define SOH_RS_MENU_CONSOLE_H

#include <stdint.h>
#include <string>
#include <vector>

// The one parser + renderer behind BOTH console surfaces for the mod-owned pause interface
// (sturdy-bassoon#111), the arrangement RegionConsole.h describes and for the same reason:
//
//   - the human `menu ...` command, registered here, which joins `lines` with newlines into the
//     ImGui console, and
//   - `agenttest menu ...` (agenttest/AgentTest.cpp), which writes each line as its own
//     `[agenttest] rs_menu <line>` marker in agent-log.txt.
//
// One implementation, two sinks, so the two can never drift.
//
// THIS IS THE MENU'S TEST SURFACE, not a convenience. The menu is a full-screen panel with no
// gameplay side effects, so without it the only evidence a run could take is a screenshot - and a
// screenshot cannot say which page the ring is on, whether the world is frozen, or whether the
// trigger has a binding at all. `dump` exists so later stages can assert state from a marker alone.
//
// PAGE NUMBERS ON THIS COMMAND ARE 1-BASED, and every line that reports a page prints its number beside
// its `id=`/`title=`. Since stage 8 the ring is 1 `quest_journal` "Quest Journal", 2 `items` "Items",
// 3 `equipment` "Equipment", 4 `quest_status` "Quest Status" - so `menu page 3` is the Equipment page.
// (Internally the ring is 0-based; that index is never printed.)
//
// `args[0]` is the subcommand:
//   open                  opens the scroll, always at level 0 of the current page. REFUSALS ARE
//                         NAMED: `error=kaleido_open` when vanilla pause is up (one menu at a time,
//                         and this is the one that yields), `error=no_play`, `error=no_pages`,
//                         `error=disabled`. Opening an already
//                         open menu is not a refusal - it reports `was_open=1` and rc=0.
//                         IT RETURNS WHILE THE MENU IS STILL RISING (`phase=opening`): the scroll
//                         slides up from below the screen over 8 game ticks and the world dims as
//                         it comes. A screenshot taken straight after it catches the menu half off
//                         the bottom edge and reads as a broken layout. There is no `open now` -
//                         wait about a second, or poll `dump` until `phase=open`
//   close [now]           starts the closing SLIDE, the way B and START do; the freeze and the HUD
//                         are restored when it finishes, not when it starts, because un-freezing
//                         halfway down would show the world moving under a menu still on screen.
//                         `now` skips the slide and restores everything on the spot - which is what
//                         a run wants when the next command must not race the animation, and what a
//                         scene load takes internally. Idempotent either way: closing a closed menu
//                         reports `was_open=0` and rc=0, because "nothing to do" is not a refusal
//   page <n>              selects page n, 1-based. Out of range is rc=1 and never a clamp - a
//                         silent clamp would let a run assert a page it never reached. Works while
//                         closed; the ring is state, not a view
//   primary [custom|vanilla]
//                         which menu START opens, written to a CVar so it survives the session.
//                         With no argument it only reports. This is the console's job because SoH
//                         has no `set` command, so a CVar the agent loop must change needs a
//                         subcommand of its own. THE DEFAULT IS `custom` SINCE STAGE 6, and vanilla
//                         pause is reachable only through `primary vanilla` - but a value already
//                         SAVED in the config beats the default, so a config that ever stored 0
//                         still reads `primary=vanilla` until `primary custom` is run once
//   sweep [l|r|loop|hold <l|r> <tick>|stop]
//                         STAGE 5. Rolls the scroll one page, the way a shoulder press does: the
//                         side on that shoulder - roll end and hand - travels in until the hands
//                         meet, the scroll is shut, the page content swaps, and the same side
//                         travels back out. `l` rolls back, `r` forward; with no argument it only
//                         reports. Refused (rc=1, dropped rather than queued, because a queued press
//                         would let a run assert a page the animation never rolled to) with a named
//                         kind: `error=busy` while a roll is running, `error=level` in a detail
//                         view or while going between levels (the ring is the top level's), and
//                         `error=no_ring` when the ring has fewer than two pages. It exists beside
//                         `agenttest press L|R` because a mid-sweep screenshot has to be taken at a
//                         known tick: the line carries `tick=`/`of=`/`dx=`/`width=`, so a capture is
//                         self-describing. `dx=` is the roll's own displacement only, and
//                         `stick_rolls=`/`dpad_rolls=` (#129) how many of `sweeps=` an outward push
//                         from a hand started, by stick and by D-pad - the one roll cause a node id
//                         cannot show, since a shoulder, A, the stick and the D-pad all leave the
//                         cursor on the same node. Both count when both were pushed at once.
//                         `loop` keeps sweeping, alternating direction, until `stop`, and IT IS THE
//                         INSTRUMENT FOR THIS STAGE rather than a convenience: one sweep is half a
//                         second and a command round trip is seconds, so no run can photograph a
//                         chosen tick of a single sweep. Under a loop every capture lands on some
//                         tick of a live animation and a burst of them samples the whole excursion.
//                         `hold <l|r> <tick>` parks the animation AT one tick instead - the same
//                         agent-loop problem from the other end. A loop gives a distribution; a hold
//                         gives a LOOK at a chosen frame ("is the parchment in register at the
//                         peak", "which hand carries the twist"), which is otherwise a race against
//                         half a second. The swap is played forward rather than assigned, so a held
//                         frame is one the animation really produces. rc=1 on a tick out of range.
//                         `stop` releases a hold and abandons the sweep in flight
//   cursor [left|right|up|down|select|<id>]
//                         STAGE 5. The cursor graph, whose end nodes are the two hands. With no
//                         argument it only reports. A direction moves one step and is rc=1 when
//                         that direction has no neighbour - refused rather than clamped, so a run
//                         cannot report reaching a node it never reached. `select` is what A does:
//                         on a hand it starts the sweep that hand implies ("selecting a hand does
//                         what L/R does"), on an item whose page has a detail view (a quest row) it
//                         goes down a level and reports `select=descend`, and on any other item it
//                         reports `select=item` and does nothing. Anything else is taken as a node
//                         id and is rc=1 when no node carries it. There is NO graph in a detail
//                         view (`node=none nodes=0`); up/down scroll the journal there instead.
//                         ⚠ The INPUT path (D-pad, stick) does not log a refused move - only this
//                         command prints `error=no_neighbour` - so a run asserting a refusal from
//                         real input reads the node back
//                         THIS IS THE ONLY WAY TO ASSERT THE CURSOR. A screenshot shows a highlight
//                         box; it cannot say which node the graph thinks the cursor is on, and
//                         adjacency here is the graph's rather than the pixels'
//   level [down|up|loop|hold <down|up> <tick>|stop]
//                         STAGE 6. Going down into a detail view and back: close to the centre,
//                         turn counter-clockwise to vertical, open vertically - and the same path
//                         reversed going up. `down` is what A on a quest row does (it goes into the
//                         row the cursor is on), `up` what B does in a detail; with no argument it
//                         only reports. The line carries `level=` (0 the page, 1 its detail),
//                         `moving=`, `tick=`/`of=`, `pose=` (where the scroll is along the DOWNWARD
//                         path, 0 flat to `of` vertical, whichever way it travels), `phase=`
//                         (rest/close/turn/open), `sep=` (roll centre to roll centre in the scroll's
//                         own frame) and `angle=` (degrees counter-clockwise). All refusals are one
//                         kind, `error=refused` - not settled, already moving, wrong level, cursor on
//                         a hand, or a page with no detail view; the state on the line says which.
//                         `loop` and `hold` are `sweep`'s two instruments for the same reason: the
//                         gesture is 1.2 s and a round trip is seconds. `hold` refuses a bad tick
//                         as `error=range`; `stop` releases either and lands on whichever level the
//                         animation had reached. Every level change a hold plays through counts in
//                         `swaps=`, so that field counts holds as well as real gestures.
//                         #130 adds `drop=` (how far below its game space the scroll draws this
//                         tick: the entry slide, the probe's offset and the fixed 3-unit drop), `hand_l=`/`hand_r=` (each hand's
//                         last drawn extent, x0,y0,x1,y1 on screen, y down, read through its own
//                         matrix - at level 1 rest they run off the bottom and top edges),
//                         `hud_shown=` (START's and A's share of their alpha), `b_shown=` (B's),
//                         `b_moved=` (B at its level-1 spot) and `detail_rect=` (the journal's band)
//   filler [n]            STAGE 6, TEST-ONLY. Appends n synthetic rows (0-60) to the quest list after
//                         the real ones, so it has more rows than fit and scrolling can be driven;
//                         each has a synthetic journal long enough to scroll. `filler 0` removes
//                         them. Not a CVar - nothing survives the session. rc=1 on `error=range`
//   stress [<n> [same]|off|memo <on|off>]
//                         STAGE 7, TEST-ONLY. Replaces the visible page's level-0 body with n glyphs
//                         laid out row by row across the horizontal page at the journal scale, so a
//                         run can choose how dense a frame is and read its cost off `agenttest perf`.
//                         `0` is on with an EMPTY page (the chrome-only bracket), `off` restores the
//                         page and puts the memo back how the first `memo` flip found it. `same`
//                         repeats one character instead of cycling letters and digits.
//                         `memo on|off` flips Fast3D's texture-path memo (on from startup since
//                         the #111 follow-up), which isolates the per-glyph resource-name lookup.
//                         The line carries `stress=`, `glyphs=` (-1 off), `same=`, `capacity=` (how
//                         many fit in that mode), `scale=`, `pitch=`, `memo=` and `memo_repaths=`
//                         (memo hits that found their address rewritten with another path, so
//                         re-resolved). rc=1 on `error=range` (more than fit; the
//                         line says `max=`), `error=arg`, or `error=no_interpreter`. Not a CVar -
//                         nothing survives the session - and detail views are never replaced
//   probe [on|off]        THE INSTRUMENT. Drives one stepped per-tick offset into BOTH halves of
//                         the menu at once - the WHOLE scroll (parchment, rolls, hands and text)
//                         through the same Matrix_ chain the sweep uses, and a green probe string
//                         through a texture rectangle's baked y - and recolours the rolls magenta
//                         so a pixel scan cannot pick up the world. Reports `step=`/`phase=`/`dy=`,
//                         which is the 20 Hz lattice a screenshot is measured against: a drawn
//                         position that is not a whole multiple of `step` from the park position
//                         came from the renderer. It also colours the parchment frame cyan and each
//                         page's glyphs a colour of that page's own, which is what makes a smear
//                         MEASURABLE: a frame carrying two page colours is a content swap caught in
//                         the act. Since stage 5 the probe string is the menu's ONLY
//                         texture rectangle, kept as the deliberate non-interpolating reference
//                         channel. SOH_2D_DRAWING.md's "two-channel probe". Not a CVar
//   kaleido               STAGE 8, READ-ONLY. VANILLA pause's live cursor - the vanilla half of the
//                         movement differential (the scroll half is `cursor`): `state=` (6 is taking
//                         input), `page=` (kaleido's pageIndex: 0 items, 2 quest status, 3 equipment),
//                         `point=`, `x=`/`y=`, `special=none|left|right` (the page arrows, which the
//                         scroll's hands stand in for), `item=`, `slot=` and `sub=` (kaleido's
//                         unk_1E4). A node id carries the same point: `items_09` <-> page=0 point=9.
//                         Driving kaleido at all needs `agenttest kaleidoinput on`. rc=1 no_play
//   equips                STAGE 8, READ-ONLY. The save fields an equip writes, for comparing the
//                         scroll's equip with vanilla's: `buttons=` (the 8 button items, B then
//                         C-left/down/right then D-up/down/left/right), `slots=` (the 7 cButtonSlots),
//                         `equipment=` (equips.equipment), `swordless=`, `inf29=`, `sword_health=`,
//                         `bgs=`, `dpad=` (SoH's DpadEquips) and `done=` (equips the ported pages did).
//                         #123 adds a second line, `section=player`: Link in the world
//   hud                   #125, READ-ONLY. The gameplay HUD as the interface sees it, under either menu:
//                         `mode=`/`prev=`, `status=` (the nine buttonStatus), `alpha=` (thirteen
//                         alphas), `b_label=`/`b_label_shown=`, and the flying equip icon (`flight=`,
//                         `flight_ticks=`, `flight_hold=`, `flights=`, `landings=`). #130 adds, before
//                         the flight: `margin_t=` (the cosmetics' top margin) and `raised=on/total` (how
//                         many of the elements the mod raises take it), `magic=` (level, capacity,
//                         current) and `b_shift=` (the move VB_SHIFT_HUD_B_BUTTON gives B this frame,
//                         asked through the hook itself)
//   flight hold <n>|release|loop|stop|probe <on|off>
//                         #125 and #133, TEST-ONLY, and all of it exists because of the agent loop
//                         rather than the game: a flight is well under a second and a command round
//                         trip is seconds, so no run can photograph a chosen frame of one.
//                         `hold <n>` parks the icon once it has taken n ticks, so one pose can be read
//                         and screenshot, and `release` lets it go. Those enumerate the LATTICE: the
//                         poses the 20 Hz animation itself can draw, which `flight_quad=` reports
//                         exactly. `loop` replays the last flight forever and NEVER LANDS IT - no save
//                         write, no equip, no icon swap - so a burst of captures samples a live
//                         excursion; rc=1 `error=no_flight` when nothing has been started to replay.
//                         `stop` releases the hold as well, or a held flight would never reach the
//                         tick that lands it. LOOP AND HOLD COMPOSE: a hold alone already walks ONE
//                         flight tick by tick, which is all the lattice needs, and the loop is what
//                         lets that walk be repeated - or restarted - without equipping again. The
//                         loop ABANDONS a flight already in the air rather than landing it, as
//                         `menu page` does to a sweep. Neither survives the menu closing.
//                         `stop` ends the loop and lets the next tick land it, so a looped flight
//                         equips exactly once. `probe on` draws the icon as a flat MAGENTA rectangle
//                         instead, so a pixel scan recovers its corner and its side - the two channels
//                         #133's smoothness claim is about, and neither readable off a 32x32 icon with
//                         transparent edges. `hud` gains `flight_quad=` (left,top,side as floats),
//                         `flight_loop=` and `flight_probe=`. Nothing here is a CVar
//   song                  #127, READ-ONLY. The Quest Status page's song playback (`state=` kaleido's
//                         song sub-state, `point=`, `song=`, `count=`, `notes=`, `muted=`) and the
//                         ocarina's two global staves (`playback=` and `playing=`, each pos,state,
//                         button), `bgm_muted=` (the audio thread's own flag on the BGM player) and
//                         counters; the staves read true under vanilla pause as well
//   inv <kind> <a> <b>    STAGE 8, TEST-ONLY. Writes gSaveContext - a scratch save only. The sparse
//                         inventory the movement tests need: `item <slot> <ITEM_ id|255>`,
//                         `equip <bit> <0|1>`, `upgrade <UPG_ type> <value>`, `quest <bit> <0|1>`.
//                         rc=1 `error=arg` on anything out of range. Nothing refreshes the HUD
//   dump                  EVERYTHING a marker can carry: open/closed, current page, page count,
//                         which menu `primary` selects, the live freeze and HUD state, the
//                         trigger's binding count, the counters, and one line per registered page.
//                         `stick_frames=` is the stage-2 evidence - it counts frames on which the
//                         menu was OFFERED stick input while the world was frozen, which is what
//                         turns "Link did not move" from an untested negative into a challenged one.
//                         `section=sweep` carries the live animation, `section=interp` the probe's
//                         lattice plus `epoch=` - frame interpolation's camera epoch, the one
//                         number that says whether something has quietly switched interpolation off
//                         for the rest of the frame - and `pause_mode=`, the register that would
//                         have exempted it. `section=draw` is what the last drawn frame cost in
//                         glyphs, quads and Gfx words, and one `section=cursor` line per node.
//                         Stage 6 adds `section=level` (the `level` line's fields),
//                         `section=quests` (rows, visible, top, `cursor_row=`, `debug_tier=`), one
//                         `section=row` line per row THE LAST FRAME DREW - screen position, quest,
//                         status and the colour it was drawn in, which is what a screenshot of the
//                         list is asserted against - and `section=journal` (the detail view's last
//                         frame: wrapped `lines=`, `top=`, `drawn=`, `max_top=`, `width=`).
//                         Stage 7 adds `section=view` - the same last frame counting only the VIEW
//                         (the page, detail or stress body): `view=page|detail|stress|none`,
//                         `frame=`, `drawn_rows=` (distinct text lines), `drawn_glyphs=` and
//                         `drawn_words=` (Gfx words it appended) - and `section=stress`, the
//                         `stress` line's fields. Stage 8 adds `icons=` to `section=draw` and
//                         `drawn_icons=` to `section=view` (textured quads: icons, digits, outlines
//                         and Link's composite), then for each ported page one `section=port` line
//                         (`page=`, `id=`, `current=`, `scale=`, `offset=x,y`, `extent=x0,y0,x1,y1`,
//                         `slots=`, `nodes=`) and one `section=slot` line per slot (`id=` the node id,
//                         `slot=` vanilla's cursor point, `box=`, `item=` the ITEM_ token, `owned=`,
//                         `node=`, `grey=`, `cursor=` - so `item=ITEM_HOOKSHOT cursor=1` is "the cursor
//                         is on the Hookshot"), every page listed whether visible or not, and
//                         `section=link`: Link's renders/loads, `fb=`, `age=`, `load_size=`, and the
//                         segment witness - `seg4=`/`seg6=` before the last render, `seg4_during=`/
//                         `seg6_during=` what Player_DrawPause left there, `seg4_now=`/`seg6_now=`
//                         read live between frames. #124 adds `cursor_drawn=` to `section=draw`:
//                         1 when the last frame drew the cursor box, 0 through a roll or a level
//                         change (the box hides with the content). #131 adds `section=sfx`, one
//                         count per sound event the menu plays (`sfx_cursor=`, `sfx_hand=`,
//                         `sfx_roll_left=`, `sfx_roll_right=`, `sfx_open=`, `sfx_close=`) - THE ONLY
//                         CHANNEL FOR SOUND, because the harness cannot hear. A count proves the call
//                         was made, not that anything reached the speaker, so a sound change needs a
//                         listening pass too. The `sfx_` prefix is load-bearing: this line also ends
//                         with `open=` from the state suffix, and `cursor=`/`hand=` are other
//                         sections' fields, so the bare names would collide three ways
//
// Returns 0 when the operation succeeded (or for read-only subcommands), 1 otherwise - so `rc=` on
// the agent loop's cmd marker is the pass/fail bit.
int32_t RsMenuConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines);

#endif // SOH_RS_MENU_CONSOLE_H
