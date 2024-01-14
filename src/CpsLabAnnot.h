/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

struct MainWindow;
struct WindowTab;
struct Annotation;
struct DisplayModel;
struct TextSelection;
struct TextSel;
struct Rect;

namespace cpslab {

class Markers;

extern WCHAR* USERAPP_DDE_SERVICE;
extern WCHAR* USERAPP_DDE_TOPIC;
extern WCHAR* USERAPP_DDE_DEBUG_TOPIC;
extern WCHAR* PDFSYNC_DDE_SERVICE;
extern WCHAR* PDFSYNC_DDE_TOPIC;

extern void SaveWordsToFile(MainWindow* win, const char* fname);
extern void SaveTextToFile(MainWindow* win, const char* fname);
extern bool IsWord(const WCHAR* pageText, const Rect* coords, const WCHAR* begin, const WCHAR* end);
extern const char* MarkWords(MainWindow* win);
extern const char* MarkWords(MainWindow* win, const char* json_file);
extern const char* MarkWords(MainWindow* win, StrVec& words);
extern void CloseEvent(WindowTab* tab);
extern void CloseEvent(MainWindow* win);
extern char* GetWordsInCircle(const DisplayModel* dm, int pageNo, const Rect regionI, const char* lineSep="\r\n", Markers* mk=nullptr);
extern char* GetWordsInRegion(const DisplayModel* dm, int pageNo, const Rect regionI, const char* lineSep="\r\n", Markers* mk=nullptr);

struct TmpAllocator : Allocator {
    void* p;
    size_t len;
    TmpAllocator();
    ~TmpAllocator();
    void* Alloc(size_t size);
    void* Realloc(void* men, size_t size);
    void Free(const void*);
};

class MarkerNode
{
  private:
    WindowTab*  tab_;
    AutoFreeStr filePath_;

  public:
    AutoFreeStr keyword;        // [Cell|Net|Pin| etc..]
    PdfColor mark_color;        // marker color
    PdfColor select_color;      // selector color
    StrVec words;               // all marker words
    Vec<Annotation*> annotations;   // marker annotations
    Vec<char*> mark_words;         // mark wores in annotations
    Vec<Rect> rects;                // marker rectangles.
    Vec<int> pages;                 // marker pages.

  public:   // working area
    StrVec selected_words;      // selected_words
    Vec<const char*> assoc_cells;    // Cell that associated with the selected pin.

  public:
    MarkerNode(WindowTab* tab);
    ~MarkerNode();

  public:
    const char* selectWord(MainWindow* win, const int pageNo, char* word, bool conti=false);
    const char* selectWords(MainWindow* win, StrVec& words, bool conti=false);
    size_t getMarkWordsByPageNo(const int pageNo, StrVec& result);
    int getPage(const char* word, const int pageNo=-1);
    bool tExist(const int pageNo, const char* word);
};

struct PageInCell
{
    int pageNo;
    str::Str cells;
};

class Markers
{
  private:
    WindowTab*  tab_;
    uint select_;
    Vec<PageInCell> page_in_cell_;

  public:
    Vec<MarkerNode*> markerTable;

  public:
    Markers(WindowTab* tab);
    ~Markers();
  public:
    void sendSelectMessage(MainWindow* win, bool conti=false);
    void parse(const char* fname);
  public:
    void selectWords(MainWindow* win, const char* keyword, StrVec& words);
    void selectWords(MainWindow* win, StrVec& words);
  public:
    void deleteAnnotations();
    MarkerNode* getMarker(const char* keyword);
    size_t getMarkersByWord(const WCHAR* word, Vec<MarkerNode*>& result);
    size_t getMarkersByWord(const char* word, Vec<MarkerNode*>& result);
    size_t getMarkersByTS(TextSelection* ts, Vec<MarkerNode*>& result);
    size_t getMarkersByRect(Rect& r, Vec<MarkerNode*>& result, bool specified_object_only=false);
    void setSelection(const char* keyword);
    void unsetSelection(const char* keyword);
    bool isSelection(const char* keyword);
    const char* getCellsInPage(const int pageNo);
};

} // namespace cpslab
