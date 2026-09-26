// Vertical-traversal probe scene for sturdy-bassoon#134. Hand-authored, not a grid-tool export:
// the tool has no ramp or stair primitive, its only vertical unit is the 80-unit storey, and its
// exporter emits two SurfaceType rows chosen by an isWall boolean - so no ladder wall type and no
// exit index. The fixtures and the reasoning behind each are in
// docs/notes/2026-09-25-vertical-traversal-source-findings.md; the geometry is generated, not typed.
//
// Structure copied from test_level/CustomTestLevel.cpp, including the three room-behaviour fields
// (#39) and the audio reset - a hand-rolled init bypasses the scene command list, so anything not
// set here keeps whatever the previous scene left behind, and a probe that measures the wrong Link
// is worse than no probe.
#include "CustomVertProbeScene.h"
#include "global.h"
#include "z64scene.h"
#include "macros.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/OTRGlobals.h"
#include "soh/ActorDB.h"
#include <spdlog/spdlog.h>

extern "C" ActorDBEntry* ActorDB_Retrieve(const int id);

extern "C" {
    extern CollisionHeader vert_probe_scene_collisionHeader;
    extern PolygonType0    vert_probe_room_0_shapeHeader;

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

static EnvLightSettings sVertProbeLightSettings[4] = {
    // Dawn
    {{ 70, 45, 57 }, { 73, -73, 73 }, { 180, 154, 138 }, { -73, 73, -73 }, { 20, 20, 60 },
     { 140, 120, 100 }, (s16)(993 | (1 << 10)), 12800 },
    // Day
    {{ 105, 90, 90 }, { 73, -73, 73 }, { 255, 255, 240 }, { -73, 73, -73 }, { 50, 50, 90 },
     { 100, 100, 120 }, (s16)(996 | (1 << 10)), 12800 },
    // Dusk
    {{ 120, 90, 0 }, { 73, -73, 73 }, { 250, 135, 50 }, { -73, 73, -73 }, { 30, 30, 60 },
     { 120, 70, 50 }, (s16)(995 | (1 << 10)), 12800 },
    // Night
    {{ 40, 70, 100 }, { 73, -73, 73 }, { 20, 20, 35 }, { -73, 73, -73 }, { 50, 50, 100 },
     { 0, 0, 30 }, (s16)(992 | (1 << 10)), 12800 },
};

static EntranceEntry sVertProbeEntrances[] = {
    { 0, 0 },
};

// Fixture D's landing carries exitIndex 1, which is setupExitList[0]. Without a list the exit read
// at z_player.c:5135 would dereference NULL; sending it back to test_level makes the warp obvious
// in `agenttest state` (scene changes to 0x6E).
static s16 sVertProbeExitList[] = {
    ENTR_TEST_LEVEL_0,
};

// South-west of fixture A, facing +Z up the lane of steps.
static ActorEntry sVertProbePlayerSpawn = {
    ACTOR_PLAYER, { 0, 0, -2600 }, { 0, 0, 0 }, 0x0D00
};

static RomFile sVertProbeRoomList[] = {
    { (uintptr_t)&vert_probe_room_0_shapeHeader,
      (uintptr_t)&vert_probe_room_0_shapeHeader + 256,
      nullptr },
};

extern "C" int CustomVertProbeScene_IsCustomScene(s32 sceneId) {
    return sceneId == SCENE_VERT_PROBE;
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

    BgCheck_Allocate(&play->colCtx, play, &vert_probe_scene_collisionHeader);

    play->numRooms = 1;
    play->roomList = sVertProbeRoomList;

    play->setupEntranceList = sVertProbeEntrances;
    play->setupExitList     = sVertProbeExitList;
    play->linkActorEntry    = &sVertProbePlayerSpawn;
    play->linkAgeOnLoad     = gSaveContext.linkAge;

    s16 linkObjectId = gLinkObjectIds[gSaveContext.linkAge];
    ActorDB_Retrieve(play->linkActorEntry->id)->objectId = linkObjectId;
    Object_Spawn(&play->objectCtx, linkObjectId);

    play->objectCtx.subKeepIndex = Object_Spawn(&play->objectCtx, OBJECT_GAMEPLAY_FIELD_KEEP);

    play->skyboxId = SKYBOX_NORMAL_SKY;
    play->envCtx.unk_17 = play->envCtx.unk_18 = 0;
    play->envCtx.indoors         = 0;
    play->envCtx.skyboxDisabled  = 0;
    play->envCtx.sunMoonDisabled = 0;
    play->sequenceCtx.seqId            = NA_BGM_KAKARIKO_KID;
    play->sequenceCtx.natureAmbienceId = 0xFF;

    Audio_QueueSeqCmd(0xF0000000);

    play->envCtx.numLightSettings  = 4;
    play->envCtx.lightSettingsList = sVertProbeLightSettings;

    Play_InitEnvironment(play, play->skyboxId);
    GameInteractor_ExecuteAfterSceneCommands(play->sceneNum);
}

extern "C" void CustomVertProbeScene_InitRoom(PlayState* play, RoomContext* roomCtx) {
    roomCtx->curRoom.echo       = 0;
    roomCtx->curRoom.meshHeader = (MeshHeader*)&vert_probe_room_0_shapeHeader;

    // Issue #39: without these the room inherits the previous scene's behaviour, and a stale
    // ROOM_BEHAVIOR_TYPE1_2 caps run speed - which would quietly change every measurement here.
    roomCtx->curRoom.behaviorType1 = ROOM_BEHAVIOR_TYPE1_0;
    roomCtx->curRoom.behaviorType2 = ROOM_BEHAVIOR_TYPE2_0;
    roomCtx->curRoom.lensMode      = LENS_MODE_HIDE_ACTORS;

    // No actors: every fixture is collision.
    play->numSetupActors = 0;
    play->setupActorList = nullptr;

    Player_SetBootData(play, GET_PLAYER(play));
    Actor_SpawnTransitionActors(play, &play->actorCtx);
    GameInteractor_ExecuteAfterSceneCommands(play->sceneNum);
}

extern "C" int CustomVertProbeScene_TrySpawn(PlayState* play, s32 sceneId, s32 spawn) {
    if (sceneId != SCENE_VERT_PROBE) {
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

    SPDLOG_INFO("CustomVertProbeScene: spawned scene {} spawn {}", sceneId, spawn);
    return 1;
}
