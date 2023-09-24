/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

struct MainWindow;
struct WindowTab;
struct Annotation;
struct TextSelection;
struct Rect;

namespace cpslab {

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
    void sendMessage(MainWindow* win);
    void parse(const char* fname);
  public:
    void deleteAnnotations();
    MarkerNode* getMarker(const char* keyword);
    size_t getMarkersByWord(const WCHAR* word, Vec<MarkerNode*>& result);
    size_t getMarkersByWord(const char* word, Vec<MarkerNode*>& result);
    size_t getMarkersByTS(TextSelection* ts, Vec<MarkerNode*>& result);
    size_t getMarkersByRect(Rect& r, Vec<MarkerNode*>& result);
};

} // namespace cpslab
