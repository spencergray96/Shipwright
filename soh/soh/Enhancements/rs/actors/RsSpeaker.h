#ifndef SOH_RS_SPEAKER_H
#define SOH_RS_SPEAKER_H

#include <stddef.h>
#include <stdint.h>
#include "soh/cvar_prefixes.h"

// A SPEAKER PER SCREEN (sturdy-bassoon#281): who has the floor in a conversation with several
// characters. The idea is Majora's Mask's Actor_ChangeFocus, rebuilt rather than ported.
//
// The conversation's OWNER is the RsNpc Link talked to, and it stays the owner throughout: its rule
// table, its tree, its gifts, its way out. A screen or a reply may name another character as its
// speaker (RsDialogueRule.speaker, RsDialogueOption.replySpeaker), and while that box is open the
// owner lends the floor: Player's talk actor and focus actor become the speaker, so the talk camera
// swings to it and Link turns to face it. Nothing else about the conversation changes hands - every
// box is still the owner's text id, which is why choice memory and a scroll's re-layout never notice.
//
// Who has the floor is decided per box the OWNER opens: the entry box, a node, a reply. A journal
// notice and the close leave it where it is - swinging back to the owner as the box closes would be a
// wasted swing, and Player drops its talk actor when the talk ends anyway. A gift is still the owner's
// to hand over; its re-open lands on the next node's speaker.
//
// NPC conversations only. An interaction's text hook finds whose line to render through Player's talk
// actor (RsActors.cpp), so lending a prop's floor would make its next box render somebody else's
// params as an interaction id; registration refuses a speaker on an interaction for that reason.
//
// The state here is the one conversation in progress - there is at most one, since Player talks to one
// actor - and none of it outlives that conversation or touches the save.

struct Actor;
struct PlayState;

#ifdef __cplusplus
extern "C" {
#endif

// A conversation starts, or re-opens after a gift: `owner` is the RsNpc whose conversation it is.
void RsSpeaker_Begin(struct Actor* owner, int32_t ownerNpcId);

// The owner has just opened a box spoken by `speaker` (an RS_DLG_SPEAKER value; 0 is the owner). Finds
// the live placement - the owner itself for its own id, otherwise the nearest RsNpc of that NpcId -
// and gives it Player's talk and focus actors. A speaker with no placement in the scene leaves the
// floor with the owner. Writes `rs_dialogue npc=<owner> event=speaker rule=<n> on=<on> declared=<id|-1>
// speaker=<id> result=moved|kept|absent at=<x>,<z>`; `on` names the box (entry, node, reply).
void RsSpeaker_Hand(struct PlayState* play, int32_t ruleIndex, int32_t speaker, const char* on);

// The conversation is over, however it ended. Safe to call twice.
void RsSpeaker_End(void);

// An RsNpc is being destroyed. If it holds the borrowed floor, the floor goes back to the owner before
// Player or the camera can read a freed actor; if it is the owner, the conversation is forgotten.
void RsSpeaker_ActorGone(struct PlayState* play, struct Actor* actor);

// --- the talk camera's re-swing (z_camera.c, Camera_KeepOn3, SOH [sturdy-bassoon#281]) -----------
//
// OoT's talk camera sets itself up once, on the target it starts with, and never looks again; Majora's
// Mask's compares its target every frame and re-runs the set-up when it changes. Ours does that only
// for a change this module made, so every vanilla conversation keeps vanilla's camera.

// Asked every frame of Camera_KeepOn3 with the camera's current target and the one its set-up framed.
// 1 when a handoff has moved the target since: the camera re-runs its set-up (animState 0xA). Not
// while a set-up is already pending (`inSetup`), which frames the new target by itself - that is how
// a conversation whose first line is someone else's opens straight on them.
int32_t RsSpeaker_CameraReswing(struct Actor* cameraTarget, struct Actor* framedTarget, int32_t inSetup);

// The set-up's swing time in ticks: the developer setting below for a re-swing this module asked for,
// `settingTicks` (the camera setting's own) for every other set-up.
int32_t RsSpeaker_CameraSwingTicks(int32_t settingTicks);

// How long the camera takes to swing from one speaker to the next, in 20 Hz ticks: `npc swing <ticks>`
// sets it, kept in the config like the other developer settings. 10 is the normal talk camera's own
// opening swing, half a second. One value for every handoff; a per-line override is not built (the
// ADR records how it would be).
#define CVAR_RS_SPEAKER_SWING CVAR_DEVELOPER_TOOLS("RsSpeakerSwing")
#define RS_SPEAKER_SWING_DEFAULT 10
#define RS_SPEAKER_SWING_MIN 1
#define RS_SPEAKER_SWING_MAX 60

// The swing time `npc swing` set, clamped to the range: the one reading every surface prints.
int32_t RsSpeaker_SwingTicks(void);

// One line for `npc speaker`: `owner=<npc|-1> talk=<who> focus=<who> pending=<0|1> armed=<0|1>
// swing=<ticks>`, where <who> is an NpcId, -1 for nobody, or `actor:0x<id>` for anything else.
void RsSpeaker_Describe(struct PlayState* play, char* buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif // SOH_RS_SPEAKER_H
