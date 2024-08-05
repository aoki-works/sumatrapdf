/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "utils/BaseUtil.h"
#include "utils/ScopedWin.h"
#include "utils/WinUtil.h"
#include "utils/Timer.h"
#include "utils/ThreadUtil.h"

#include "wingui/UIModels.h"

#include "Settings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "EngineAll.h"
#include "DisplayModel.h"
#include "GlobalPrefs.h"
#include "RenderCache.h"
#include "TextSelection.h"

#define NO_LOG
#include "utils/Log.h"

#pragma warning(disable : 28159) // silence /analyze: Consider using 'GetTickCount64' instead of 'GetTickCount'

// TODO: remove this and always conserve memory?
/* Define if you want to conserve memory by always freeing cached bitmaps
for pages not visible. Disabling this might lead to pages not rendering
due to insufficient (GDI) memory. */
// #define CONSERVE_MEMORY

bool gShowTileLayout = false;

static void RenderCacheThread(RenderCache* cache) {
    PageRenderRequest* req;
    RenderedBitmap* bmp;

    for (;;) {
        if (cache->ClearCurrentRequest()) {
            DWORD waitResult = WaitForSingleObject(cache->startRendering, INFINITE);
            // Is it not a page render request?
            if (WAIT_OBJECT_0 != waitResult) {
                continue;
            }
        }

        req = cache->GetNextRequest();
        if (!req) {
            continue;
        }

        auto dm = req->dm;
        if (!dm->PageVisibleNearby(req->pageNo) && !req->renderCb) {
            continue;
        }

        if (dm->dontRenderFlag) {
            if (req->renderCb) {
                req->renderCb->Call(nullptr);
            }
            continue;
        }

        // make sure that we have extracted page text for
        // all rendered pages to allow text selection and
        // searching without any further delays
        if (!dm->textCache->HasTextForPage(req->pageNo)) {
            dm->textCache->GetTextForPage(req->pageNo);
        }

        ReportIf(req->abortCookie != nullptr);
        EngineBase* engine = dm->GetEngine();
        engine->AddRef();
        RenderPageArgs args(req->pageNo, req->zoom, req->rotation, &req->pageRect, RenderTarget::View,
                            &req->abortCookie);
        auto timeStart = TimeGet();
        logf("RenderCache: calling engine->RenderPage() page: %d, page rect: (%d, %d) (%d, %d)\n", req->pageNo,
             (int)req->pageRect.x, (int)req->pageRect.y, (int)req->pageRect.dx, (int)req->pageRect.dy);
        bmp = engine->RenderPage(args);
        if (req->abort) {
            delete bmp;
            if (req->renderCb) {
                req->renderCb->Call(nullptr);
            }
            engine->Release();
            continue;
        }
        auto durMs = TimeSinceInMs(timeStart);
        if (durMs > 100) {
            auto path = engine->FilePath();
            logfa("Slow rendering: %.2f ms, page: %d in '%s'\n", (float)durMs, req->pageNo, path);
        }

        logf("RenderCache: finished rendering page: %d, page rect: (%d, %d) (%d, %d) in %.2f\n", req->pageNo,
             (int)req->pageRect.x, (int)req->pageRect.y, (int)req->pageRect.dx, (int)req->pageRect.dy, durMs);
        if (req->renderCb) {
            // the callback must free the RenderedBitmap
            req->renderCb->Call(bmp);
            req->renderCb = (OnBitmapRendered*)1; // will crash if accessed again, which should not happen
        } else {
            // don't replace colors for individual images
            if (bmp && !engine->IsImageCollection()) {
                UpdateBitmapColors(bmp->GetBitmap(), cache->textColor, cache->backgroundColor);
            }
            cache->Add(req, bmp);
            dm->RepaintDisplay();
        }
        engine->Release();
        ResetTempAllocator();
    }
    DestroyTempAllocator();
}

RenderCache::RenderCache() {
    // enable when debugging RenderCache logic
    // gEnableDbgLog = true;
    int screenDx = GetSystemMetrics(SM_CXSCREEN);
    int screenDy = GetSystemMetrics(SM_CYSCREEN);
    maxTileSize = {screenDx, screenDy};
    isRemoteSession = GetSystemMetrics(SM_REMOTESESSION);
    textColor = WIN_COL_BLACK;
    backgroundColor = WIN_COL_WHITE;

    InitializeCriticalSection(&cacheAccess);
    InitializeCriticalSection(&requestAccess);

    startRendering = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    auto fn = MkFunc0(RenderCacheThread, this);
    renderThread = StartThread(fn, "RenderCacheThread");
    ReportIf(nullptr == renderThread);
}

