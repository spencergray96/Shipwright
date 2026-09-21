#include "CustomRsAreaP3Scene.h"
#include "global.h"
#include "z64scene.h"
#include "macros.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/OTRGlobals.h"
#include "soh/ActorDB.h"
#include <spdlog/spdlog.h>
#include "../GridToolSceneData.h"

// The A side of sturdy-bassoon#79: region 12850 (Lumbridge) exported from the OSRS cache, cropped
// to the same ground the hand-built grid-tool scene `lumbridge_castle` (0x7C) covers, and converted
// to flat vertex colour. Conversion log: oot-project/blender/rs-export/area/P3A_LOG.md.
//
// Hand-written on purpose: no grid-tool project owns this scene and no re-export touches it. The
// geometry and collision C beside this file are the Fast64 export post-processed by
// docs/test-runs/2026-09-18-rs-export-p3-ab/integrate_area.py; this file replaces the SceneCmd
// arrays that export also emitted, the same way every other compiled-in custom scene here does.

extern "C" ActorDBEntry* ActorDB_Retrieve(const int id);

extern "C" {
    extern CollisionHeader rs_area_p3_scene_collisionHeader;
    extern RoomShapeNormal rs_area_p3_room_0_shapeHeader;

    extern s16   gLinkObjectIds[];
    s32  Object_Spawn(ObjectContext* objectCtx, s16 objectId);
    void BgCheck_Allocate(CollisionContext* colCtx, PlayState* play, CollisionHeader* colHeader);
    void Play_InitEnvironment(PlayState* play, s16 skyboxId);
    u32  func_80096FE8(PlayState* play, RoomContext* roomCtx);
    void func_80096FD4(PlayState* play, Room* room);
    void Player_SetBootData(PlayState* play, Player* player);
    void Actor_SpawnTransitionActors(PlayState* play, ActorContext* actorCtx);
    void Object_InitBank(PlayState* play, ObjectContext* objectCtx);
    void LightContext_Init(PlayState* play, LightContext* lightCtx);
    void TransitionActor_InitContext(GameState* state, TransitionActorContext* transiActorCtx);
}

// Row for row the grid-tool template's table - deliberately, so the A/B differs in geometry and
// not in sky - EXCEPT the day row's fog near, which is 993 here against the template's 996. That
// is P3a's one deliberate presentation choice: fog starts nearer, for the N64 haze. `agenttest fog
// 996 12800` puts it back at B's value without a rebuild, so haze and geometry can be told apart;
// the perf marker's `fog=` field says which band a reading was taken under.
//
// A's materials have G_LIGHTING off, so ambient and diffuse do nothing here and only the fog and
// the skybox respond to time of day. Compare at `agenttest time day` only.
static EnvLightSettings sRsAreaP3LightSettings[4] = {
    {{ 70, 45, 57 }, { 73, -73, 73 }, { 180, 154, 138 }, { -73, 73, -73 }, { 20, 20, 60 },
     { 140, 120, 100 }, (s16)(993 | (1 << 10)), 12800 },
    {{ 105, 90, 90 }, { 73, -73, 73 }, { 255, 255, 240 }, { -73, 73, -73 }, { 50, 50, 90 },
     { 100, 100, 120 }, (s16)(993 | (1 << 10)), 12800 },
    {{ 120, 90, 0 }, { 73, -73, 73 }, { 250, 135, 50 }, { -73, 73, -73 }, { 30, 30, 60 },
     { 120, 70, 50 }, (s16)(995 | (1 << 10)), 12800 },
    {{ 40, 70, 100 }, { 73, -73, 73 }, { 20, 20, 35 }, { -73, 73, -73 }, { 50, 50, 100 },
     { 0, 0, 30 }, (s16)(992 | (1 << 10)), 12800 },
};

static EntranceEntry sRsAreaP3Entrances[] = {
    { 0, 0 },
};

// P3a's spawn: OoT (-140, 9, 160) is RS tile (24.5, 19.5), the west half of the castle courtyard.
// params as the grid-tool template writes them: bits 8-11 PLAYER_START_MODE_IDLE, low byte 0xFF.
static ActorEntry sRsAreaP3PlayerSpawn = {
    ACTOR_PLAYER, { -140, 9, 160 }, { 0, 0, 0 }, 0xDFF
};

static RomFile sRsAreaP3RoomList[] = {
    { (uintptr_t)&rs_area_p3_room_0_shapeHeader,
      (uintptr_t)&rs_area_p3_room_0_shapeHeader + 256,
      nullptr },
};

extern "C" int CustomRsAreaP3Scene_IsCustomScene(s32 sceneId) {
    return sceneId == SCENE_RS_AREA_P3;
}

