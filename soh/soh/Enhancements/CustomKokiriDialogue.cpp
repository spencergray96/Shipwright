/**
 * CustomKokiriDialogue.cpp
 * 
 * Test modification: Replaces a Kokiri child's dialogue with custom text.
 * This hooks text ID 0x1004 which is used by a Kokiri in Kokiri Forest.
 * 
 * Author: Spencer
 * Created: 2026-02-06
 */

#include <soh/OTRGlobals.h>
#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include <variables.h>
}

// Text ID 0x1004 is used by ENKO_TYPE_CHILD_0 in Kokiri Forest
#define TEXT_KOKIRI_CHILD_0 0x1004

void BuildCustomKokiriMessage(uint16_t* textId, bool* loadFromMessageTable) {
    CustomMessage msg = CustomMessage(
        "AHHHH SPENCER"
    );
    msg.AutoFormat();
    msg.LoadIntoFont();
    *loadFromMessageTable = false;
}

void CustomKokiriDialogue_Register() {
    // Always enabled (condition = 1)
    COND_ID_HOOK(OnOpenText, TEXT_KOKIRI_CHILD_0, 1, BuildCustomKokiriMessage);
}

// Auto-register this enhancement on startup
static RegisterShipInitFunc initFunc(CustomKokiriDialogue_Register);