RenderCache::~RenderCache() {
    EnterCriticalSection(&requestAccess);
    EnterCriticalSection(&cacheAccess);

    SafeCloseHandle(&renderThread);
    SafeCloseHandle(&startRendering);
    if (curReq || firstRequest || cacheCount != 0) {
        logf("RenderCache::~RenderCache: curReq: 0x%p, cacheCount: %d\n", curReq, cacheCount);
        ReportIf(true);
    }

    LeaveCriticalSection(&cacheAccess);
    DeleteCriticalSection(&cacheAccess);
    LeaveCriticalSection(&requestAccess);
    DeleteCriticalSection(&requestAccess);
}

/* Find a bitmap for a page defined by <dm> and <pageNo> and optionally also
   <rotation> and <zoom> in the cache - call DropCacheEntry when you
   no longer need a found entry. */
BitmapCacheEntry* RenderCache::Find(DisplayModel* dm, int pageNo, int rotation, float zoom, TilePosition* tile) {
    ScopedCritSec scope(&cacheAccess);
    rotation = NormalizeRotation(rotation);
    for (int i = 0; i < cacheCount; i++) {
        BitmapCacheEntry* e = cache[i];
        if ((dm == e->dm) && (pageNo == e->pageNo) && (rotation == e->rotation) &&
            (kInvalidZoom == zoom || zoom == e->zoom) && (!tile || e->tile == *tile)) {
            e->refs++;
            logf("RenderCache::Find: e: 0x%p page: %d refs: %d\n", e, pageNo, e->refs);
            ReportIf(i != e->cacheIdx);
            return e;
        }
    }
    return nullptr;
}

bool RenderCache::Exists(DisplayModel* dm, int pageNo, int rotation, float zoom, TilePosition* tile) {
    ScopedCritSec scope(&cacheAccess);
    BitmapCacheEntry* entry = Find(dm, pageNo, rotation, zoom, tile);
    if (entry) {
        entry->refs--;
        if (entry->refs < 1) {
            logf("RenderCache::Exists() entry: 0x%p, page: %d refs: %d\n", entry, pageNo, entry->refs);
            ReportIf(true);
        }
    }
    return entry != nullptr;
}

// assumes cacheAccess lock is taken
// drops entry but only if is not used by anyone i.e. ref count is 1
// TODO: should mark as "delete when refcount drops to 1?"
bool RenderCache::DropCacheEntryIfNotUsed(BitmapCacheEntry* entry, const char* from) {
    if (!entry || entry->refs > 1) {
        return false;
    }
    return DropCacheEntry(entry, from);
}

bool RenderCache::DropCacheEntry(BitmapCacheEntry* entry, const char* from) {
    ScopedCritSec scope(&cacheAccess);
    ReportIf(!entry);
    if (!entry) {
        return false;
    }
    int idx = entry->cacheIdx;
    ReportIf(idx < 0);
    ReportIf(idx >= cacheCount);
    if ((idx < 0) || (idx >= cacheCount)) {
        return false;
    }
    bool willDelete = entry->refs <= 1;
    logf("RenderCache::DropCacheEntry: page: %d, rotation: %d, zoom: %.2f, refs: %d, willDelete: %d, from: %s\n",
         entry->pageNo, entry->rotation, entry->zoom, entry->refs, willDelete, from);
    ReportIf(entry->refs < 1);
    --entry->refs;
    if (!willDelete) {
        return false;
    }
    ReportIf(entry->refs != 0);
    ReportIf(cache[idx] != entry);

    delete entry;

    // fast removal by replacing freed item with the item at the end
    cache[idx] = nullptr;
    int lastIdx = cacheCount - 1;
    if ((lastIdx >= 0) && (idx != lastIdx)) {
        cache[idx] = cache[lastIdx];
        cache[idx]->cacheIdx = idx;
        cache[lastIdx] = nullptr;
    }
    cacheCount--;
    ReportIf(cacheCount < 0);
    return true;
}

// assumes cacheAccess lock is taken
static bool FreeIfFull(RenderCache* rc, PageRenderRequest* req) {
    int n = rc->cacheCount;
    if (n < kMaxBitmapsCached) {
        return true;
    }

    logf("FreeIfFull: trying to free because rc->cacheCount %d > kMaxBitmapsCached (%d)\n", rc->cacheCount,
         kMaxBitmapsCached);
    DisplayModel* dm = req->dm;
    // free an invisible page of the same DisplayModel ...
    for (int i = 0; i < n; i++) {
        auto entry = rc->cache[i];
        if (entry->dm == dm && !dm->PageVisibleNearby(entry->pageNo)) {
            bool didDrop = rc->DropCacheEntryIfNotUsed(entry, "FreeIfFull");
            if (didDrop) {
                return true;
            }
        }
    }

    // ... or just the oldest cached page
    for (int i = 0; i < n; i++) {
        auto entry = rc->cache[i];
        if (entry->dm == dm) {
            // don't free pages from the document we're currently displaying
            // as it leads to flicker
            // TODO: it can still flicker if the dm is from a visible tab
            // in a different window, but it's harder to detect
            continue;
        }
        bool didDrop = rc->DropCacheEntryIfNotUsed(entry, "FreeIfFull");
        if (didDrop) {
            return true;
        }
    }
    ReportIfQuick(true);
    return false;
}

