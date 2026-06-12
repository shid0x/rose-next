#include "stdafx.h"

#include "interface/dlgs/console_dialog.h"

#include "tgamectrl/tcontrolmgr.h"
#include "tgamectrl/timage.h"
#include "tgamectrl/tpane.h"
#include "tgamectrl/resourcemgr.h"

const int BACKGROUND_ID = 0;

bool
ConsoleDialog::Create(int x, int y, int width, int height) {
    if (!CTDialog::Create(x, y, width, height)) {
        return false;
    }

    // Use the background from the chat window
    CResourceMgr* res_manager = CResourceMgr::GetInstance();
    int graphic_id = res_manager->GetImageNID(0, "CHAT_LIST_BG");

    // Create and add a background to the dialog
    CTImage* background =
        CTControlMgr::GetInstance()->MakeImage(BACKGROUND_ID, x, y, width, height, graphic_id, 0);
    background->SetAlphaValue(128);

    Add(background);

    return true;
}
