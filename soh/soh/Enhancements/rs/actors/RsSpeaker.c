/*
 * RsSpeaker.c - a speaker per screen (sturdy-bassoon#281). See RsSpeaker.h.
 *
 * Majora's Mask's Actor_ChangeFocus is the whole primitive: while Player is talking, set its talk actor
 * and focus actor to someone else. Player re-points the talk camera at its focus actor every frame
 * (Player_UpdateCamAndSeqModes) and turns Link toward it (Player_Action_Talk), so those two writes are
 * the handoff. What OoT lacks is a talk camera that notices: Camera_KeepOn3 frames its target once.
 * The re-swing below is MM's three lines, gated to a change this file made.
 */

#include <stdio.h> // snprintf, for the agent-loop markers

#include "RsSpeaker.h"
#include "RsActorParams.h"
#include "RsActors.h"
#include "global.h"
#include "soh/Enhancements/rs/dialogue/NpcDialogueDef.h"

static Actor* sOwner = NULL;      // the RsNpc whose conversation this is or last was, until it is destroyed
static int32_t sOwnerNpcId = -1;
static int32_t sOpen = 0;         // the conversation is in progress: the floor may be lent
static Actor* sPending = NULL;    // a handoff the talk camera has not seen yet
static int32_t sArmed = 0;        // the camera is re-running its set-up for a handoff: its next set-up
                                  // takes the swing setting's time rather than the camera setting's

/* The NpcId an actor speaks as, -1 for none, for the markers and `npc speaker`. */
static int32_t RsSpeaker_NpcOf(Actor* actor) {
    return (actor != NULL && actor->id == ACTOR_RS_NPC) ? RS_NPC_PARAMS_GET_ID(actor->params) : -1;
}

/* The placement that speaks as `npcId`: the owner for its own id - another placement of the same
 * character is not who Link is talking to - otherwise the nearest live RsNpc of that id to the owner,
 * because a cast stands together. NULL when none is placed in this scene. */
static Actor* RsSpeaker_Find(PlayState* play, int32_t npcId) {
    Actor* best = NULL;
    f32 bestDist = 0.0f;
    Actor* actor;

    if (npcId == sOwnerNpcId) {
        return sOwner;
    }
    for (actor = play->actorCtx.actorLists[ACTORCAT_NPC].head; actor != NULL; actor = actor->next) {
        f32 dist;

        if (actor->id != ACTOR_RS_NPC || actor->update == NULL || RS_NPC_PARAMS_GET_ID(actor->params) != npcId) {
            continue;
        }
        dist = Actor_WorldDistXZToActor(sOwner, actor);
        if (best == NULL || dist < bestDist) {
            best = actor;
            bestDist = dist;
        }
    }
    return best;
}

/* Gives Player's talk and focus actors to `target`, and tells the camera now rather than on Player's
 * next update, so a set-up that runs this frame already frames it. */
static void RsSpeaker_Give(PlayState* play, Player* player, Actor* target) {
    player->talkActor = target;
    player->focusActor = target;
    Camera_SetParam(Play_GetCamera(play, CAM_ID_MAIN), 8, target);
    sPending = target;
}

static void RsSpeaker_Mark(const char* on, int32_t declared, Actor* target, const char* result, int32_t ruleIndex) {
    char line[160];

    snprintf(line, sizeof(line),
             "rs_dialogue npc=%d event=speaker rule=%d on=%s declared=%d speaker=%d result=%s at=%d,%d", sOwnerNpcId,
             ruleIndex, on, declared, RsSpeaker_NpcOf(target), result, (int)target->world.pos.x,
             (int)target->world.pos.z);
    RsAgent_Marker(line);
}

void RsSpeaker_Begin(Actor* owner, int32_t ownerNpcId) {
    sOwner = owner;
    sOwnerNpcId = ownerNpcId;
    sOpen = 1;
    sPending = NULL;
    sArmed = 0;
}