void RenderCache::Add(PageRenderRequest* req, RenderedBitmap* bmp) {
    ScopedCritSec scope(&cacheAccess);
    ReportIf(!req->dm);

    req->rotation = NormalizeRotation(req->rotation);
    ReportIf(cacheCount > kMaxBitmapsCached);

    /* It's possible there still is a cached bitmap with different zoom/rotation */
    FreePage(req->dm, req->pageNo, &req->tile);

    bool hasSpace = FreeIfFull(this, req);
    ReportIf(cacheCount > kMaxBitmapsCached);
    if (!hasSpace) {
        logf("RenderCache::Add(): no space, page: %d\n", req->pageNo);
        return;
    }

    // Copy the PageRenderRequest as it will be reused
    auto entry = new BitmapCacheEntry(req->dm, req->pageNo, req->rotation, req->zoom, req->tile, bmp);
    entry->cacheIdx = cacheCount;
    cache[cacheCount] = entry;
    logf("RenderCache::Add(): added page: %d at %d, size: (%d, %d)\n", req->pageNo, cacheCount, bmp->size.dx,
         bmp->size.dy);
    cacheCount++;
}

static RectF GetTileRect(RectF pagerect, TilePosition tile) {
    ReportIf(tile.res > 30);
    RectF rect;
    rect.dx = pagerect.dx / (1ULL << tile.res);
    rect.dy = pagerect.dy / (1ULL << tile.res);
    rect.x = pagerect.x + tile.col * rect.dx;
    rect.y = pagerect.y + ((1ULL << tile.res) - tile.row - 1) * rect.dy;
    return rect;
}

// get the coordinates of a specific tile
static Rect GetTileRectDevice(EngineBase* engine, int pageNo, int rotation, float zoom, TilePosition tile) {
    RectF mediabox = engine->PageMediabox(pageNo);
    if (tile.res > 0 && tile.res != kInvalidTileRes) {
        mediabox = GetTileRect(mediabox, tile);
    }
    RectF pixelbox = engine->Transform(mediabox, pageNo, zoom, rotation);
    return pixelbox.Round();
}

static RectF GetTileRectUser(EngineBase* engine, int pageNo, int rotation, float zoom, TilePosition tile) {
    Rect pixelbox = GetTileRectDevice(engine, pageNo, rotation, zoom, tile);
    return engine->Transform(ToRectF(pixelbox), pageNo, zoom, rotation, true);
}

static Rect GetTileOnScreen(EngineBase* engine, int pageNo, int rotation, float zoom, TilePosition tile,
                            Rect pageOnScreen) {
    Rect bbox = GetTileRectDevice(engine, pageNo, rotation, zoom, tile);
    bbox.Offset(pageOnScreen.x, pageOnScreen.y);
    return bbox;
}

static bool IsTileVisible(DisplayModel* dm, int pageNo, TilePosition tile, float fuzz = 0) {
    if (!dm) {
        return false;
    }
    PageInfo* pageInfo = dm->GetPageInfo(pageNo);
    EngineBase* engine = dm->GetEngine();
    if (!engine || !pageInfo) {
        return false;
    }
    int rotation = dm->GetRotation();
    float zoom = dm->GetZoomReal(pageNo);
    Rect r = pageInfo->pageOnScreen;
    Rect tileOnScreen = GetTileOnScreen(engine, pageNo, rotation, zoom, tile, r);
    // consider nearby tiles visible depending on the fuzz factor
    tileOnScreen.x -= (int)(tileOnScreen.dx * fuzz * 0.5);
    tileOnScreen.dx = (int)(tileOnScreen.dx * (fuzz + 1));
    tileOnScreen.y -= (int)(tileOnScreen.dy * fuzz * 0.5);
    tileOnScreen.dy = (int)(tileOnScreen.dy * (fuzz + 1));
    Rect screen(Point(), dm->GetViewPort().Size());
    return !tileOnScreen.Intersect(screen).IsEmpty();
}

