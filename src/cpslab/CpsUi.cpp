/* CPS Lab: UI glue (toolbar / command handlers). Moved out of SumatraPDF.cpp etc.
   Function bodies are unchanged. */

#include "utils/BaseUtil.h"
#include "utils/WinUtil.h"

#include "wingui/UIModels.h"

#include "Settings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "EngineAll.h"
#include "GlobalPrefs.h"
#include "ChmModel.h"
#include "DisplayModel.h"
#include "ProgressUpdateUI.h"
#include "TextSelection.h"
#include "TextSearch.h"
#include "SumatraPDF.h"
#include "MainWindow.h"
#include "WindowTab.h"
#include "Commands.h"
#include "cpslab/CpsLabAnnot.h"
#include "cpslab/CpsUi.h"

namespace cpslab {

void OnSelectCommand(MainWindow* win, WindowTab* tab, int cmdId) {
    const char* keyword = nullptr;
    switch (cmdId) {
        case CmdSelectPins:
            keyword = "Pin";
            break;
        case CmdSelectCells:
            keyword = "Cell";
            break;
        case CmdSelectNets:
            keyword = "Net";
            break;
        default:
            return;
    }
    if (tab != nullptr) {
        auto iResult = SendMessage(win->hwndToolbar, TB_GETSTATE, cmdId, 0);
        if (iResult & TBSTATE_CHECKED) {
            tab->markers->setSelection(keyword);
        } else {
            tab->markers->unsetSelection(keyword);
        }
    }
}

} // namespace cpslab
