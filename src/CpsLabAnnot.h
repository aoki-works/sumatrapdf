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
    AutoFreeStr keyword;
    PdfColor mark_color;
    PdfColor select_color;
    StrVec words;
    StrVec selected_words;
    Vec<Annotation*> annotations;
    Vec<Rect> rects;

  public:
    MarkerNode(WindowTab* tab);
    ~MarkerNode();

  public:
    const char* selectWords(MainWindow* win, StrVec& words, bool conti=false);
};


class Markers
{
  private:
    WindowTab*  tab_;

  public:
    Vec<MarkerNode*> markerTable;

  public:
    Markers(WindowTab* tab);
    ~Markers();
  public:
    void sendSelectMessage(MainWindow* win);
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
    size_t getMarkersByRect(Rect& r, Vec<MarkerNode*>& result);
};

} // namespace cpslab