/* Free all bitmaps in the cache that are of a specific page (or all pages
   of the given DisplayModel, or even all invisible pages). */
void RenderCache::FreePage(DisplayModel* dm, int pageNo, TilePosition* tile) {
    logf("RenderCache::FreePage: page: %d\n", pageNo);
    ScopedCritSec scope(&cacheAccess);

    // must go from end becaues freeing changes the cache
    for (int i = cacheCount - 1; i >= 0; i--) {
        BitmapCacheEntry* entry = cache[i];
        bool shouldFree;
        if (dm && pageNo != kInvalidPageNo) {
            // a specific page
            shouldFree = (entry->dm == dm) && (entry->pageNo == pageNo);
            if (tile) {
                // a given tile of the page or all tiles not rendered at a given resolution
                // (and at resolution 0 for quick zoom previews)
                shouldFree =
                    shouldFree && (entry->tile == *tile ||
                                   tile->row == (USHORT)-1 && entry->tile.res > 0 && entry->tile.res != tile->res ||
                                   tile->row == (USHORT)-1 && entry->tile.res == 0 && entry->outOfDate);
            }
        } else if (dm) {
            // all pages of this DisplayModel
            shouldFree = (entry->dm == dm);
        } else {
            // all invisible pages resp. page tiles
            shouldFree = !entry->dm->PageVisibleNearby(entry->pageNo);
            if (!shouldFree && entry->tile.res > 1) {
                shouldFree = !IsTileVisible(entry->dm, entry->pageNo, entry->tile, 2.0);
            }
        }
        if (shouldFree) {
            DropCacheEntryIfNotUsed(entry, "FreePage");
        }
    }
}

void RenderCache::FreeForDisplayModel(DisplayModel* dm) {
    FreePage(dm);
}

void RenderCache::FreeNotVisible() {
    FreePage();
}

// keep the cached bitmaps for visible pages to avoid flickering during a reload.
// mark invisible pages as out-of-date to prevent inconsistencies
void RenderCache::KeepForDisplayModel(DisplayModel* oldDm, DisplayModel* newDm) {
    ScopedCritSec scope(&cacheAccess);
    for (int i = 0; i < cacheCount; i++) {
        BitmapCacheEntry* entry = cache[i];
        if (entry->dm != oldDm) {
            continue;
        }
        if (oldDm->PageVisible(entry->pageNo)) {
            entry->dm = newDm;
        }
        // make sure that the page is rerendered eventually
        entry->zoom = kInvalidZoom;
        entry->outOfDate = true;
    }
}

// marks all tiles containing rect of pageNo as out of date
void RenderCache::Invalidate(DisplayModel* dm, int pageNo, RectF rect) {
    logf("RenderCache::Invalidate(): page: %d\n", pageNo);
    ScopedCritSec scopeReq(&requestAccess);

    ClearQueueForDisplayModel(dm, pageNo);
    if (curReq && curReq->dm == dm && curReq->pageNo == pageNo) {
        AbortCurrentRequest();
    }

    ScopedCritSec scopeCache(&cacheAccess);

    RectF mediabox = dm->GetEngine()->PageMediabox(pageNo);
    for (int i = 0; i < cacheCount; i++) {
        auto e = cache[i];
        if (e->dm == dm && e->pageNo == pageNo && !GetTileRect(mediabox, e->tile).Intersect(rect).IsEmpty()) {
            e->zoom = kInvalidZoom;
            e->outOfDate = true;
        }
    }
}

// determine the count of tiles required for a page at a given zoom level
USHORT RenderCache::GetTileRes(DisplayModel* dm, int pageNo) const {
    auto engine = dm->GetEngine();
    RectF mediabox = engine->PageMediabox(pageNo);
    float zoom = dm->GetZoomReal(pageNo);
    float zoomVirt = dm->GetZoomVirtual();
    Rect viewPort = dm->GetViewPort();
    int rotation = dm->GetRotation();
    RectF pixelbox = engine->Transform(mediabox, pageNo, zoom, rotation);

    float factorW = (float)pixelbox.dx / (maxTileSize.dx + 1);
    float factorH = (float)pixelbox.dy / (maxTileSize.dy + 1);
    // using the geometric mean instead of the maximum factor
    // so that the tile area doesn't get too small in comparison
    // to maxTileSize (but remains smaller)
    float factorAvg = sqrtf(factorW * factorH);

    // use larger tiles when fitting page or width or when a page is smaller
    // than the visible canvas width/height or when rendering pages
    // without clipping optimizations
    if (zoomVirt == kZoomFitPage || zoomVirt == kZoomFitWidth || pixelbox.dx <= viewPort.dx ||
        pixelbox.dy < viewPort.dy || !engine->HasClipOptimizations(pageNo)) {
        factorAvg /= 2.0;
    }

    USHORT res = 0;
    if (factorAvg > 1.5) {
        res = (USHORT)ceilf(log(factorAvg) / log(2.0f));
    }
    // limit res to 30, so that (1 << res) doesn't overflow for 32-bit signed int
    return std::min(res, (USHORT)30);
}

