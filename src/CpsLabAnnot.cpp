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
WCHAR* PDFSYNC_DDE_SERVICE = nullptr;
WCHAR* PDFSYNC_DDE_TOPIC = nullptr;
CpsMode MODE = CpsMode::Document;
const char* EXPORT_TEXT_BLOCKS = nullptr;

// =============================================================
//
// =============================================================
struct MarkFileParser : json::ValueVisitor {
    /*
     *  { "Net": {"mark_color" : "coloe_code",
     *            "select_color" : "coloe_code",
     *            "word" : ["xx", "",...] }
     *  }
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
class WordBlock {
    using node_t = std::pair<std::wstring, std::vector<int>* >;
    std::string          keyword_;
    std::vector<node_t>* words_;

  public:
    WordBlock() : keyword_(), words_(nullptr) { }
    WordBlock(const char* kwd) : keyword_(kwd), words_(nullptr)
    {
        words_ = new std::vector<node_t>;
    }
    ~WordBlock() {
        if (words_ != nullptr) {
            for (auto w : *words_) {
                delete w.second;
            }
            delete words_;
        }
    }
    const char* keyword(void) { return keyword_.c_str(); }
    std::vector<node_t>* words(void){ return words_; }
    std::vector<int>* add(const wchar_t* wd) {
        auto pages = new std::vector<int>;
        words_->push_back(node_t(wd, pages));
        return pages;
    }
};

// =============================================================
//
// =============================================================
MarkerNode::MarkerNode(WindowTab* tab)
    : tab_(tab), filePath_(), keyword(), mark_color(0xff00ffff), select_color(0xff00ffff),
      words(), annotations(), mark_words(), rects(), pages(),
      selected_words(), assoc_cells()
{
    mark_color = 0xff00ffff;
    select_color = 0xff0000ff;
}

MarkerNode::~MarkerNode() {
    for (auto a : annotations) {
        DeleteAnnotation(a);
    }
}

const char* MarkerNode::selectWord(MainWindow* win, const int pageNo, char* wd, bool conti) {

    char* first_word = nullptr;
    DisplayModel* dm = win->AsFixed();
    dm->textSearch->SetDirection(TextSearchDirection::Forward);
    dm->textSearch->wordSearch = true;
    for (int i = 0; i < words.Size(); ++i) {
        char* mark_word = words.At(i);
        if (!str::Eq(wd, mark_word)) {
            continue;
        }
        const WCHAR* wsep = strconv::Utf8ToWStr(wd);
        //TextSel* sel = dm->textSearch->FindFirst(1, strconv::Utf8ToWStr(wd), nullptr, conti);
        TextSel* sel = dm->textSearch->FindFirst(pageNo, wsep, nullptr, conti);
        if (sel == nullptr) {
            str::Free(wsep);
            continue;
        }
        if (!conti) {
            //bool prev = gGlobalPrefs->showToolbar;
            //gGlobalPrefs->showToolbar = false;      // to avoid calling find-function.
            //HwndSetText(win->hwndFindEdit, wd);
            //gGlobalPrefs->showToolbar = prev;
            first_word = wd;
            dm->ShowResultRectToScreen(sel);
            //moveto = false;
        }
        do {
            dm->textSelection->CopySelection(dm->textSearch, conti);
            conti = true;
            sel = dm->textSearch->FindNext(nullptr, conti);
        } while (sel);
        str::Free(wsep);
    }
    dm->textSearch->wordSearch = false;
    return first_word;
}


const char* MarkerNode::selectWords(MainWindow* win, StrVec& select_words, bool conti) {
    const char* first_word = nullptr;
    for(auto wd : select_words) {
        const char* ret = selectWord(win, 1, wd, conti);
        if (ret != nullptr) { first_word = ret; }
    }
    return first_word;
}



size_t MarkerNode::getMarkWordsByPageNo(const int pageNo, StrVec& result) {
    int i = 0;
    for (auto pno : pages) {
        if (pno == pageNo) {
            auto w = mark_words.at(i);
            result.Append(w);
        }
        ++i;
    }
    return result.Size();
}

int MarkerNode::getPage(const char* cell, const int pageNo) {
    int i = 0;
    for (char* w : mark_words) {
        if (str::Eq(cell, w)) {
            int target_pageNo = pages.at(i);
            if (0 < pageNo) {
                if (pageNo <= target_pageNo) {
                    return target_pageNo;
                }
            } else {
                return target_pageNo;
            }
        }
        ++i;
    }
    return -1;
}

bool MarkerNode::tExist(const int pageNo, const char* cell) {
    int i = 0;
    for (auto pno : pages) {
        if (pno == pageNo) {
            auto w = mark_words.at(i);
            if (str::Eq(cell, w)) {
                return true;
            }
        }
        ++i;
    }
    return false;
}

// =============================================================
//
// =============================================================
Markers::Markers(WindowTab* tab) : tab_(), select_(), page_in_cell_() {
    tab_ = tab;
    select_ = (0x01 | 0x02 | 0x04);
}

Markers::~Markers() {
    deleteAnnotations();
}

bool Markers::isSelection(const char* keyword)
{
    if (str::Eq(keyword, "Net")) {
        return select_ & 0x01;
    } else if (str::Eq(keyword, "Cell")) {
        return select_ & 0x02;
    } else if (str::Eq(keyword, "Pin")) {
        return select_ & 0x04;
    }
    return false;
}

void Markers::setSelection(const char* keyword)
{
    if (str::Eq(keyword, "Net")) {
        select_ |= 0x01;
    } else if (str::Eq(keyword, "Cell")) {
        select_ |= 0x02;
    } else if (str::Eq(keyword, "Pin")) {
        select_ |= 0x04;
    }
}

void Markers::unsetSelection(const char* keyword)
{
    if (str::Eq(keyword, "Net")) {
        select_ &= ~0x01;
    } else if (str::Eq(keyword, "Cell")) {
        select_ &= ~0x02;
    } else if (str::Eq(keyword, "Pin")) {
        select_ &= ~0x04;
    }
}

void Markers::parse(const char* fname) {
    MarkFileParser  mfp;
    mfp.tab = tab_;
    mfp.Parse(fname);
    for(auto m : mfp.markerTable) {
        markerTable.Append(m);
    }
    page_in_cell_.Reset();
}

void Markers::deleteAnnotations() {
    while (0 < markerTable.size()) {
        auto m = markerTable.Pop();
        delete m;
    }
    markerTable.Reset();
    page_in_cell_.Reset();
}

MarkerNode* Markers::getMarker(const char* keyword) {
    for (auto p : markerTable) {
        if (str::Eq(p->keyword, keyword)) {
            return p;
        }
    }
    MarkerNode* marker_node = new MarkerNode(tab_);
    marker_node->keyword.SetCopy(keyword);
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
    return getMarkersByWord(strconv::WStrToUtf8(word), result);
}

size_t Markers::getMarkersByRect(Rect& r, Vec<MarkerNode*>& result, bool specified_object_only)
{
    size_t n = 0;
    for (auto p : markerTable) {
        if (specified_object_only && !isSelection(p->keyword)) {
            continue;
        }
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

const char* Markers::getCellsInPage(const int pageNo) {
    for (PageInCell& c : page_in_cell_) {
        if (c.pageNo == pageNo) {
            return c.cells.Get();
        }
    }
    str::Str cellsInPage;
    for (auto m : markerTable) {
        if (m->keyword != nullptr && str::Eq(m->keyword, "Cell")) {
            StrVec cellVect;
            m->getMarkWordsByPageNo(pageNo, cellVect);
            for (auto c : cellVect) { cellsInPage.AppendFmt(", \"%s\"", c); }
        }
    }
    page_in_cell_.Append(PageInCell());
    page_in_cell_.Last().pageNo = pageNo;
    page_in_cell_.Last().cells = cellsInPage;
    return page_in_cell_.Last().cells.Get();
}

void Markers::sendSelectMessage(MainWindow* win, bool conti) {
    if (USERAPP_DDE_SERVICE == nullptr || USERAPP_DDE_TOPIC == nullptr) {
        return;
    }

    const char* sep = "\r\n";
    DisplayModel* dm = win->AsFixed();

    if (!tab_->selectionOnPage) {
        return;
    }
    if (tab_->selectionOnPage->size() == 0) {
        return;
    }
    if (dm->GetEngine()->IsImageCollection()) {
        return;
    }
    for (auto m : markerTable) {
        m->selected_words.Reset();
        m->assoc_cells.Reset();
    }

    for (SelectionOnPage& sel : *tab_->selectionOnPage) {
        char* text;
        Rect regionI = sel.rect.Round();
        bool isTextOnlySelectionOut = dm->textSelection->result.len > 0;
        if (isTextOnlySelectionOut) {
            // Selected by w-click.
            WCHAR* s = dm->textSelection->ExtractText(sep);
            text = ToUtf8(s);
            Rect r = dm->textSelection->result.rects[dm->textSelection->result.len - 1];
            Vec<MarkerNode*> nodes;
            getMarkersByRect(r, nodes);
            if (nodes.Size() == 0) {
                // dm->textSelection->result.len--;
            } else {
                for (auto m : nodes) {
                    if (!m->selected_words.Contains(text)) {
                        m->selected_words.Append(text);
                        if (m->keyword != nullptr && str::Eq(m->keyword, "Pin")) {
                            m->assoc_cells.Append(getCellsInPage(sel.pageNo));
                        }
                    }
                }
            }
            str::Free(s);
        } else {
            // Selected by area.
            if (gGlobalPrefs->circularSelectionRegion) {
                text = GetWordsInCircle(dm, sel.pageNo, regionI, sep, this);

            } else {
                text = GetWordsInRegion(dm, sel.pageNo, regionI, sep, this);
            }
        }
        if (!str::IsEmpty(text)) {
            //str_vec.Append(text);
            str::Free(text);
        }
    }
    UpdateTextSelection(win, false);

    for (auto m : markerTable) {
        StrVec selected_words;
        StrVec assoc_cells;
        bool is_pin = (m->keyword != nullptr && str::Eq(m->keyword, "Pin"));
        if (m->selected_words.Size() == 0) {
            continue;
        }
        for (int i = 0; i < m->selected_words.Size(); i++) {
            auto s = m->selected_words.At(i);
            if (!selected_words.Contains(s)) {
                selected_words.Append(s);
                if (is_pin) {
                    auto c = m->assoc_cells.at(i);
                    assoc_cells.Append(c);
                }
            }
        }
        // -----------------------------------------------
        str::Str cmd;
        if (is_pin) {
            if (conti) {
                cmd.AppendFmt("[CPinSelect(\"%s\"", tab_->filePath.Get());
            } else {
                cmd.AppendFmt("[PinSelect(\"%s\"", tab_->filePath.Get());
            }
            for (int i = 0; i < selected_words.Size(); i++) {
                auto s = selected_words.At(i);
                auto c = assoc_cells.At(i);
                cmd.AppendFmt(", (\"%s\" %s)", s, c);
            }
        } else {
            if (conti) {
                // Continue selection
                cmd.AppendFmt("[CSelect(\"%s\"", tab_->filePath.Get());
            } else {
                // Newly selection
                cmd.AppendFmt("[Select(\"%s\"", tab_->filePath.Get());
            }
            for (int i = 0; i < selected_words.Size(); i++) {
                auto s = selected_words.At(i);
                cmd.AppendFmt(", \"%s\"", s);
            }
        }
        cmd.AppendFmt(")]");
        DDEExecute(USERAPP_DDE_SERVICE, USERAPP_DDE_TOPIC, ToWStrTemp(cmd.Get()));
    }
}

static void SetSelectedWordToFindEdit(MainWindow*win,  StrVec& words) {
    const char* line = Join(words, " ");
    bool prev = gGlobalPrefs->showToolbar;
    gGlobalPrefs->showToolbar = false;      // to avoid calling find-function.
    HwndSetText(win->hwndFindEdit, line);
    gGlobalPrefs->showToolbar = prev;
}

void Markers::selectWords(MainWindow* win, const char* keyword, StrVec& words) {
    DeleteOldSelectionInfo(win, true);
    RepaintAsync(win, 0);
    bool conti = false;
    MarkerNode* node = getMarker(keyword);
    if (node != nullptr) {
        if (node->selectWords(win, words, conti) != nullptr) {
            conti = true;
        }
    }
    SetSelectedWordToFindEdit(win, words);
    UpdateTextSelection(win, false);
}

void Markers::selectWords(MainWindow* win, StrVec& words) {
    DeleteOldSelectionInfo(win, true);
    RepaintAsync(win, 0);
    MarkerNode* cn = getMarker("Cell");
    MarkerNode* pn = getMarker("Pin");
    bool conti = false;
    for (auto w : words) {
        AutoFreeStr cellName, pinName;
        const char* next = str::Parse(w, "%s:%s", &cellName, &pinName);
        bool is_pin = (next != nullptr);
        if (is_pin) {
            int curPageNo = -1;
            int pageNo = -1;
            do {
                pageNo = cn->getPage(cellName.Get(), curPageNo);
                if (0 < pageNo) {
                    if (pn->tExist(pageNo, pinName.Get())) {
                        if (pn->selectWord(win, pageNo, pinName.Get(), conti) != nullptr) {
                            conti = true;
                        }
                        break;
                    } else {
                        curPageNo = pageNo + 1;
                    }
                }
            } while (0 < pageNo);
        } else {
            for (auto node : markerTable) {
                if (node->selectWord(win, 1, w, conti) != nullptr) {
                    conti = true;
                }
            }
        }
    }
    /*
    for (auto node : markerTable) {
        if (node->selectWords(win, words, conti) != nullptr) {
            conti = true;
        }
    }
    */
    SetSelectedWordToFindEdit(win, words);
    UpdateTextSelection(win, false);
}

