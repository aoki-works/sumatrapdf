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
#include "TextSearch.h"
#include "Annotation.h"
#include "MainWindow.h"
#include "WindowTab.h"
#include "Selection.h"

#include "CpsLabAnnot.h"

namespace cpslab {

WCHAR* USERAPP_DDE_SERVICE = nullptr;
WCHAR* USERAPP_DDE_TOPIC = nullptr;
WCHAR* USERAPP_DDE_DEBUG_TOPIC = nullptr;

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


void Markers::sendSelectMessage(MainWindow* win) {
    if (USERAPP_DDE_SERVICE == nullptr || USERAPP_DDE_TOPIC == nullptr) {
        return;
    }
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
        DDEExecute(USERAPP_DDE_SERVICE, USERAPP_DDE_TOPIC, ToWstrTemp(cmd.Get()));
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
        DDEExecute(USERAPP_DDE_SERVICE, USERAPP_DDE_TOPIC, ToWstrTemp(cmd.Get()));
    }
    /**/
}

// =============================================================
//
// =============================================================
void CloseEvent(WindowTab* tab) {
    if (USERAPP_DDE_SERVICE == nullptr || USERAPP_DDE_TOPIC == nullptr) {
        return;
    }
    char* path = tab->filePath;
    if (path != nullptr) {
        str::Str cmd;
        cmd.AppendFmt("[PDFClosed(\"%s\")]", path);
        const WCHAR* w = ToWstrTemp(cmd.Get());
        DDEExecute(USERAPP_DDE_SERVICE, USERAPP_DDE_TOPIC, w, true);
    }
}

void CloseEvent(MainWindow* win) {
    if (USERAPP_DDE_SERVICE == nullptr || USERAPP_DDE_TOPIC == nullptr) {
        return;
    }
    for (auto& tab : win->Tabs()) {
        char* path = tab->filePath;
        if (path != nullptr) {
            str::Str cmd;
            cmd.AppendFmt("[PDFClosed(\"%s\")]", path);
            const WCHAR* w = ToWstrTemp(cmd.Get());
            DDEExecute(USERAPP_DDE_SERVICE, USERAPP_DDE_TOPIC, w, true);
        }
    }
}

// =============================================================
//
// =============================================================
void SaveWordsToFile(MainWindow* win, const char* fname) {
    DisplayModel* dm = win->AsFixed();
    char* text = dm->GetText();
    if (text == nullptr) {
        return;
    }

    StrVec word_vec;
    char* ctx;
    const char* delim = ", \n";
    char* wd = strtok_s(text, delim, &ctx);
    while (wd) {
        /* split only ascii characters */
        char* begin = nullptr;
        for (auto c = wd; *c; c++) {
            if (isascii(*c)) {
                if (begin == nullptr) {
                    begin = c;
                }
            } else {
                if (begin != nullptr) {
                    *c = 0;
                    word_vec.Append(begin);
                }
                begin = nullptr;
            }
        }
        if (begin != nullptr) {
            word_vec.Append(begin);
        }
        wd = strtok_s(nullptr, delim, &ctx);
    }
    word_vec.Sort();

    StrVec words;
    char* prev = nullptr;
    for (auto sel : word_vec) {
        if (prev == nullptr || !str::Eq(prev, sel)) {
            words.Append(sel);
            prev = sel;
        }
    }

    FILE* outFile = nullptr;
    WCHAR* tmpFileW = ToWstrTemp(fname);
    errno_t err = _wfopen_s(&outFile, tmpFileW, L"wb");
    if (err == 0) {
        for (auto sel : words) {
            std::fputs(sel, outFile);
            std::fputs("\n", outFile);
        }
        std::fclose(outFile);
    }
    str::Free(text);
}

// =============================================================
//
// =============================================================
void SaveTextToFile(MainWindow* win, const char* fname) {
    DisplayModel* dm = win->AsFixed();
    char* text = dm->GetText();
    if (text == nullptr) {
        return;
    }

    FILE* outFile = nullptr;
    WCHAR* tmpFileW = ToWstrTemp(fname);
    errno_t err = _wfopen_s(&outFile, tmpFileW, L"wb");
    if (err == 0) {
        std::fwrite(text, 1, std::strlen(text), outFile);
        std::fclose(outFile);
    }
    str::Free(text);
}