void RsSpeaker_Hand(PlayState* play, int32_t ruleIndex, int32_t speaker, const char* on) {
    Player* player = GET_PLAYER(play);
    const int32_t declared = RS_DLG_SPEAKER_GET_ID(speaker);
    Actor* target;
    const char* result;

    /* Only inside a conversation Player is still in: the floor is Player's talk actor, which Player
     * itself clears the moment its talk flag drops. */
    if (!sOpen || player == NULL || !CHECK_FLAG_ALL(player->actor.flags, ACTOR_FLAG_TALK)) {
        return;
    }
    target = RsSpeaker_Find(play, speaker == RS_DLG_SPEAKER_DEFAULT ? sOwnerNpcId : declared);
    if (target == NULL) {
        /* Registered, but not placed here: a runtime fact registration cannot see. The owner speaks. */
        target = sOwner;
        result = "absent";
    } else {
        result = player->talkActor == target ? "kept" : "moved";
    }
    if (player->talkActor != target || player->focusActor != target) {
        RsSpeaker_Give(play, player, target);
    }
    RsSpeaker_Mark(on, declared, target, result, ruleIndex);
}

/* The owner is KEPT: the conversation ends before its box has finished closing, and Player holds the
 * lent floor until then, so a speaker destroyed in between still has someone to hand it back to. Only
 * the owner's own destruction forgets it. */
void RsSpeaker_End(void) {
    sOpen = 0;
    sPending = NULL;
    sArmed = 0;
}

void RsSpeaker_ActorGone(PlayState* play, Actor* actor) {
    Player* player;

    if (sOwner == NULL) {
        return;
    }
    if (actor == sOwner) {
        RsSpeaker_End();
        sOwner = NULL;
        sOwnerNpcId = -1;
        return;
    }
    if (sPending == actor) {
        sPending = NULL;
    }
    /* Player's own list empties first on a scene change, so this is NULL then and there is nothing to
     * hand back. Otherwise a speaker killed mid-conversation (a console clear) would leave Player and
     * the camera pointing at freed memory; vanilla's own cleanup in Actor_Delete only covers the focus
     * actor, and only by dropping the talk camera. Outside a talk - the owner handing over a gift - the
     * floor is not lent, and vanilla's cleanup is the right one. */
    player = GET_PLAYER(play);
    if (player != NULL && CHECK_FLAG_ALL(player->actor.flags, ACTOR_FLAG_TALK) &&
        (player->talkActor == actor || player->focusActor == actor)) {
        RsSpeaker_Give(play, player, sOwner);
        RsSpeaker_Mark("gone", RsSpeaker_NpcOf(actor), sOwner, "moved", -1);
    }
}

int32_t RsSpeaker_CameraReswing(Actor* cameraTarget, Actor* framedTarget, int32_t inSetup) {
    /* Until Player points the camera at the handoff's actor, it is not this camera's change to react
     * to - and a target change nobody here asked for is vanilla's to ignore. */
    if (sPending == NULL || cameraTarget != sPending) {
        return 0;
    }
    sPending = NULL;
    if (inSetup || framedTarget == cameraTarget) {
        return 0;
    }
    sArmed = 1;
    return 1;
}

int32_t RsSpeaker_SwingTicks(void) {
    return CLAMP(CVarGetInteger(CVAR_RS_SPEAKER_SWING, RS_SPEAKER_SWING_DEFAULT), RS_SPEAKER_SWING_MIN,
                 RS_SPEAKER_SWING_MAX);
}

int32_t RsSpeaker_CameraSwingTicks(int32_t settingTicks) {
    if (!sArmed) {
        return settingTicks;
    }
    sArmed = 0;
    return RsSpeaker_SwingTicks();
}

static void RsSpeaker_Who(Actor* actor, char* buf, size_t len) {
    if (actor == NULL) {
        snprintf(buf, len, "-1");
    } else if (actor->id == ACTOR_RS_NPC) {
        snprintf(buf, len, "%d", (int)RS_NPC_PARAMS_GET_ID(actor->params));
    } else {
        snprintf(buf, len, "actor:0x%04X", (unsigned)actor->id);
    }
}

void RsSpeaker_Describe(PlayState* play, char* buf, size_t len) {
    Player* player = play != NULL ? GET_PLAYER(play) : NULL;
    char talk[24];
    char focus[24];

    RsSpeaker_Who(player != NULL ? player->talkActor : NULL, talk, sizeof(talk));
    RsSpeaker_Who(player != NULL ? player->focusActor : NULL, focus, sizeof(focus));
    snprintf(buf, len, "owner=%d talk=%s focus=%s pending=%d armed=%d swing=%d", sOpen ? sOwnerNpcId : -1, talk, focus,
             sPending != NULL, sArmed, RsSpeaker_SwingTicks());
}