// =============================================================
//
// =============================================================
TmpAllocator::TmpAllocator() {
    len = 100;
    //p = ::AllocZero(len, sizeof(char));
    p = malloc(len * sizeof(char));
}
TmpAllocator::~TmpAllocator() {
    if (p != nullptr) {
        // free(p);
    }
}

void* TmpAllocator::Alloc(size_t size) {

    if (len <= size) {
        free(p);
        len = size + 100;
        //p = ::AllocZero(len, sizeof(char));
        p = malloc(len * sizeof(char));
    }
    return p;
}

void* TmpAllocator::Realloc(void* men, size_t size)
{
    if (len <= size) {
        void* tmp = realloc(men, size);
        len = size;
        free(p);
        p = tmp;
    }
    return p;
}

void TmpAllocator::Free(const void*)
{
}


// =============================================================
//
// =============================================================
bool IsWord(const WCHAR* begin, const size_t length) {
    if (*begin == '\n') {
        return false;
    }
    if (!isWordChar(*begin)) {
        return false;
    }
    if (isWordChar(*(begin - 1))) {
        return false;
    }
    if (isWordChar(*(begin + length))) {
        return false;
    }
    return true;
}


bool IsWord(const WCHAR* pageText, const Rect* coords, const WCHAR* begin, const WCHAR* end) {
    if (!isWordChar(*begin)) {
        return false;
    }
    // -----------------------------------------------------------------
    // Check wheter 'begin' is the beginning character of a word.
    // -----------------------------------------------------------------
    Rect rect = coords[begin - pageText];   // boundary rectangle of 'begin' character.
    if (begin != pageText) {
        if (isWordChar(*(begin - 1))) {
            // The previous character of 'begin' is also word-character.
            if (gGlobalPrefs->printableCharAsWordChar) {
                Rect r = coords[begin - pageText - 1]; // boundary rectangle of the previous char of 'begin'.
                if (r.x == rect.x || r.y == rect.y) {
                    // 'begin' and 'begin-1' is on the same line.
                    // Then the 'begin' is not beginning of a word.
                    return false;
                } else {
                    // 'begin' and 'begin-1' is not on the same line.
                    // Then 'begin' is the beginning character of a word.
                }
            }
        }
    }
    // -----------------------------------------------------------------
    // Check wheter the 'end' is a word-character, and
    // on the same line of 'begin'.
    // -----------------------------------------------------------------
    if (isWordChar(*(end))) {
        if (gGlobalPrefs->printableCharAsWordChar) {
            Rect r = coords[end - pageText];
            if (r.x == rect.x || r.y == rect.y) {
                return false;
            }
        }
    }
    //return true;
    // -----------------------------------------------------------------
    // Check wheter from 'begin' to 'end' is a word-character, and
    // on the same line.
    // -----------------------------------------------------------------
    // forword search the end letter of 'word'.
    for (auto c = begin + 1; c < end; ++c) {
        if (!isWordChar(*c)) {
            return false;
        }
        if (gGlobalPrefs->printableCharAsWordChar) {
            Rect r = coords[c - pageText];
            if (r.x != rect.x && r.y != rect.y) {
                return false;
            }
        }
    }
    return true;
}