static void InitScene(PlayState* play, s32 spawn) {
    play->curSpawn          = spawn;
    play->linkActorEntry    = nullptr;
    play->unk_11DFC         = nullptr;
    play->setupEntranceList = nullptr;
    play->setupExitList     = nullptr;
    play->cUpElfMsgs        = nullptr;
    play->setupPathList     = nullptr;
    play->numSetupActors    = 0;

    Object_InitBank(play, &play->objectCtx);
    LightContext_Init(play, &play->lightCtx);
    TransitionActor_InitContext(&play->state, &play->transiActorCtx);
    func_80096FD4(play, &play->roomCtx.curRoom);
    YREG(15) = 0;
    gSaveContext.worldMapArea = 0;

    BgCheck_Allocate(&play->colCtx, play, &rs_area_p3_scene_collisionHeader);

    play->numRooms = 1;
    play->roomList = sRsAreaP3RoomList;

    play->setupEntranceList = sRsAreaP3Entrances;
    play->linkActorEntry    = &sRsAreaP3PlayerSpawn;
    play->linkAgeOnLoad     = gSaveContext.linkAge;

    s16 linkObjectId = gLinkObjectIds[gSaveContext.linkAge];
    ActorDB_Retrieve(play->linkActorEntry->id)->objectId = linkObjectId;
    Object_Spawn(&play->objectCtx, linkObjectId);

    play->objectCtx.subKeepIndex = Object_Spawn(&play->objectCtx, OBJECT_GAMEPLAY_FIELD_KEEP);

    play->skyboxId = SKYBOX_NORMAL_SKY;
    // What SCENE_CMD_SKYBOX_SETTINGS and SCENE_CMD_SKYBOX_DISABLES set in a normal scene: a clear
    // sky, outdoors, sky and sun drawn. PlayState is re-allocated for each scene without being
    // cleared, so skipping them left the previous scene's values in place - Hyrule Field's storm,
    // Link's house's indoor lighting (sturdy-bassoon#122).
    play->envCtx.unk_17 = play->envCtx.unk_18 = 0;
    play->envCtx.indoors         = 0;
    play->envCtx.skyboxDisabled  = 0;
    play->envCtx.sunMoonDisabled = 0;
    // The same track lumbridge_castle uses, so the pair differs in geometry only.
    play->sequenceCtx.seqId            = NA_BGM_KAKARIKO_KID;
    play->sequenceCtx.natureAmbienceId = 0xFF;

    // See the note in CustomLumbridgeCastleScene.cpp: this hand-rolled init bypasses
    // SCENE_CMD_SOUND_SETTINGS, and without this reset every one-shot SFX is silently dropped.
    Audio_QueueSeqCmd(0xF0000000);

    play->envCtx.numLightSettings  = 4;
    play->envCtx.lightSettingsList = sRsAreaP3LightSettings;

    Play_InitEnvironment(play, play->skyboxId);
    GameInteractor_ExecuteAfterSceneCommands(play->sceneNum);
}

extern "C" void CustomRsAreaP3Scene_InitRoom(PlayState* play, RoomContext* roomCtx) {
    roomCtx->curRoom.echo       = 0;
    roomCtx->curRoom.meshHeader = (MeshHeader*)&rs_area_p3_room_0_shapeHeader;

    // Issue #39: nothing on the load path clears these, so without them the room inherits the
    // previous scene's behaviour (a stale TYPE1_2 caps run speed and kills the roll).
    roomCtx->curRoom.behaviorType1 = ROOM_BEHAVIOR_TYPE1_0;
    roomCtx->curRoom.behaviorType2 = ROOM_BEHAVIOR_TYPE2_0;
    roomCtx->curRoom.lensMode      = LENS_MODE_HIDE_ACTORS;

    play->numSetupActors = 0;
    play->setupActorList = nullptr;

    Player_SetBootData(play, GET_PLAYER(play));
    Actor_SpawnTransitionActors(play, &play->actorCtx);
    GameInteractor_ExecuteAfterSceneCommands(play->sceneNum);
}

extern "C" int CustomRsAreaP3Scene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn) {
    if (sceneId != SCENE_RS_AREA_P3) {
        return 0;
    }

    SceneTableEntry* scene = &gSceneTable[sceneId];
    scene->unk_13      = 0;
    play->loadedScene  = scene;
    play->sceneNum     = sceneId;
    play->sceneConfig  = scene->config;
    play->sceneSegment = nullptr;

    InitScene(play, spawn);
    func_80096FE8(play, &play->roomCtx);
    GameInteractor_ExecuteOnSceneInit(play->sceneNum);

    SPDLOG_INFO("CustomRsAreaP3Scene: spawned scene {} spawn {}", sceneId, spawn);
    return 1;
}