// get the maximum resolution available for the given page
USHORT RenderCache::GetMaxTileRes(DisplayModel* dm, int pageNo, int rotation) {
    ScopedCritSec scope(&cacheAccess);
    USHORT maxRes = 0;
    for (int i = 0; i < cacheCount; i++) {
        auto e = cache[i];
        if (e->dm == dm && e->pageNo == pageNo && e->rotation == rotation) {
            maxRes = std::max(e->tile.res, maxRes);
        }
    }
    logf("RenderCache::GetMaxTileRes(): page: %d max res: %d\n", pageNo, (int)maxRes);
    return maxRes;
}

// reduce the size of tiles in order to hopefully use less memory overall
bool RenderCache::ReduceTileSize() {
    logf("RenderCache::ReduceTileSize(): reducing tile size (current: %d x %d)\n", maxTileSize.dx, maxTileSize.dy);
    if (maxTileSize.dx < 200 || maxTileSize.dy < 200) {
        return false;
    }

    ScopedCritSec scope1(&requestAccess);
    ScopedCritSec scope2(&cacheAccess);

    if (maxTileSize.dx > maxTileSize.dy) {
        maxTileSize.dx /= 2;
    } else {
        maxTileSize.dy /= 2;
    }

    // invalidate all rendered bitmaps and all requests
    while (cacheCount > 0) {
        FreeForDisplayModel(cache[0]->dm);
    }
    while (firstRequest) {
        auto dm = firstRequest->dm;
        ClearQueueForDisplayModel(dm);
    }
    AbortCurrentRequest();

    return true;
}

void RenderCache::RequestRendering(DisplayModel* dm, int pageNo) {
    logf("RenderCache::RequestRendering(): page: %d\n", pageNo);
    TilePosition tile(GetTileRes(dm, pageNo), 0, 0);
    // only honor the request if there's a good chance that the
    // rendered tile will actually be used
    if (tile.res > 1) {
        return;
    }

    RequestRenderingTile(dm, pageNo, tile);
    // render both tiles of the first row when splitting a page in four
    // (which always happens on larger displays for Fit Width)
    if (tile.res == 1 && !IsRenderQueueFull()) {
        tile.col = 1;
        RequestRenderingTile(dm, pageNo, tile, false);
    }
}

/* Render a bitmap for page <pageNo> in <dm>. */
void RenderCache::RequestRenderingTile(DisplayModel* dm, int pageNo, TilePosition tile, bool clearQueueForPage) {
    logf("RenderCache::RequestRenderingTile(): page: %d, tile.row: %d tile.col: %d\n", pageNo, (int)tile.row,
         (int)tile.col);
    ScopedCritSec scope(&requestAccess);
    ReportIf(!dm);
    if (!dm || dm->dontRenderFlag) {
        return;
    }

    int rotation = NormalizeRotation(dm->GetRotation());
    float zoom = dm->GetZoomReal(pageNo);

    if (curReq && (curReq->pageNo == pageNo) && (curReq->dm == dm) && (curReq->tile == tile)) {
        if ((curReq->zoom == zoom) && (curReq->rotation == rotation)) {
            /* we're already rendering exactly the same page */
            return;
        }
        /* Currently rendered page is for the same page but with different zoom
        or rotation, so abort it */
        AbortCurrentRequest();
    }

    // clear requests for tiles of different resolution and invisible tiles
    if (clearQueueForPage) {
        ClearQueueForDisplayModel(dm, pageNo, &tile);
    }

    auto req = firstRequest;
    while (req) {
        bool isMatch = (req->pageNo == pageNo) && (req->dm == dm) && (req->tile == tile);
        if (isMatch) {
            req = req->next;
            continue;
        }
        if ((req->zoom == zoom) && (req->rotation == rotation)) {
            /* Request with exactly the same parameters already queued for
            rendering. Move it to the top of the queue so that it'll
            be rendered faster. */
            if (req != firstRequest) {
                ListRemove(&firstRequest, req);
                req->next = firstRequest;
                firstRequest = req;
            }
        } else {
            /* There was a request queued for the same page but with different
            zoom or rotation, so only replace this request */
            req->zoom = zoom;
            req->rotation = rotation;
        }
        return;
    }

    if (Exists(dm, pageNo, rotation, zoom, &tile)) {
        /* This page has already been rendered in the correct dimensions
           and isn't about to be rerendered in different dimensions */
        logf("RenderCache::RequestRenderingTile(): page: %d already rendered\n", pageNo);
        return;
    }

    QueueTileRenderingRequest(dm, pageNo, rotation, zoom, &tile, nullptr, nullptr);
}