// =============================================================
//
// =============================================================
const WCHAR* SelectWordAt(const DisplayModel* dm, int pageNo, const WCHAR* pageText,
                          const Rect* coords, const WCHAR* src, const WCHAR* lineSep,
                          str::WStr& result,
                          Markers* markers=nullptr,
                          bool specified_object_only=false) {
    if (*src == '\n') {
        return (src + 1);
    }
    if (!isWordChar(*src)) {
        return (src + 1);
    }
    // backword search the begin letter of 'word'.
    int lineSep_len = str::Len(lineSep);
    const WCHAR* begin = src;
    Rect rect = coords[begin - pageText];
    for (; *begin; --begin) {
        if (!isWordChar(*begin)) {
            begin++;
            break;
        }
        if (gGlobalPrefs->printableCharAsWordChar) {
            Rect r = coords[begin - pageText];
            if (r.x != rect.x && r.y != rect.y) {
                begin++;
                break;
            }
        }
    }
    // forword search the end letter of 'word'.
    const WCHAR* end = src;
    for (; *end; ++end) {
        if (!isWordChar(*end)) {
            break;
        }
        if (gGlobalPrefs->printableCharAsWordChar) {
            Rect r = coords[end - pageText];
            if (r.x != rect.x && r.y != rect.y) {
                break;
            }
        }
    }
    /**/
    rect = coords[begin - pageText]; // boundary rectangle of begin letter.
    int px = rect.x + rect.dx / 2.0;
    int py = rect.y + rect.dy / 2.0;
    dm->textSelection->StartAt(pageNo, px, py);
    /* */
    rect = coords[end - pageText - 1]; // boundary rectangle of end letter.
    px = rect.x + rect.dx;
    py = rect.y + rect.dy / 2.0;
    dm->textSelection->SelectUpTo(pageNo, px, py, !result.IsEmpty());
    /* */
    if (markers != nullptr) {
        Rect r = dm->textSelection->result.rects[dm->textSelection->result.len - 1];
        Vec<MarkerNode*> nodes;
        markers->getMarkersByRect(r, nodes, specified_object_only);
        if (nodes.Size() == 0) {
            dm->textSelection->result.len--;
            return end;
        } else {
            for (auto m : nodes) {
                char* s = ToUtf8(begin, end - begin);
                if (!m->selected_words.Contains(s)) {
                    m->selected_words.Append(s);
                    if (markers != nullptr && m->keyword != nullptr && str::Eq(m->keyword, "Pin")) {
                        m->assoc_cells.Append(markers->getCellsInPage(pageNo));
                    }
                }
                str::Free(s);
            }
        }
    }
    /* */
    result.Append(begin, end - begin); // append 'word' to result.
    result.Append(lineSep, lineSep_len);
    return end;
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
        const WCHAR* w = ToWStrTemp(cmd.Get());
        DDEExecute(USERAPP_DDE_SERVICE, USERAPP_DDE_TOPIC, w);
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
            const WCHAR* w = ToWStrTemp(cmd.Get());
            DDEExecute(USERAPP_DDE_SERVICE, USERAPP_DDE_TOPIC, w);
        }
    }
}

