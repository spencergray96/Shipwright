#ifndef SOH_RS_BOAT_SAIL_DIALOG_H
#define SOH_RS_BOAT_SAIL_DIALOG_H

#include <stdint.h>
#include <string>

// The Boat Sails window's file dialog (RsBoatSailDialog.cpp): one at a time, answered on a later frame.

// Opens the dialog for the sail, starting in `startFolder`. False when one is already open.
bool RsBoatSailDialog_Open(int32_t sail, const std::string& startFolder);
// The sail an open dialog is for, or -1.
int32_t RsBoatSailDialog_Waiting(void);
// True once the dialog has closed: `*sail` is what it was for and `*path` the file picked ("" for cancelled).
bool RsBoatSailDialog_Poll(int32_t* sail, std::string* path);

#endif // SOH_RS_BOAT_SAIL_DIALOG_H