// =============================================================
//
// =============================================================
const char* MarkWords(MainWindow* win) {
    WindowTab* tab = win->CurrentTab();
    DisplayModel* dm = tab->AsFixed();
    auto engine = dm->GetEngine();
    // ---------------------------------------------
    char* first_word = nullptr;
    for (auto marker_node : tab->markers->markerTable) {
        str::Str annot_key_content("@CPSLabMark:");
        annot_key_content.Append(marker_node->keyword.Get());
        annot_key_content.AppendChar('@');
        // -- ClearSearchResult ------------
        DeleteOldSelectionInfo(win, true);
        RepaintAsync(win, 0);
        // ---------------------------------
        dm->textSearch->SetDirection(TextSearchDirection::Forward);
        bool conti = false;
        for (auto term : marker_node->words) {
            TextSel* sel = dm->textSearch->FindFirst(1, strconv::Utf8ToWstr(term), nullptr, conti);
            if (!sel) {
                continue;
            }
            if (first_word == nullptr) {
                first_word = term;
            }
            do {
                for (int ixi = 0; ixi < sel->len; ixi++) {
                    marker_node->rects.Append(sel->rects[ixi]);
                }
                dm->textSelection->CopySelection(dm->textSearch, conti);
                UpdateTextSelection(win, false);
                conti = true;
                sel = dm->textSearch->FindNext(nullptr, conti);
            } while (sel);
        }
        //  ---------------------------------------------
        Vec<SelectionOnPage>* s = tab->selectionOnPage;
        if (s != nullptr) {
            Vec<int> pageNos;
            for (auto& sel : *s) {
                int pno = sel.pageNo;
                if (!dm->ValidPageNo(pno)) {
                    continue;
                }
                bool fo = false;
                for (auto n : pageNos) {
                    if (n == pno) {
                        fo = true;
                        break;
                    }
                }
                if (!fo) {
                    pageNos.Append(pno);
                }
            }
            for (auto pno : pageNos) {
                Vec<RectF> rects;
                for (auto& sel : *s) {
                    if (pno != sel.pageNo) {
                        continue;
                    }
                    rects.Append(sel.rect);
                }
                Annotation* annot = EngineMupdfCreateAnnotation(engine, AnnotationType::Highlight, pno, PointF{});
                SetQuadPointsAsRect(annot, rects);
                SetColor(annot, marker_node->mark_color); // Acua
                SetContents(annot, annot_key_content.Get());
                marker_node->annotations.Append(annot);
            }
            tab->askedToSaveAnnotations = true;
            DeleteOldSelectionInfo(win, true);
        }
    }
    return first_word;
}

// =============================================================
//
// =============================================================
const char* MarkWords(MainWindow* win, const char* json_file) {
    WindowTab* tab = win->CurrentTab();
    tab->markers->deleteAnnotations();
    tab->markers->parse(json_file);
    return MarkWords(win);
}

// =============================================================
//
// =============================================================
const char* MarkWords(MainWindow* win, StrVec& words) {
    WindowTab* tab = win->CurrentTab();
    tab->markers->deleteAnnotations();
    MarkerNode* m = tab->markers->getMarker("Net");
    for (auto w : words) {
        m->words.Append(w);
    }
    return MarkWords(win);
}

// =============================================================
//
// =============================================================
char* GetWordsInRegion(const DisplayModel* dm, int pageNo, const Rect regionI) {
    Rect* coords;
    const WCHAR* pageText = dm->textCache->GetTextForPage(pageNo, nullptr, &coords);
    if (str::IsEmpty(pageText)) {
        return nullptr;
    }

    str::WStr result;
    int selected_words = 0;
    for (const WCHAR* src = pageText; *src; ) {
        if (*src == '\n') { ++src; continue; }
        if (!isWordChar(*src)) { ++src; continue; }
        /* check whether this 'letter' is intersect with the regionI */
        Rect rect = coords[src - pageText]; // boundary rectangle of this 'letter'.
        Rect isect = regionI.Intersect(rect);
        if (isect.IsEmpty() || 1.0 * isect.dx * isect.dy / (rect.dx * rect.dy) < 0.3) {
            ++src;
            continue;       // not intersected.
        }
        // backword search the begin letter of 'word'.
        const WCHAR* begin = src;
        for (; *begin; --begin) {if (!isWordChar(*begin)) break;}
        if (!isWordChar(*begin)) begin++;
        // forword search the end letter of 'word'.
        const WCHAR* end = src;
        for (; *end; ++end) {if (!isWordChar(*end)) break;}
        /**/
        rect = coords[begin - pageText];  // boundary rectangle of begin letter.
        int px = rect.x + rect.dx / 2.0;
        int py = rect.y + rect.dy / 2.0;
        dm->textSelection->StartAt(pageNo, px, py);
        /* */
        rect = coords[end - pageText - 1];  // boundary rectangle of end letter.
        px = rect.x + rect.dx ;
        py = rect.y + rect.dy / 2.0;
        dm->textSelection->SelectUpTo(pageNo, px, py, 0 < selected_words);
        /* */
        result.Append(begin, end - begin);  // append 'word' to result.
        result.Append(L"\r\n", 2);
        src = end;
        selected_words++;
    }
    WCHAR* ws = result.Get();
    return ToUtf8(ws);
}

