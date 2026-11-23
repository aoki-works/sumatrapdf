/* CPS Lab: DDE command handlers (GetText/GetWord/GetBlock, MarkWord, Select*, Quit).
   Moved out of SearchAndDDE.cpp. Function bodies are unchanged. */

#include "utils/BaseUtil.h"
#include "utils/ScopedWin.h"
#include "utils/FileUtil.h"
#include "utils/WinUtil.h"

#include "wingui/UIModels.h"

#include "Settings.h"
#include "DisplayMode.h"
#include "DocController.h"
#include "EngineBase.h"
#include "EngineAll.h"
#include "Annotation.h"
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
#include "AppSettings.h"
#include "cpslab/CpsLabAnnot.h"
#include "cpslab/CpsDde.h"

#include "utils/Log.h"

namespace cpslab {

/*
 CPS Lab.
 (1) [GetText("<pdffilepath>", "<textFileName>)]
     [GeWord("<pdffilepath>", "<textFileName>)]
     [GetBlock("<pdffilepath>", "<textFileName>)]
*/
static const char* HandleGetTextCmd(const char* cmd, bool* ack) {
    AutoFreeStr pdfFile;
    AutoFreeStr txtFile;
    float zoom = kInvalidZoom;
    Point scroll(-1, -1);
    bool get_word = false;
    bool get_block = false;
    BOOL setFocus = 0;
    const char* next = str::Parse(cmd, "[GetText(\"%s\",%? \"%s\")]", &pdfFile, &txtFile);
    if (!next) {
        next = str::Parse(cmd, "[GetWord(\"%s\",%? \"%s\")]", &pdfFile, &txtFile);
        if (next) {
            logf("GetWord : ");
            get_word = true;
        } else {
            next = str::Parse(cmd, "[GetBlock(\"%s\",%? \"%s\")]", &pdfFile, &txtFile);
            if (next) {
                logf("GetBlock : ");
                get_block = true;
            }
        }
    }
    if (!next) {
        return nullptr;
    }
    if ((!get_block) && (!get_block)) {
        logf("GetText : ");
    }

    logf("HandleGetTextCmd: '%s'\n", cmd);
    MainWindow* win = FindMainWindowByFile(pdfFile, true);
    if (!win) {
        return next;
    }
    if (!win->IsDocLoaded()) {
        ReloadDocument(win, false);
        if (!win->IsDocLoaded()) {
            return next;
        }
    }

    if (get_block) {
        logf("  ... SaveBlocksToFile : '%s'\n", txtFile.Get());
        SaveBlocksToFile(win, txtFile.Get());
    } else if (get_word) {
        logf("  ... SaveWordsToFile : '%s'\n", txtFile.Get());
        SaveWordsToFile(win, txtFile.Get());
    } else {
        logf("  ... SaveTextToFile : '%s'\n", txtFile.Get());
        SaveTextToFile(win, txtFile.Get());
    }
    if (setFocus) {
        win->Focus();
    }
    logf("  ... done\n");
    *ack = true;
    return next;
}

/*
 CPS Lab.
 (2) [MarkWord("<pdffilepath>", "<SetupFile>")]
     [MarkWord("<pdffilepath>", "<Text>", "<Text>", ...)]
*/
static const char* HandleMarkWordCmd(const char* cmd, bool* ack) {
    AutoFreeStr pdfFile;
    float zoom = kInvalidZoom;
    Point scroll(-1, -1);
    // ---------------------------------------------
    // AutoFreeStr term;
    const char* next = str::Parse(cmd, "[MarkWord(\"%s\",%? ", &pdfFile);
    if (!next) {
        next = str::Parse(cmd, "[MarkWord(\"%s\")]", &pdfFile);
    }
    if (!next) {
        return nullptr;
    }
    logf("MarkWord : ");
    logf("HandleMarkWordCmd: '%s'\n", cmd);
    // ---------------------------------------------
    MainWindow* win = FindMainWindowByFile(pdfFile, true);
    if (!win) {
        return next;
    }
    if (!win->IsDocLoaded()) {
        ReloadDocument(win, false);
        if (!win->IsDocLoaded()) {
            return next;
        }
    }
    // ---------------------------------------------
    AutoFreeStr word;
    StrVec args;
    const char* curcmd = next;
    while (true) {
        next = str::Parse(curcmd, "\"%s\",%? ", &word);
        if (!next) {
            next = str::Parse(curcmd, "\"%s\")]", &word);
        }
        if (!next) {
            break;
        }
        // mark_words[0]->word_vec.Append(word.Get());
        args.Append(word.Get());
        curcmd = next;
    }
    // -----------------------------------------------------
    const char* first_word = nullptr;
    WindowTab* tab = win->CurrentTab();
    if (args.Size() == 0) {
        tab->markers->deleteAnnotations();
    } else if (args.Size() == 1) {
        first_word = MarkWords(win, args.At(0)); // mark words by setup-file.
    } else {
        first_word = MarkWords(win, args);
    }
    // ---------------------------------------------
    // Pan to first word.
    /*  no need to jump to first word
    if (first_word != nullptr) {
        bool wasModified = true;
        bool showProgress = true;
        HwndSetText(win->hwndFindEdit, first_word);
        FindTextOnThread(win, TextSearch::Direction::Forward, first_word, wasModified, showProgress);
        win->Focus();
    }
    */
    // ---------------------------------------------
    MainWindowRerender(win);
    logf("                done.'\n");
    *ack = true;
    return next;
}

/*
 CPS Lab.
[Select("<pdffilepath>", "<Category>", "<Text>", "<Text>", ...)]
*/
static const char* HandleSelectCmd(const char* cmd, bool* ack) {
    AutoFreeStr pdfFile;
    float zoom = kInvalidZoom;
    Point scroll(-1, -1);
    boolean select_block = false;
    boolean select_image = false;
    BOOL setFocus = 1;
    // ---------------------------------------------
    const char* next = str::Parse(cmd, "[Select(\"%s\",%? ", &pdfFile);
    if (!next) {
        next = str::Parse(cmd, "[SelectBlock(\"%s\",%? ", &pdfFile);
        if (next) {
            select_block = true;
        } else {
            next = str::Parse(cmd, "[SelectImage(\"%s\",%? ", &pdfFile);
            if (next) {
                select_image = true;
            }
        }
    }
    if (!next) {
        return nullptr;
    }
    // ---------------------------------------------
    MainWindow* win = FindMainWindowByFile(pdfFile, true);
    if (!win) {
        return next;
    }
    if (!win->IsDocLoaded()) {
        ReloadDocument(win, false);
        if (!win->IsDocLoaded()) {
            return next;
        }
    }
    // ---------------------------------------------
    DisplayModel* dm = win->AsFixed();
    WindowTab* tab = win->CurrentTab();
    const char* curcmd = next;
    if (select_image || select_block) {
        int pageNo;
        int x, y, dx, dy;
        next = str::Parse(curcmd, "%d, %d, %d, %d, %d)]", &pageNo, &x, &y, &dx, &dy);
        if (!win->ctrl->ValidPageNo(pageNo)) {
            return next;
        }
        win->ctrl->GoToPage(pageNo, true);
        DeleteOldSelectionInfo(win, true);
        if (select_image) {
            Rect rc = dm->CvtToScreen(pageNo, ToRectF(Rect(x, y, dx, dy)));
            win->CurrentTab()->selectionOnPage = SelectionOnPage::FromRectangle(dm, rc);
            win->showSelection = (win->CurrentTab()->selectionOnPage != nullptr);
            ScheduleRepaint(win, 0);
        } else {
            GetTextInRegion(dm, pageNo, Rect(x, y, dx, dy));
            UpdateTextSelection(win, false);
        }
    } else {
        AutoFreeStr wd;
        StrVec words;
        while (true) {
            next = str::Parse(curcmd, "\"%s\",%? ", &wd);
            if (!next) {
                next = str::Parse(curcmd, "\"%s\")]", &wd);
            }
            if (!next) {
                break;
            }
            words.Append(wd.Get());
            curcmd = next;
        }
        tab->markers->selectWords(win, words);
    }
    // ---------------------------------------------
    MainWindowRerender(win);
    if (setFocus) {
        win->Focus();
    }
    *ack = true;
    return next;
}

/*
Quit tool

[Quit]
*/
static const char* HandleQuitCmd(const char* cmd, bool* ack) {
    if (!str::StartsWith(cmd, "[Quit]")) {
        return nullptr;
    }
    logf("Quit\n");
    const char* next = cmd + str::Leni("[Quit]");
    CreateAndShowMainWindow(nullptr);
    *ack = true;
    // ======================
    SaveSettings();
    Vec<MainWindow*> toClose = gWindows;
    for (MainWindow* win : toClose) {
        CloseWindow(win, true, false);
    }
    return next;
}

// Returns nullptr if cmd is not a CPS Lab command, otherwise a pointer past it.
const char* HandleDdeCmd(const char* cmd, bool* ack) {
    const char* next = HandleGetTextCmd(cmd, ack);
    if (!next) {
        next = HandleMarkWordCmd(cmd, ack);
    }
    if (!next) {
        next = HandleSelectCmd(cmd, ack);
    }
    if (!next) {
        next = HandleQuitCmd(cmd, ack);
    }
    return next;
}

} // namespace cpslab
