// The system's file dialog for the Boat Sails window's "Choose image..." (sturdy-bassoon#287), on its own so
// portable-file-dialogs' <windows.h> stays out of the window's ImGui code. pfd runs the dialog on a thread of its own,
// so the game keeps drawing while it is open; the window polls for the answer each frame.

#include "RsBoatSailDialog.h"

#include <memory>

#include "soh/Extractor/portable-file-dialogs.h"

namespace {

std::unique_ptr<pfd::open_file> sDialog;
int32_t sSail = -1;

} // namespace

bool RsBoatSailDialog_Open(int32_t sail, const std::string& startFolder) {
    if (sDialog != nullptr) {
        return false;
    }
    sSail = sail;
    sDialog = std::make_unique<pfd::open_file>("Choose a picture for the sail", startFolder,
                                               std::vector<std::string>{ "Images", "*.png *.jpg *.jpeg *.bmp *.tga",
                                                                         "All files", "*" });
    return true;
}

int32_t RsBoatSailDialog_Waiting(void) {
    return sDialog != nullptr ? sSail : -1;
}

bool RsBoatSailDialog_Poll(int32_t* sail, std::string* path) {
    if (sDialog == nullptr || !sDialog->ready(0)) {
        return false;
    }
    const std::vector<std::string> picked = sDialog->result();
    sDialog.reset();
    *sail = sSail;
    *path = picked.empty() ? std::string() : picked[0];
    sSail = -1;
    return true;
}