void RenderCache::QueueRenderingRequest(DisplayModel* dm, int pageNo, int rotation, float zoom, RectF pageRect,
                                        const OnBitmapRendered& onRendered) {
    bool ok = QueueTileRenderingRequest(dm, pageNo, rotation, zoom, nullptr, &pageRect, &onRendered);
    if (!ok) {
        onRendered.Call(nullptr);
    }
}

bool RenderCache::QueueTileRenderingRequest(DisplayModel* dm, int pageNo, int rotation, float zoom, TilePosition* tile,
                                            RectF* pageRect, const OnBitmapRendered* onRendered) {
    ReportIf(!dm);
    if (!dm || dm->dontRenderFlag) {
        return false;
    }

    ReportIf(!(tile || pageRect && onRendered));
    if (!tile && !(pageRect && onRendered)) {
        return false;
    }
    if (tile) {
        logf("RenderCache::QueueTileRenderingRequest(): page: %d, tile->row: %d, tile->col\n", pageNo, tile->row,
             tile->col);
    } else {
        logf("RenderCache::QueueTileRenderingRequest(): page: %d\n", pageNo);
    }

    ScopedCritSec scope(&requestAccess);
    PageRenderRequest* newRequest = new PageRenderRequest;
    newRequest->dm = dm;
    newRequest->pageNo = pageNo;
    newRequest->rotation = rotation;
    newRequest->zoom = zoom;
    if (tile) {
        newRequest->pageRect = GetTileRectUser(dm->GetEngine(), pageNo, rotation, zoom, *tile);
        newRequest->tile = *tile;
    } else if (pageRect) {
        newRequest->pageRect = *pageRect;
        // can't cache bitmaps that aren't for a given tile
        ReportIf(!onRendered);
    } else {
        CrashMe();
    }
    newRequest->abort = false;
    newRequest->abortCookie = nullptr;
    newRequest->timestamp = GetTickCount();
    newRequest->renderCb = onRendered;

    newRequest->next = firstRequest;
    firstRequest = newRequest;
    SetEvent(startRendering);

    return true;
}

int RenderCache::GetRenderDelay(DisplayModel* dm, int pageNo, TilePosition tile) {
    ScopedCritSec scope(&requestAccess);

    if (curReq && curReq->pageNo == pageNo && curReq->dm == dm && curReq->tile == tile) {
        return GetTickCount() - curReq->timestamp;
    }

    auto req = firstRequest;
    while (req) {
        if (req->pageNo == pageNo && req->dm == dm && req->tile == tile) {
            return GetTickCount() - req->timestamp;
        }
        req = req->next;
    }

    return kRenderDelayUndefined;
}

PageRenderRequest* RenderCache::GetNextRequest() {
    ScopedCritSec scope(&requestAccess);

    if (!firstRequest) {
        return nullptr;
    }

    curReq = firstRequest;
    firstRequest = curReq->next;
    return curReq;
}

bool RenderCache::ClearCurrentRequest() {
    ScopedCritSec scope(&requestAccess);
    if (curReq) {
        delete curReq->abortCookie;
    }
    curReq = nullptr;

    bool isQueueEmpty = (firstRequest == nullptr);
    return isQueueEmpty;
}

/* Wait until rendering of a page beloging to <dm> has finished. */
/* TODO: this might take some time, would be good to show a dialog to let the
   user know he has to wait until we finish */
void RenderCache::CancelRendering(DisplayModel* dm) {
    ClearQueueForDisplayModel(dm);

    for (;;) {
        EnterCriticalSection(&requestAccess);
        if (!curReq || (curReq->dm != dm)) {
            // to be on the safe side
            ClearQueueForDisplayModel(dm);
            LeaveCriticalSection(&requestAccess);
            return;
        }

        AbortCurrentRequest();
        LeaveCriticalSection(&requestAccess);

        /* TODO: busy loop is not good, but I don't have a better idea */
        Sleep(50);
    }
}