// =============================================================
std::string escape_json(const char c) {
    std::string output = "";
    switch (c) {
        case '"':
            output = "\\\"";
            break;
        case '\\':
            output = "\\\\";
            break;
        case '\b':
            output = "\\b";
            break;
        case '\f':
            output = "\\f";
            break;
        case '\n':
            output = "\\n";
            break;
        case '\r':
            output = "\\r";
            break;
        case '\t':
            output = "\\t";
            break;
        // Add other special characters as needed
        default:
            output = c;
            break;
    }
    return output;
}
// =============================================================
std::string base64_encode(const BYTE* data, size_t len) {
    const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string result;
    int i = 0;
    int j = 0;
    BYTE char_array_3[3];
    BYTE char_array_4[4];

    while (len--) {
        char_array_3[i++] = *(data++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; (i < 4); i++) {
                result += base64_chars[char_array_4[i]];
            }
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 3; j++) {
            char_array_3[j] = '\0';
        }

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

        for (j = 0; (j < i + 1); j++) {
            result += base64_chars[char_array_4[j]];
        }

        while ((i++ < 3)) {
            result += '=';
        }
    }

    return result;
}
// =============================================================
//
// =============================================================
void SaveBlocksToFile(MainWindow* win, const char* fname) {
    DisplayModel* dm = win->AsFixed();
    WCHAR* tmpFileW = ToWStrTemp(fname);
    FILE* outFile = nullptr;
    errno_t err = _wfopen_s(&outFile, tmpFileW, L"wb");
    if (err != 0) {
        return;
    }
    size_t n = 0;
    std::fputs("[\n", outFile);
    TmpAllocator alloc;
    std::vector<std::pair<Vec<PageText*>, Vec<IPageElement*> > > blocks;
    getBlocks(win, blocks);
    size_t pageNo = 0;
    for (auto page : blocks) {
        pageNo++;
        for (IPageElement* pageEl : page.second) { // images) {
            Rect rect = pageEl->rect.Round();
            RenderedBitmap* bmp = dm->GetEngine()->GetImageForPageElement(pageEl);
            auto hbmp = bmp->GetBitmap();
            if (!hbmp) {
                continue;
            }
            auto imgData = SerializeBitmap(hbmp);
            size_t len = imgData.size();
            u8* data = imgData.data();
            auto base64 = base64_encode(data, len);
            if (0 < n) {
                std::fputs(",\n", outFile);
            }
            std::fprintf(outFile, "{\"page\" : %d,\n", pageNo);
            std::fprintf(outFile, "\"rect\" : [%d,%d,%d,%d],\n", rect.x, rect.y, rect.dx, rect.dy);
            std::fputs("\"image\" : \"", outFile);
            std::fputs(base64.c_str(), outFile);
            std::fputs("\"}", outFile);
            str::Free(data);
            n++;
        }
    }
    pageNo = 0;
    for (auto page : blocks) {
        pageNo++;
        for (PageText* b : page.first) {
            if (b->len == 0) {
                continue;
            }
            int x1 = b->coords[0].x;
            int y1 = b->coords[0].y;
            int x2 = x1 + b->coords[0].dx;
            int y2 = y1 + b->coords[0].dy;
            for (int i = 0; i < b->len; i++) {
                Rect r = b->coords[i];
                if (!r.IsEmpty()) {
                    x1 = r.x;
                    y1 = r.y;
                    x2 = x1 + r.dx;
                    y2 = y1 + r.dy;
                }
            }
            for (int i=0; i < b->len; i++) {
                auto r = b->coords[i];
                if (r.IsEmpty()) { continue; }
                if (r.x < x1) x1 = r.x;
                if (r.y < y1) y1 = r.y;
                if (x2 < r.x + r.dx) x2 = r.x + r.dx;
                if (y2 < r.y + r.dy) y2 = r.y + r.dy;
            }
            if (str::IsEmpty(b->text)) {
                continue;
            }
            char* w = strconv::WStrToUtf8(b->text, b->len, &alloc);
            if (0 < n) {
                std::fputs(",\n", outFile);
            }
            std::fprintf(outFile, "{\"page\" : %d,\n", pageNo);
            std::fprintf(outFile, "\"rect\" : [%d,%d,%d,%d],\n", x1, y1, x2 - x1, y2 - y1);
            std::fputs("\"en\" : \"", outFile);
            for (auto c = w; *c; c++) {
                std::fputs(escape_json(*c).c_str(), outFile);
            }
            std::fputs("\"}", outFile);
            delete b;
            n++;
        }
    }
    std::fputs("\n]\n", outFile);
    std::fclose(outFile);
}

