/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

struct DocumentTextCache {
    EngineBase* engine = nullptr;
    int nPages = 0;
    PageText* pagesText = nullptr;
    int debugSize = 0;

    CRITICAL_SECTION access;

    explicit DocumentTextCache(EngineBase* engine);
    ~DocumentTextCache();

    bool HasTextForPage(int pageNo) const;
    const WCHAR* GetTextForPage(int pageNo, int* lenOut = nullptr, Rect** coordsOut = nullptr);
};

// TODO: replace with Vec<TextSel>
struct TextSel {
    int len = 0;
    int cap = 0;
    int* pages = nullptr;
    Rect* rects = nullptr;
};

struct TextSelection {
    int startPage = -1;
    int endPage = -1;
    int startGlyph = -1;
    int endGlyph = -1;

    EngineBase* engine = nullptr;
    DocumentTextCache* textCache = nullptr;

    TextSelection(EngineBase* engine, DocumentTextCache* textCache);
    ~TextSelection();

    bool IsOverGlyph(int pageNo, double x, double y);
    void StartAt(int pageNo, int glyphIx);
    void StartAt(int pageNo, double x, double y);
    void SelectUpTo(int pageNo, int glyphIx, bool conti=false);
    void SelectUpTo(int pageNo, double x, double y, bool conti=false);
    void SelectWordAt(int pageNo, double x, double y, bool conti=false);
    void CopySelection(TextSelection* orig, bool conti=false);
    WCHAR* ExtractText(const char* lineSep);
    WCHAR* ExtractText(const char* lineSep, const int pageNo);
    void Reset();

    TextSel result{};

    void GetGlyphRange(int* fromPage, int* fromGlyph, int* toPage, int* toGlyph) const;
};

uint distSq(int x, int y);
bool isWordChar(WCHAR c);
