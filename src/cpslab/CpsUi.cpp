/* CPS Lab: UI glue (toolbar / command handlers). Moved out of SumatraPDF.cpp etc.
   Function bodies are unchanged. */

#include "utils/BaseUtil.h"
#include "utils/WinUtil.h"

#include "wingui/UIModels.h"

#include "Settings.h"
#include "Flags.h"
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
#include "Selection.h"
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

// restores the checked state of the Nets / Parts / Pins buttons from the tab's markers
void UpdateToolbarButtons(MainWindow* win) {
    HWND hwnd = win->hwndToolbar;
    auto s = SendMessage(hwnd, TB_GETSTATE, CmdSelectNets, 0);
    if (win->CurrentTab()->markers->isSelection("Net")) {
        s |= TBSTATE_CHECKED;
    } else {
        s &= ~TBSTATE_CHECKED;
    }
    SendMessage(hwnd, TB_SETSTATE, CmdSelectNets, s);

    s = SendMessage(hwnd, TB_GETSTATE, CmdSelectCells, 0);
    if (win->CurrentTab()->markers->isSelection("Cell")) {
        s |= TBSTATE_CHECKED;
    } else {
        s &= ~TBSTATE_CHECKED;
    }
    SendMessage(hwnd, TB_SETSTATE, CmdSelectCells, s);

    s = SendMessage(hwnd, TB_GETSTATE, CmdSelectPins, 0);
    if (win->CurrentTab()->markers->isSelection("Pin")) {
        s |= TBSTATE_CHECKED;
    } else {
        s &= ~TBSTATE_CHECKED;
    }
    SendMessage(hwnd, TB_SETSTATE, CmdSelectPins, s);
}

// applies CPS Lab command-line flags (DDE service/topic names, document mode, text export)
void InitFromFlags(const Flags& flags) {
    // DDE Server/Topic name for PdfSync
    if (flags.pdfsync_dde_service != nullptr) {
        cpslab::PDFSYNC_DDE_SERVICE = strconv::Utf8ToWStr(flags.pdfsync_dde_service);
    }
    // logf("PDFSYNC_DDE_SERVICE : '%ls'\n", PDFSYNC_DDE_SERVICE);
    if (flags.pdfsync_dde_topic != nullptr) {
        cpslab::PDFSYNC_DDE_TOPIC = strconv::Utf8ToWStr(flags.pdfsync_dde_topic);
    }
    // logf("PDFSYNC_DDE_TOPIC : '%ls'\n", PDFSYNC_DDE_TOPIC);

    // DDE Server/Topic name for UserApp
    if (flags.userapp_dde_service != nullptr) {
        cpslab::USERAPP_DDE_SERVICE = strconv::Utf8ToWStr(flags.userapp_dde_service);
    }
    if (flags.userapp_dde_topic != nullptr) {
        cpslab::USERAPP_DDE_TOPIC = strconv::Utf8ToWStr(flags.userapp_dde_topic);
    }
    if (flags.userapp_dde_debug_topic != nullptr) {
        cpslab::USERAPP_DDE_DEBUG_TOPIC = strconv::Utf8ToWStr(flags.userapp_dde_debug_topic);
    }
    if (flags.document_mode) {
        cpslab::MODE = cpslab::CpsMode::Document;
    }
    if (flags.export_text_blocks) {
        cpslab::EXPORT_TEXT_BLOCKS = flags.export_text_blocks;
    }
}

void OnDragStopped(MainWindow* win, int x, int y, bool didDragMouse) {
    if (cpslab::MODE == cpslab::CpsMode::Document) {
        if (!didDragMouse) {
            cpslab::sendClickPoint(win, x, y);
        }
    }
}

// callback to user application via DDE
void OnSelectionStopped(MainWindow* win, int x, int y) {
    WindowTab* tab = win->CurrentTab();
    if (cpslab::MODE == cpslab::CpsMode::Document) {
        if (tab->selectionOnPage && 0 < tab->selectionOnPage->size()) {
            cpslab::sendSelectText(win);
        } else {
            cpslab::sendClickPoint(win, x, y);
        }
    } else {
        tab->markers->sendSelectMessage(win);
    }
}

// callback to user application via DDE
void OnWordSelected(MainWindow* win) {
    WindowTab* tab = win->CurrentTab();
    tab->markers->sendSelectMessage(win, IsCtrlPressed());
}

void OnImageSelected(MainWindow* win, int x, int y) {
    if (cpslab::MODE == cpslab::CpsMode::Document) {
        cpslab::sendSelectImage(win, x, y);
    }
}

} // namespace cpslab