// =============================================================
//
// =============================================================
void SaveWordsToFile(MainWindow* win, const char* fname) {
    StrVec word_vec;
    DisplayModel* dm = win->AsFixed();
    int pageCount = dm->PageCount();
    TmpAllocator alloc;
    for (int pageNo = 1; pageNo <= pageCount; pageNo++) {
        Rect* coords;
        const WCHAR* pageText = dm->textCache->GetTextForPage(pageNo, nullptr, &coords);
        if (str::IsEmpty(pageText)) {
            continue;
        }
        for (const WCHAR* src = pageText; *src;) {
            if (*src == '\n') {
                src++;
                continue;
            }
            if (!isWordChar(*src)) {
                src++;
                continue;
            }
            // forword search the end letter of 'word'.
            const WCHAR* begin = src;
            Rect rect = coords[begin - pageText];
            const WCHAR* end = src;
            for (; *end; ++end) {
                if (!isWordChar(*end)) {
                    break;
                }
                if (gGlobalPrefs->printableCharAsWordChar) {
                    Rect r = coords[end - pageText];
                    if (r.x != rect.x && r.y != rect.y) {
                        break;
                    }
                }
            }
            char* w = strconv::WStrToUtf8(begin, end - begin, &alloc);
            //char* w = ToUtf8(begin, end - begin);
            word_vec.Append(w);
            //str::Free(w);
            src = end;
        }
    }
    Sort(word_vec);
    //
    StrVec words;
    char* prev = nullptr;
    for (auto sel : word_vec) {
        if (prev == nullptr || !str::Eq(prev, sel)) {
            words.Append(sel);
            prev = sel;
        }
    }
    //
    FILE* outFile = nullptr;
    WCHAR* tmpFileW = ToWStrTemp(fname);
    errno_t err = _wfopen_s(&outFile, tmpFileW, L"wb");
    if (err == 0) {
        for (auto sel : words) {
            std::fputs(sel, outFile);
            std::fputs("\n", outFile);
        }
        std::fclose(outFile);
    }
}