void RenderCache::ClearQueueForDisplayModel(DisplayModel* dm, int pageNo, TilePosition* tile) {
    ScopedCritSec scope(&requestAccess);
again:
    PageRenderRequest* req = firstRequest;
    while (req) {
        bool shouldRemove = (req->dm == dm);
        if (shouldRemove) {
            if (pageNo != kInvalidPageNo) {
                shouldRemove = req->pageNo == pageNo;
            }
        }
        if (shouldRemove) {
            if (tile) {
                bool sameTile = req->tile.res != tile->res;
                bool tileNotVisible = !IsTileVisible(dm, req->pageNo, *tile, 0.5);
                shouldRemove = sameTile || tileNotVisible;
            }
        }
        if (!shouldRemove) {
            req = req->next;
            continue;
        }
        if (req->renderCb) {
            req->renderCb->Call(nullptr);
        }
        ListRemove(&firstRequest, req);
        delete req;
        goto again;
    }
}

void RenderCache::AbortCurrentRequest() {
    ScopedCritSec scope(&requestAccess);
    if (!curReq) {
        return;
    }
    if (curReq->abortCookie) {
        curReq->abortCookie->Abort();
    }
    curReq->abort = true;
}

// TODO: conceptually, RenderCache is not the right place for code that paints
//       (this is the only place that knows about Tiles, though)
int RenderCache::PaintTile(HDC hdc, Rect bounds, DisplayModel* dm, int pageNo, TilePosition tile, Rect tileOnScreen,
                           bool renderMissing, bool* renderOutOfDateCue, bool* renderedReplacement) {
    float zoom = dm->GetZoomReal(pageNo);
    BitmapCacheEntry* entry = Find(dm, pageNo, dm->GetRotation(), zoom, &tile);
    int renderDelay = 0;

    if (!entry) {
        if (!isRemoteSession) {
            if (renderedReplacement) {
                *renderedReplacement = true;
            }
            entry = Find(dm, pageNo, dm->GetRotation(), kInvalidZoom, &tile);
        }
        renderDelay = GetRenderDelay(dm, pageNo, tile);
        if (renderMissing && kRenderDelayUndefined == renderDelay && !IsRenderQueueFull()) {
            RequestRenderingTile(dm, pageNo, tile);
        }
    }
    RenderedBitmap* renderedBmp = entry ? entry->bitmap : nullptr;
    HBITMAP hbmp = renderedBmp ? renderedBmp->GetBitmap() : nullptr;

    if (!hbmp) {
        if (entry && !(renderedBmp && ReduceTileSize())) {
            renderDelay = kRenderDelayFailed;
        } else if (0 == renderDelay) {
            renderDelay = 1;
        }

        if (entry) {
            DropCacheEntry(entry, "PaintTile");
        }
        return renderDelay;
    }

    HDC bmpDC = CreateCompatibleDC(hdc);
    if (bmpDC) {
        Size bmpSize = renderedBmp->GetSize();
        int xSrc = -std::min(tileOnScreen.x, 0);
        int ySrc = -std::min(tileOnScreen.y, 0);
        float factor = std::min(1.0f * bmpSize.dx / tileOnScreen.dx, 1.0f * bmpSize.dy / tileOnScreen.dy);

        HGDIOBJ prevBmp = SelectObject(bmpDC, hbmp);
        int xDst = bounds.x;
        int yDst = bounds.y;
        int dxDst = bounds.dx;
        int dyDst = bounds.dy;
        if (factor != 1.0f) {
            xSrc = (int)(xSrc * factor);
            ySrc = (int)(ySrc * factor);
            int dxSrc = (int)(bounds.dx * factor);
            int dySrc = (int)(bounds.dy * factor);
            logf("RenderCache::PaintTile: StretchBlt page: %d, factor: %.f, bmpSize: (%d,%d), tileOnScreen: (%d, %d)\n",
                 pageNo, factor, bmpSize.dx, bmpSize.dy, tileOnScreen.dx, tileOnScreen.dy);
            StretchBlt(hdc, xDst, yDst, dxDst, dyDst, bmpDC, xSrc, ySrc, dxSrc, dySrc, SRCCOPY);
        } else {
            logf("RenderCache::PaintTile: BitBlt page: %d\n", pageNo);
            BitBlt(hdc, xDst, yDst, dxDst, dyDst, bmpDC, xSrc, ySrc, SRCCOPY);
        }

        SelectObject(bmpDC, prevBmp);
        DeleteDC(bmpDC);

        if (gShowTileLayout) {
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(0xff, 0xff, 0x00));
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            DrawRect(hdc, bounds);
            DeletePen(SelectObject(hdc, oldPen));
        }
    }

    if (entry->outOfDate) {
        if (renderOutOfDateCue) {
            *renderOutOfDateCue = true;
        }
        ReportIf(renderedReplacement && !*renderedReplacement);
    }

    DropCacheEntry(entry, "PaintTile");
    return 0;
}

