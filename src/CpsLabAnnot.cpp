/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "utils/BaseUtil.h"
#include "utils/ScopedWin.h"
#include "utils/WinUtil.h"
#include "utils/FileUtil.h"
#include "utils/JsonParser.h"
#include "wingui/UIModels.h"
#include "Settings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "EngineAll.h"
#include "GlobalPrefs.h"
#include "ChmModel.h"
#include "DisplayModel.h"
#include "SumatraPDF.h"
#include "TextSelection.h"
#include "Annotation.h"
#include "MainWindow.h"
#include "WindowTab.h"
#include "Selection.h"

#include "CpsLabAnnot.h"

// CPS Lab.
extern WCHAR* USERAPP_DDE_SERVICE;
extern WCHAR* USERAPP_DDE_TOPIC;
extern WCHAR* USERAPP_DDE_DEBUG_TOPIC;

namespace cpslab {

// =============================================================
//
// =============================================================
struct MarkFileParser : json::ValueVisitor {
    /*
     *  { "Net": {"mark_color" : "coloe_code",
                  "select_color" : "coloe_code",
                  "words" : ["xx", "",...] }
              }
     */
    WindowTab* tab;
    Vec<MarkerNode*> markerTable;
    MarkerNode* getMark(const char* keyword);
    bool mark_color(const char* path, const char* value) ;
    bool select_color(const char* path, const char* value) ;
    bool words(const char* path, const char* value);
    //
    bool Visit(const char* path, const char* value, json::Type type) override;
    void Parse(const char* path);
};

void MarkFileParser::Parse(const char* path) {
    ByteSlice data = file::ReadFile(path);
    json::Parse(data, this);
    data.Free();
}

MarkerNode* MarkFileParser::getMark(const char* keyword) {
    for(auto m : markerTable) {
        if (str::Eq(m->keyword.Get(), keyword)) {
            return m;
        }
    }
    MarkerNode* m = new MarkerNode(tab);
    m->keyword.Set(str::Dup(keyword));
    markerTable.Append(m);
    return m;
}

bool MarkFileParser::mark_color(const char* path, const char* value) {
    AutoFreeStr keyword;
    const char* prop = str::Parse(path, "/%s/mark_color", &keyword);
    if (prop == nullptr) {
        return false;
    }
    MarkerNode* mark = getMark(keyword.Get());
    mark->mark_color = 0xff000000 + std::strtol(value, nullptr, 16);
    return true;
}

bool MarkFileParser::select_color(const char* path, const char* value) {
    AutoFreeStr keyword;
    const char* prop = str::Parse(path, "/%s/select_color", &keyword);
    if (prop == nullptr) {
        return false;
    }
    MarkerNode* mark = getMark(keyword.Get());
    mark->select_color = 0xff000000 + std::strtol(value, nullptr, 16);
    return true;
}

bool MarkFileParser::words(const char* path, const char* value) {
    AutoFreeStr keyword;
    int idx;
    const char* prop = str::Parse(path, "/%s/word[%d]", &keyword, &idx);
    if (prop == nullptr) {
        return false;
    }
    MarkerNode* mark = getMark(keyword.Get());
    mark->words.Append(value);
    return true;
}

bool MarkFileParser::Visit(const char* path, const char* value, json::Type type) {
    if (json::Type::String != type) return true;
    if (this->mark_color(path, value))   return true;
    if (this->select_color(path, value)) return true;
    if (this->words(path, value))         return true;
    return true;
}

// =============================================================
//
// =============================================================
MarkerNode::MarkerNode(WindowTab* tab)
    : tab_(tab), filePath_(), keyword(), mark_color(0xff00ffff), select_color(0xff00ffff), words(), annotations() {
    mark_color = 0xff00ffff;
    select_color = 0xff0000ff;
}

MarkerNode::~MarkerNode() {
    for (auto a : annotations) {
        DeleteAnnotation(a);
    }
}

// =============================================================
//
// =============================================================
Markers::Markers(WindowTab* tab) {
    tab_ = tab;
}

Markers::~Markers() {
    deleteAnnotations();
}

void Markers::parse(const char* fname) {
    MarkFileParser  mfp;
    mfp.tab = tab_;
    mfp.Parse(fname);
    for(auto m : mfp.markerTable) {
        markerTable.Append(m);
    }
}

void Markers::deleteAnnotations() {
    while (0 < markerTable.size()) {
        auto m = markerTable.Pop();
        delete m;
    }
    markerTable.Reset();
}

MarkerNode* Markers::getMarker(const char* keyword) {
    for (auto p : markerTable) {
        if (str::Eq(p->keyword, keyword)) {
            return p;
        }
    }
    MarkerNode* marker_node = new MarkerNode(tab_);
    marker_node->keyword.SetCopy(keyword);
    // marker_node->filePath.SetCopy(tab->filePath.Get());
    // marker_node->mark_color;
    // marker_node->select_color;
    // MARKER_TABLE.Append(marker_node);
    markerTable.Append(marker_node);
    return marker_node;
}

size_t Markers::getMarkersByWord(const char* word, Vec<MarkerNode*>& result) {
    size_t n = 0;
    for (auto p : markerTable) {
        for (auto w : p->words) {
            if (str::Eq(w, word)) {
                result.Append(p);
                n++;
                break;
            }
        }
    }
    return n;
}

size_t Markers::getMarkersByWord(const WCHAR* word, Vec<MarkerNode*>& result) {
    return getMarkersByWord(strconv::WstrToUtf8(word), result);
}

size_t Markers::getMarkersByRect(Rect& r, Vec<MarkerNode*>& result) {
    size_t n = 0;
    for (auto p : markerTable) {
        for (Rect pr : p->rects) {
            if (r == pr) {
                result.Append(p);
                n++;
                break;
            }
        }
    }
    return n;
}

size_t Markers::getMarkersByTS(TextSelection* ts, Vec<MarkerNode*>& result) {
    size_t n = 0;
    for (auto p : markerTable) {
        for (int i = 0; i < ts->result.len; ++i) {
            Rect r = ts->result.rects[i];
            for (Rect pr : p->rects) {
                if (r == pr) {
                    result.Append(p);
                    n++;
                    break;
                }
            }
        }
    }
    return n;
}


void Markers::sendMessage(MainWindow* win) {
    DisplayModel* dm = win->AsFixed();
    //if (dm->textSelection->result.len == 0) { return; }
    const char* sep = "\r\n";
    bool isS = true;
    char* selection = GetSelectedText(tab_, sep, isS);
    UpdateTextSelection(win, false);
    //RepaintAsync(win, 0);
    /*
    WCHAR* ws = dm->textSelection->ExtractText(" ");
    char* selection = ToUtf8(ws);
    str::Free(ws);
    */
    if (str::IsEmpty(selection)) {
        return;
    }
    //
    for (auto m : markerTable) { m->selected_words.Reset(); }
    //
    StrVec words;
    bool collapse = true;
    Split(words, selection, sep, collapse);
    //
    /*
    str::Str cmd("[Select(");
    for (size_t i = 0; i < words.size(); ++i) {
        char* s = words.at(i);
        if (0 < i) {
            cmd.Append(",", 1);
        }
        cmd.AppendFmt("\"%s\"", s);
    }
    cmd.Append(")]", 2);
    DDEExecute(USERAPP_DDE_SERVICE, USERAPP_DDE_TOPIC, ToWstrTemp(cmd.Get()));
    */
    /* */
    //
    //str::Str cmd;
    for (size_t i = 0; i < words.size(); ++i) {
        char* s = words.at(i);
        Rect r = dm->textSelection->result.rects[i];
        //cmd.AppendFmt("(%s %d %d %d %d)", s, r.x, r.y, r.dx, r.dy);
        // tab->markers->getMarkersByWord(selection.Get(), result);
        Vec<MarkerNode*> result;
        getMarkersByRect(r, result);
        for (auto m : result) {
            if (!m->selected_words.Contains(s)) {
                m->selected_words.Append(s);
            }
        }
    }

    /*{
        cmd.AppendFmt("[MMM ");
        for (int i = 0; i < dm->textSelection->result.len; ++i) {
            Rect r = dm->textSelection->result.rects[i];
            cmd.AppendFmt("(%d %d %d %d)", r.x, r.y, r.dx, r.dy);
        }
        cmd.AppendFmt(" = ");
        for (auto p : markerTable) {
            for (auto r : p->rects) {
                cmd.AppendFmt("(%d %d %d %d)", r.x, r.y, r.dx, r.dy);
            }
        }

        if (USERAPP_DDE_SERVICE != nullptr && USERAPP_DDE_TOPIC != nullptr) {
            DDEExecute(USERAPP_DDE_SERVICE, USERAPP_DDE_TOPIC, ToWstrTemp(cmd.Get()));
        }
    }*/

    for (auto m : markerTable) {
        if (m->selected_words.Size() == 0) {
            continue;
        }
        str::Str cmd;
        // cmd.AppendFmt("[Search(\"%ls\")]", selection.Get());
        cmd.AppendFmt("[Select(\"%s\", \"%s\"", tab_->filePath.Get(), m->keyword.Get());
        for (auto s : m->selected_words) {
            cmd.AppendFmt(", \"%s\"", s);
        }
        cmd.AppendFmt(")]");
        if (USERAPP_DDE_SERVICE != nullptr && USERAPP_DDE_TOPIC != nullptr) {
            DDEExecute(USERAPP_DDE_SERVICE, USERAPP_DDE_TOPIC, ToWstrTemp(cmd.Get()));
        }
    }
    /**/
}



} // end of namespace cpslab