// =============================================================
//
// =============================================================
void SaveTextToFile(MainWindow* win, const char* fname) {
    DisplayModel* dm = win->AsFixed();
    int pageCount = dm->PageCount();
    str::WStr result;
    for (int pageNo = 1; pageNo <= pageCount; pageNo++) {
        const WCHAR* pageText = dm->textCache->GetTextForPage(pageNo);
        if (str::IsEmpty(pageText)) {
            continue;
        }
        result.Append(pageText, str::Len(pageText));
    }
    char* text = ToUtf8(result.Get());
    FILE* outFile = nullptr;
    WCHAR* tmpFileW = ToWStrTemp(fname);
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
const char* base_MarkWords(MainWindow* win, const char* save_as=nullptr) {
    WindowTab* tab = win->CurrentTab();
    DisplayModel* dm = tab->AsFixed();
    auto engine = dm->GetEngine();
    // ---------------------------------------------
    std::vector<WordBlock*> word_blocks;
    const char* sep = "\r\n";
    // ---------------------------------------------
    //StrVec markedWords;
    dm->textSearch->wordSearch = true;
    char* first_word = nullptr;
    for (auto marker_node : tab->markers->markerTable) {
        str::Str annot_key_content("@CPSLabMark:");
        const char* keyword = marker_node->keyword.Get();
        auto word_block = new WordBlock(keyword);
        word_blocks.push_back(word_block);
        // -------------------------------------
        annot_key_content.Append(keyword);
        annot_key_content.AppendChar('@');
        // -- ClearSearchResult ------------
        DeleteOldSelectionInfo(win, true);
        RepaintAsync(win, 0);
        // - Select all words in PDF file -----------------------
        dm->textSearch->SetDirection(TextSearchDirection::Forward);
        bool conti = false;
        for (auto word : marker_node->words) {
            const WCHAR* wsep = strconv::Utf8ToWStr(word);
            // TextSel* sel = dm->textSearch->FindFirst(1, strconv::Utf8ToWStr(word), nullptr, conti);
            TextSel* sel = dm->textSearch->FindFirst(1, wsep, nullptr, conti);
            if (!sel) {
                str::Free(wsep);
                continue;
            }
            // if (!markedWords.Contains(word)) { markedWords.Append(word); }
            auto pages = word_block->add(wsep);
            if (first_word == nullptr) {
                first_word = word;
            }
            do {
                for (int ixi = 0; ixi < sel->len; ixi++) {
                    pages->push_back(sel->pages[ixi]);
                    marker_node->pages.Append(sel->pages[ixi]);
                    marker_node->mark_words.Append(word);
                }
                dm->textSelection->CopySelection(dm->textSearch, conti);
                UpdateTextSelection(win, false);
                conti = true;
                sel = dm->textSearch->FindNext(nullptr, conti);
            } while (sel);
            str::Free(wsep);
        }
        // -- Create 'Annotation' for each page. -------------
        Vec<SelectionOnPage>* selections = tab->selectionOnPage;
        if (selections != nullptr) {
            Vec<int> pageNos;   // page number list where selected words.
            for (auto& sel : *selections) {
                int pageno = sel.pageNo;
                if (!dm->ValidPageNo(pageno)) {
                    continue;
                }
                bool fo = false;
                for (auto n : pageNos) {
                    if (n == pageno) {
                        fo = true;
                        break;  // 'pageno' has been isted in 'pageNos'.
                    }
                }
                if (!fo) {
                    pageNos.Append(pageno);
                }
            }
            Vec<RectF> rects;   // rectangle for selected words.
            for (auto pageno : pageNos) {
                // -- create annotation ----------------------------
                rects.Reset();
                for (auto& sel : *selections) {
                    if (pageno != sel.pageNo) {
                        continue;
                    }
                    rects.Append(sel.rect);
                }
                Annotation* annot = EngineMupdfCreateAnnotation(engine, AnnotationType::Highlight, pageno, PointF{});
                SetQuadPointsAsRect(annot, rects);
                SetColor(annot, marker_node->mark_color); // Acua
                SetContents(annot, annot_key_content.Get());
                marker_node->annotations.Append(annot);
            }
            tab->askedToSaveAnnotations = true;
            DeleteOldSelectionInfo(win, true);
        }
    }
    // ---------------------------------------------
    if (save_as != nullptr) {
        WCHAR* tmpFileW = ToWStrTemp(save_as);
        FILE* outFile = nullptr;
        errno_t err = _wfopen_s(&outFile, tmpFileW, L"wb");
        if (err == 0) {
            int kcount = 0;
            std::fputs("{\n", outFile);
            for (auto bk : word_blocks) {
                if (kcount != 0) {
                    std::fprintf(outFile, ",\n");
                }
                auto keyword = bk->keyword();
                std::fprintf(outFile, "  \"%s\" : {\n", keyword);       // Net
                int wcount = 0;
                for (auto w : *bk->words()) {
                    if (wcount != 0) {
                        std::fprintf(outFile, ",\n");
                    }
                    std::fprintf(outFile, "    \"%ls\" : [", w.first.c_str());
                    auto pages = w.second;
                    if (2 <= pages->size()) {
                        std::sort(pages->begin(), pages->end());
                        pages->erase(std::unique(pages->begin(), pages->end()), pages->end());
                    }
                    for (size_t i = 0; i < pages->size(); i++) {
                        if (i != 0) {
                            std::fprintf(outFile, ", ");
                        }
                        std::fprintf(outFile, "%d", pages->at(i));
                    }
                    std::fprintf(outFile, "]");
                    wcount += 1;
                }
                std::fprintf(outFile, "\n  }");
                kcount += 1;
            }
            std::fputs("n}\n", outFile);
        }
        std::fclose(outFile);
    }
    // ---------------------------------------------
    for (auto wb : word_blocks) { delete wb; }
    // ---------------------------------------------
    dm->textSearch->wordSearch = false;
    // SetSelectedWordToFindEdit(win, markedWords);
    return first_word;
}

// =============================================================
//
// =============================================================
const char* MarkWords(MainWindow* win) {
    return base_MarkWords(win);
}
// =============================================================
//
// =============================================================
const char* MarkWords(MainWindow* win, const char* json_file) {
    WindowTab* tab = win->CurrentTab();
    tab->markers->deleteAnnotations();
    tab->markers->parse(json_file);
    if (json_file != nullptr) {
        char drive[_MAX_DRIVE];
        char dir[_MAX_DIR];
        char fname[_MAX_FNAME];
        char ext[_MAX_EXT];
        _splitpath(json_file, drive, dir, fname, ext);
        std::string path = std::string(drive) + std::string(dir) + "reply_" + std::string(fname) + std::string(ext);
        return base_MarkWords(win, path.c_str());
        //return base_MarkWords(win);
    } else {
        return base_MarkWords(win);
    }
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
    return base_MarkWords(win);
}


// =============================================================
//
// =============================================================
char* GetTextInRegion(const DisplayModel* dm, int pageNo, const Rect regionI, const char* lineSep) {
    Rect* coords;
    const WCHAR* pageText = dm->textCache->GetTextForPage(pageNo, nullptr, &coords);
    if (str::IsEmpty(pageText)) {
        return nullptr;
    }
    const WCHAR* wsep = strconv::Utf8ToWStr(lineSep);
    int wsep_len = str::Len(wsep);
    str::WStr result;
    const WCHAR* begin = nullptr;
    for (const WCHAR* src = pageText; *src; ) {
        if (begin == nullptr) {
            begin = src;
        }
        Rect rect = coords[src - pageText]; // boundary rectangle of this 'letter'.
        Rect isect = regionI.Intersect(rect);
        if (isect.IsEmpty() || 1.0 * isect.dx * isect.dy / (rect.dx * rect.dy) < 0.3) {
            if (begin < src) {
                result.Append(begin, src - begin); // append 'word' to result.
                result.Append(wsep, wsep_len);
                Rect r = coords[begin - pageText]; // boundary rectangle of this 'letter'.
                int px = r.x + r.dx / 2.0;
                int py = r.y + r.dy / 2.0;
                dm->textSelection->StartAt(pageNo, px, py);
                r = coords[src - pageText - 1];
                if (r.IsEmpty()) {
                    // Rect is empty when *src is 'return' code.
                    r = coords[src - pageText - 2];
                }
                px = r.x + r.dx;
                py = r.y + r.dy / 2.0;
                dm->textSelection->SelectUpTo(pageNo, px, py, !result.IsEmpty());
            }
            ++src;
            begin = nullptr;
        } else {
            while (*src && *src != '\n' && !isspace(*src)) { ++src; }
            if (*src) {
                ++src;  // skip white space.
            } else if (begin != nullptr) {
                result.Append(begin, src - begin); // append 'word' to result.
                result.Append(wsep, wsep_len);
                Rect r = coords[begin - pageText]; // boundary rectangle of this 'letter'.
                int px = r.x + r.dx / 2.0;
                int py = r.y + r.dy / 2.0;
                dm->textSelection->StartAt(pageNo, px, py);
                r = coords[src - pageText - 1];
                if (r.IsEmpty()) {
                    // Rect is empty when *src is 'return' code.
                    r = coords[src - pageText - 2];
                }
                px = r.x + r.dx;
                py = r.y + r.dy / 2.0;
                dm->textSelection->SelectUpTo(pageNo, px, py, !result.IsEmpty());
                break;
            }
        }
    }
    str::Free(wsep);
    WCHAR* ws = result.Get();
    return ToUtf8(ws);
}
// =============================================================
//
// =============================================================
char* GetWordsInRegion(const DisplayModel* dm, int pageNo, const Rect regionI, const char* lineSep, Markers* markers) {
    Rect* coords;
    const WCHAR* pageText = dm->textCache->GetTextForPage(pageNo, nullptr, &coords);
    if (str::IsEmpty(pageText)) {
        return nullptr;
    }
    const WCHAR* wsep = strconv::Utf8ToWStr(lineSep);
    str::WStr result;
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
        src = SelectWordAt(dm, pageNo, pageText, coords, src, wsep, result, markers, true);
    }
    str::Free(wsep);
    WCHAR* ws = result.Get();
    return ToUtf8(ws);
}

