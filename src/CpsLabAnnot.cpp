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
    : tab_(tab), filePath_(), keyword(), mark_color(0xff00ffff), select_color(0xff00ffff), words(), annotations() {
    mark_color = 0xff00ffff;
    select_color = 0xff0000ff;
}

MarkerNode::~MarkerNode() {
    for (auto a : annotations) {
        DeleteAnnotation(a);
    }
}

const char* MarkerNode::selectWords(MainWindow* win, StrVec& select_words, bool conti) {
    char* first_word = nullptr;
    DisplayModel* dm = win->AsFixed();
    dm->textSearch->SetDirection(TextSearchDirection::Forward);
    dm->textSearch->wordSearch = true;
    for(auto wd : select_words) {
        for (size_t i = 0; i < words.size(); ++i) {
            char* mkwd = words.at(i);
            if (!str::Eq(wd, mkwd)) {
                continue;
            }
            const WCHAR* wsep = strconv::Utf8ToWstr(wd);
            TextSel* sel = dm->textSearch->FindFirst(1, strconv::Utf8ToWstr(wd), nullptr, conti);
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
    }
    dm->textSearch->wordSearch = false;
    return first_word;
}

// =============================================================
//
// =============================================================
Markers::Markers(WindowTab* tab) {
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
    for (auto m : markerTable) { m->selected_words.Reset(); }

    //StrVec str_vec;
    for (SelectionOnPage& sel : *tab_->selectionOnPage) {
        char* text;
        Rect regionI = sel.rect.Round();

        bool isTextOnlySelectionOut = dm->textSelection->result.len > 0;
        if (isTextOnlySelectionOut) {
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
                    }
                }
            }
            str::Free(s);
        } else {
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

    StrVec selected_words;
    for (auto m : markerTable) {
        if (m->selected_words.Size() == 0) {
            continue;
        }
        for (auto s : m->selected_words) {
            if (!selected_words.Contains(s)) {
                selected_words.Append(s);
            }
        }
    }
    if (0 < selected_words.Size()) {
        str::Str cmd;
        if (conti) {
            cmd.AppendFmt("[CSelect(\"%s\"", tab_->filePath.Get());
        } else {
            cmd.AppendFmt("[Select(\"%s\"", tab_->filePath.Get());
        }
        for (int i = 0; i < selected_words.Size(); i++) {
            auto s = selected_words.at(i);
            cmd.AppendFmt(", \"%s\"", s);
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
    bool conti = false;
    for (auto node : markerTable) {
        if (node->selectWords(win, words, conti) != nullptr) {
            conti = true;
        }
    }
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
            char* w = strconv::WstrToUtf8(begin, end - begin, &alloc);
            //char* w = ToUtf8(begin, end - begin);
            word_vec.Append(w);
            //str::Free(w);
            src = end;
        }
    }
    word_vec.Sort();
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
        // ---------------------------------
        dm->textSearch->SetDirection(TextSearchDirection::Forward);
        bool conti = false;
        for (auto word : marker_node->words) {
            const WCHAR* wsep = strconv::Utf8ToWstr(word);
            TextSel* sel = dm->textSearch->FindFirst(1, strconv::Utf8ToWstr(word), nullptr, conti);
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
                    marker_node->rects.Append(sel->rects[ixi]);
                    pages->push_back(sel->pages[ixi]);
                }
                dm->textSelection->CopySelection(dm->textSearch, conti);
                UpdateTextSelection(win, false);
                conti = true;
                sel = dm->textSearch->FindNext(nullptr, conti);
            } while (sel);
            str::Free(wsep);
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
            Vec<RectF> rects;
            for (auto pno : pageNos) {
                rects.Reset();
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
char* GetWordsInRegion(const DisplayModel* dm, int pageNo, const Rect regionI, const char* lineSep, Markers* markers) {
    Rect* coords;
    const WCHAR* pageText = dm->textCache->GetTextForPage(pageNo, nullptr, &coords);
    if (str::IsEmpty(pageText)) {
        return nullptr;
    }
    const WCHAR* wsep = strconv::Utf8ToWstr(lineSep);
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
    const WCHAR* wsep = strconv::Utf8ToWstr(lineSep);
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



} // end of namespace cpslab


