/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

struct TextSearch : public TextSelection {
    enum class Direction : bool {
        Backward = false,
        Forward = true
    };

    TextSearch(EngineBase* engine, DocumentTextCache* textCache);
    ~TextSearch();

    bool GetSensitive() const; // CPS Lab: returns matchCase
    void SetMatchCase(bool sensitive);
    void SetDirection(Direction direction);
    void SetLastResult(TextSelection* sel);
    TextSel* FindFirst(int page, const WCHAR* text, bool conti = false); // CPS Lab: conti
    TextSel* FindNext(bool conti = false, bool only_in_page = false);    // CPS Lab
    bool wordSearch = false;                                             // CPS Lab

    int GetCurrentPageNo() const;
    int GetSearchHitStartPageNo() const;

    ProgressUpdateCb progressCb;

    // Lightweight container for page and offset within the page to use as return value of MatchEnd
    struct PageAndOffset {
        int page;
        int offset;
    };

    Vec<const WCHAR*> findWords; // CPS lab.
    WCHAR* findText = nullptr;
    WCHAR* anchor = nullptr;
    int findPage = 0;
    int searchHitStartAt = 0; // when text found spans several pages, searchHitStartAt < findPage
    bool forward = true;
    bool matchCase = false;
    // these two options are implicitly set when the search text begins
    // resp. ends in a single space (many users already search that way),
    // combining them yields a 'Whole words' search
    bool matchWordStart = false;
    bool matchWordEnd = false;

    void SetText(const WCHAR* text);
    bool FindTextInPage(int pageNo, PageAndOffset* finalGlyph, bool conti = false); // CPS Lab: conti
    bool FindStartingAtPage(int pageNo, bool conti = false);                        // CPS Lab: conti
    PageAndOffset MatchEnd(const WCHAR* start) const;
    PageAndOffset wMatchEnd(const WCHAR* start) const; // CPS Lab

    void Clear();
    void Reset();

    const WCHAR* pageText = nullptr;
    Rect* pageRects = nullptr; // CPS Lab
    int findIndex = 0;

    WCHAR* lastText = nullptr;
    int nPages = 0;
    Vec<bool> pagesToSkip;
};