// =============================================================
//
// =============================================================
char* GetWordsInCircle(const DisplayModel* dm, int pageNo, const Rect regionI, const char* lineSep, Markers* markers) {
    Rect* coords;
    const WCHAR* pageText = dm->textCache->GetTextForPage(pageNo, nullptr, &coords);
    if (str::IsEmpty(pageText)) {
        return nullptr;
    }
    const WCHAR* wsep = strconv::Utf8ToWStr(lineSep);
    str::WStr result;
    int radius = (regionI.dx < regionI.dy ?  regionI.dy : regionI.dx) / 2;
    float sqrr = pow(radius, 2);
    int cx = regionI.x + regionI.dx / 2;
    int cy = regionI.y + regionI.dy / 2;
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
        src = SelectWordAt(dm, pageNo, pageText, coords, src, wsep, result, markers, true);
    }
    str::Free(wsep);
    WCHAR* ws = result.Get();
    return ToUtf8(ws);
}

// =============================================================
//
// =============================================================
void sendClickPoint(MainWindow* win, int x, int y) {
    if (USERAPP_DDE_SERVICE == nullptr || USERAPP_DDE_TOPIC == nullptr) {
        return;
    }
    WindowTab* tab = win->CurrentTab();
    if (tab->selectionOnPage) {
        return;
    }
    DisplayModel* dm = win->AsFixed();
    Point mousePos = Point(x, y);
    IPageElement* pageEl = dm->GetElementAtPos(mousePos, nullptr);
    if (!pageEl) {
        return;
    }
    Rect rect = pageEl->rect.Round();
    if (rect.IsEmpty()) {
        return;
    }
    x = int(rect.x + rect.dx / 2.0);
    y = int(rect.y + rect.dy / 2.0);
    int pageNo = dm->GetPageNoByPoint(mousePos);
    //PointF pos = dm->CvtFromScreen(mousePos, pageNo);
    //Point pt = ToPoint(pos);
    str::Str cmd;
    //cmd.AppendFmt("[Clicked(\"%s\", %d, %d, %d)]", tab->filePath.Get(), pageNo, pt.x, pt.y);
    cmd.AppendFmt("[Clicked(\"%s\", %d, %d, %d)]", tab->filePath.Get(), pageNo, x, y);
    DDEExecute(USERAPP_DDE_SERVICE, USERAPP_DDE_TOPIC, ToWStrTemp(cmd.Get()));
}