static int cmpTilePosition(const void* a, const void* b) {
    const TilePosition *ta = (const TilePosition*)a, *tb = (const TilePosition*)b;
    return ta->res != tb->res ? ta->res - tb->res : ta->row != tb->row ? ta->row - tb->row : ta->col - tb->col;
}

int RenderCache::Paint(HDC hdc, Rect bounds, DisplayModel* dm, int pageNo, PageInfo* pageInfo,
                       bool* renderOutOfDateCue) {
    ReportIf(!pageInfo->shown || 0.0 == pageInfo->visibleRatio);

    auto timeStart = TimeGet();
    auto dur = TimeSinceInMs(timeStart);

    bool shouldCache = dm->ShouldCacheRendering(pageNo);
    logf("RenderCache::Paint() page: %d, bounds=(%d,%d,%d,%d) should cache: %d\n", pageNo, bounds.x, bounds.y,
         bounds.dx, bounds.dy, (int)shouldCache);

    if (!shouldCache) {
        int rotation = dm->GetRotation();
        float zoom = dm->GetZoomReal(pageNo);
        bounds = pageInfo->pageOnScreen.Intersect(bounds);

        RectF area = ToRectF(bounds);
        area.Offset(-pageInfo->pageOnScreen.x, -pageInfo->pageOnScreen.y);
        area = dm->GetEngine()->Transform(area, pageNo, zoom, rotation, true);

        RenderPageArgs args(pageNo, zoom, rotation, &area);
        RenderedBitmap* bmp = dm->GetEngine()->RenderPage(args);
        bool success = bmp && bmp->IsValid() && bmp->Blit(hdc, bounds);
        delete bmp;

        return success ? 0 : kRenderDelayFailed;
    }

    int rotation = dm->GetRotation();
    float zoom = dm->GetZoomReal(pageNo);
    USHORT targetRes = GetTileRes(dm, pageNo);
    USHORT maxRes = GetMaxTileRes(dm, pageNo, rotation);
    if (maxRes < targetRes) {
        maxRes = targetRes;
    }

    Vec<TilePosition> queue;
    queue.Append(TilePosition(0, 0, 0));
    int renderDelayMin = kRenderDelayUndefined;
    bool neededScaling = false;

    while (queue.size() > 0) {
        TilePosition tile = queue.PopAt(0);
        Rect tileOnScreen = GetTileOnScreen(dm->GetEngine(), pageNo, rotation, zoom, tile, pageInfo->pageOnScreen);
        if (tileOnScreen.IsEmpty()) {
            // display an error message when only empty tiles should be drawn (i.e. on page loading errors)
            renderDelayMin = std::min(kRenderDelayFailed, renderDelayMin);
            continue;
        }
        tileOnScreen = pageInfo->pageOnScreen.Intersect(tileOnScreen);
        Rect isect = bounds.Intersect(tileOnScreen);
        if (isect.IsEmpty()) {
            continue;
        }

        bool isTargetRes = tile.res == targetRes;
        int renderDelay = PaintTile(hdc, isect, dm, pageNo, tile, tileOnScreen, isTargetRes, renderOutOfDateCue,
                                    isTargetRes ? &neededScaling : nullptr);
        if (!(isTargetRes && 0 == renderDelay) && tile.res < maxRes) {
            queue.Append(TilePosition(tile.res + 1, tile.row * 2, tile.col * 2));
            queue.Append(TilePosition(tile.res + 1, tile.row * 2, tile.col * 2 + 1));
            queue.Append(TilePosition(tile.res + 1, tile.row * 2 + 1, tile.col * 2));
            queue.Append(TilePosition(tile.res + 1, tile.row * 2 + 1, tile.col * 2 + 1));
        }
        if (isTargetRes && renderDelay != 0) {
            neededScaling = true;
        }
        if (renderDelay == kRenderDelayFailed || renderDelayMin == kRenderDelayFailed) {
            renderDelayMin = kRenderDelayFailed;
        } else {
            renderDelayMin = std::min(renderDelay, renderDelayMin);
        }
        // paint tiles from left to right from top to bottom
        if (tile.res > 0 && queue.size() > 0 && tile.res < queue.at(0).res) {
            queue.Sort(cmpTilePosition);
        }
    }

#ifdef CONSERVE_MEMORY
    if (!neededScaling) {
        if (renderOutOfDateCue) {
            *renderOutOfDateCue = false;
        }
        // free tiles with different resolution
        TilePosition tile(targetRes, (USHORT)-1, 0);
        logf("RenderCache::Paint: conserve memory, calling FreePage() page: %d\n", pageNo);
        FreePage(dm, pageNo, &tile);
    }
    FreeNotVisible();
#endif

    return renderDelayMin;
}