// =============================================================
//
// =============================================================
char* GetWordsInCircle(const DisplayModel* dm, int pageNo, const Rect regionI) {
    Rect* coords;
    const WCHAR* pageText = dm->textCache->GetTextForPage(pageNo, nullptr, &coords);
    if (str::IsEmpty(pageText)) {
        return nullptr;
    }

    str::WStr result;
    int radius = (regionI.dx < regionI.dy ?  regionI.dy : regionI.dx) / 2;
    float sqrr = pow(radius, 2);
    int cx = regionI.x + regionI.dx / 2;
    int cy = regionI.y + regionI.dy / 2;
    int selected_words = 0;
    for (const WCHAR* src = pageText; *src; ) {
        if (*src == '\n') { ++src; continue; }
        if (!isWordChar(*src)) { ++src; continue; }
        /* check whether this 'letter' is intersect with the circle */
        Rect rect = coords[src - pageText]; // boundary rectangle of this 'letter'.
        Rect rc;
        if (0 < rect.dx) rc = Rect(rect.x - radius, rect.y, rect.dx + 2 * radius, rect.dy);
        else             rc = Rect(rect.x + radius, rect.y, rect.dx - 2 * radius, rect.dy);
        Rect isect = regionI.Intersect(rc);
        if (isect.IsEmpty() || 1.0 * isect.dx * isect.dy / (rect.dx * rect.dy) < 0.3) {
            ++src;
            continue;
        }
        if (0 < rect.dy) rc = Rect(rect.x, rect.y - radius, rect.dx, rect.dy + 2 * radius);
        else             rc = Rect(rect.x, rect.y + radius, rect.dx, rect.dy - 2 * radius);
        isect = regionI.Intersect(rc);
        if (isect.IsEmpty() || 1.0 * isect.dx * isect.dy / (rect.dx * rect.dy) < 0.3) {
            ++src;
            continue;
        }
        if (sqrr <= pow(rect.x           - cx, 2) + pow(rect.y           - cy, 2)) {++src; continue;}
        if (sqrr <= pow(rect.x + rect.dx - cx, 2) + pow(rect.y           - cy, 2)) {++src; continue;}
        if (sqrr <= pow(rect.x           - cx, 2) + pow(rect.y + rect.dy - cy, 2)) {++src; continue;}
        if (sqrr <= pow(rect.x + rect.dx - cx, 2) + pow(rect.y + rect.dy - cy, 2)) {++src; continue;}
        // backword search the begin letter of 'word'.
        const WCHAR* begin = src;
        for (; *begin; --begin) {if (!isWordChar(*begin)) break;}
        if (!isWordChar(*begin)) begin++;
        // forword search the end letter of 'word'.
        const WCHAR* end = src;
        for (; *end; ++end) {if (!isWordChar(*end)) break;}
        /**/
        rect = coords[begin - pageText];  // boundary rectangle of begin letter.
        int px = rect.x + rect.dx / 2.0;
        int py = rect.y + rect.dy / 2.0;
        dm->textSelection->StartAt(pageNo, px, py);
        /* */
        rect = coords[end - pageText - 1];  // boundary rectangle of end letter.
        px = rect.x + rect.dx ;
        py = rect.y + rect.dy / 2.0;
        dm->textSelection->SelectUpTo(pageNo, px, py, 0 < selected_words);
        /**/
        result.Append(begin, end - begin);
        result.Append(L"\r\n", 2);
        src = end;
        selected_words++;
    }
    WCHAR* ws = result.Get();
    return ToUtf8(ws);
}//

} // end of namespace cpslab