// =============================================================
//
// =============================================================
void sendSelectText(MainWindow* win, bool conti) {
    if (USERAPP_DDE_SERVICE == nullptr || USERAPP_DDE_TOPIC == nullptr) {
        return;
    }

    const char* sep = "\r\n";
    WindowTab* tab = win->CurrentTab();
    if (!tab->selectionOnPage) {
        return;
    }
    if (tab->selectionOnPage->size() == 0) {
        return;
    }

    DisplayModel* dm = win->AsFixed();
    if (dm->GetEngine()->IsImageCollection()) {
        return;
    }

    int pageNo = 0;
    Rect rect;
    str::Str text;
    for (SelectionOnPage& sel : *tab->selectionOnPage) {
        Rect regionI = sel.rect.Round();
        if (0 < dm->textSelection->result.len) {
            pageNo = dm->textSelection->startPage;
            Rect* coords;
            dm->textCache->GetTextForPage(pageNo, nullptr, &coords);
            int x1 = coords[dm->textSelection->startGlyph].x;
            int y1 = coords[dm->textSelection->startGlyph].y;
            int x2 = x1 + coords[dm->textSelection->startGlyph].dx;
            int y2 = y1 + coords[dm->textSelection->startGlyph].dy;
            for (auto i = dm->textSelection->startGlyph; i <= dm->textSelection->endGlyph; i++) {
                auto r = coords[i];
                if (r.IsEmpty()) { continue; }
                if (r.x < x1) x1 = r.x;
                if (r.y < y1) y1 = r.y;
                if (x2 < r.x + r.dx) x2 = r.x + r.dx;
                if (y2 < r.y + r.dy) y2 = r.y + r.dy;
            }
            rect.x = x1;
            rect.y = y1;
            rect.dx = x2 - x1;
            rect.dy = y2 - y1;
            WCHAR* s = dm->textSelection->ExtractText(sep);
            char* utf8txt = ToUtf8(s);
            str::Free(s);
            if (!str::IsEmpty(utf8txt)) {
                text.Append(utf8txt);
                str::Free(utf8txt);
                break;
            }
        } else {
            pageNo = sel.pageNo;
            char* utf8txt = GetTextInRegion(dm, sel.pageNo, regionI, sep);
            if (!str::IsEmpty(utf8txt)) {
                text.Append(utf8txt);
                str::Free(utf8txt);
                rect.x = regionI.x;
                rect.y = regionI.y;
                rect.dx = regionI.dx;
                rect.dy = regionI.dy;
                break;
            }
        }
    }
    UpdateTextSelection(win, false);

    if (0 < text.size()) {
        str::Str cmd;
        cmd.AppendFmt("[Select(\"%s\", \"%s\", %d, %d, %d, %d, %d)]",
            tab->filePath.Get(), text.Get(),
            pageNo,
            rect.x, rect.y, rect.dx, rect.dy
        );
        DDEExecute(USERAPP_DDE_SERVICE, USERAPP_DDE_TOPIC, ToWStrTemp(cmd.Get()));
    }
}


// =============================================================
//
// =============================================================
void sendSelectImage(MainWindow* win, int x, int y, bool conti) {
    if (USERAPP_DDE_SERVICE == nullptr || USERAPP_DDE_TOPIC == nullptr) {
        return;
    }
    WindowTab* tab = win->CurrentTab();
    if (!tab->selectionOnPage) {
        return;
    }
    if (tab->selectionOnPage->size() == 0) {
        return;
    }
    /* if (dm->GetEngine()->IsImageCollection()) { return; } */
    DisplayModel* dm = win->AsFixed();
    Point cursorPos{x, y};
    IPageElement* pageEl = dm->GetElementAtPos(cursorPos, nullptr);
    if (!pageEl) {
        return;
    }
    RenderedBitmap* bmp = dm->GetEngine()->GetImageForPageElement(pageEl);
    if (bmp) {
        str::Str cmd;
        CopyImageToClipboard(bmp->GetBitmap(), false);
        Rect r = pageEl->rect.Round();
        cmd.AppendFmt("[PasteFromClipBoard(\"%s\", %d, %d, %d, %d, %d)]",
            tab->filePath.Get(),
            pageEl->pageNo, r.x, r.y, r.dx, r.dy);
        DDEExecute(USERAPP_DDE_SERVICE, USERAPP_DDE_TOPIC, ToWStrTemp(cmd.Get()));
    }
}



// =============================================================
//
// =============================================================
void getPageBlocks(MainWindow* win, int pageNo, Vec<PageText*>& blocks, Vec<IPageElement*>& images) {
    DisplayModel* dm = win->AsFixed();
    auto engine = dm->GetEngine();
    engine->ExtractPageBlocks(pageNo, blocks, images);
}

void getBlocks(MainWindow* win, std::vector< std::pair<Vec<PageText*>, Vec<IPageElement*> > >& blocks) {
    DisplayModel* dm = win->AsFixed();
    int pageCount = dm->PageCount();
    for (int pageNo = 1; pageNo <= pageCount; pageNo++) {
        Vec<PageText*> texts;
        Vec<IPageElement*> images;
        getPageBlocks(win, pageNo, texts, images);
        auto page = std::make_pair(texts, images);
        blocks.push_back(page);
    }
}

} // end of namespace cpslab


