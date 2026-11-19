// See media.h. MinGW has no header for Windows.Media.Control, so the ABI
// interfaces are declared here. Interface IDs and method order were read from
// the Windows metadata (Windows.Media.Control.winmd); do not reorder.

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "media.h"
#include <inspectable.h>
#include <roapi.h>
#include <winstring.h>
#include <wincodec.h>
#include <cwchar>

namespace {

// ---------- GUIDs ----------
const GUID kIID_Statics      = {0x2050C4EE, 0x11A0, 0x57DE, {0xAE, 0xD7, 0xC9, 0x7C, 0x70, 0x33, 0x82, 0x45}};
const GUID kIID_AsyncInfo    = {0x00000036, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const GUID kIID_IUnknownM    = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const GUID kIID_IMarshal     = {0x00000003, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const GUID kIID_IStdMarshal  = {0x00000018, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const GUID kIID_IExtConn     = {0x00000019, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const GUID kIID_IWeakRefSrc  = {0x00000038, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const GUID kIID_IInspectable = {0xAF86E2E0, 0xB12D, 0x4C6A, {0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90}};
const GUID kIID_INoMarshal   = {0xECC8691B, 0xC1DB, 0x4DC0, {0x85, 0x5E, 0x65, 0xF6, 0xC5, 0x51, 0xAF, 0x49}};
const GUID kIID_ICallFactory = {0x1C733A30, 0x2A1C, 0x11CE, {0xAD, 0xE5, 0x00, 0xAA, 0x00, 0x44, 0x77, 0x3D}};
const GUID kIID_IStream      = {0x0000000C, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const GUID kCLSID_WIC        = {0xCACAF262, 0x9370, 0x4615, {0xA1, 0x3B, 0x9F, 0x55, 0x39, 0xDA, 0x4C, 0x0A}};
const GUID kIID_WICFactory   = {0xEC5EC8A9, 0xC395, 0x4314, {0x9C, 0x77, 0x54, 0xD7, 0xA9, 0x35, 0xFF, 0x70}};
const GUID kFmtPBGRA         = {0x6FDDC324, 0x4E03, 0x4BFE, {0xB1, 0x85, 0x3D, 0x77, 0x76, 0x8D, 0xC9, 0x10}};

} // namespace

// ---------- ABI interfaces (vtable order matters) ----------
// These must NOT be in an anonymous namespace: with internal linkage GCC -O2
// concludes nothing implements them and turns the virtual calls into traps.
struct Token { INT64 value; };

struct IAsyncInfoRaw : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Id(UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Status(int*) = 0; // 0 Started, 1 Completed, 2 Canceled, 3 Error
    virtual HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Cancel() = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};
struct IAsyncOpRaw : IInspectable { // IAsyncOperation<T>
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(void* result) = 0;
};
struct IStreamRef : IInspectable { // IRandomAccessStreamReference
    virtual HRESULT STDMETHODCALLTYPE OpenReadAsync(IInspectable** op) = 0;
};
struct IMediaProps : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Title(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Subtitle(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumArtist(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Artist(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumTitle(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TrackNumber(INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Genres(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumTrackCount(INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackType(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Thumbnail(IStreamRef**) = 0;
};
struct ITimeline : IInspectable { // GlobalSystemMediaTransportControlsSessionTimelineProperties
    virtual HRESULT STDMETHODCALLTYPE get_StartTime(INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_EndTime(INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MinSeekTime(INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MaxSeekTime(INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Position(INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_LastUpdatedTime(INT64*) = 0; // DateTime: FILETIME ticks
};
struct IPlaybackInfo : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Controls(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackStatus(INT32*) = 0; // 4 Playing, 5 Paused
};
struct ISession : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_SourceAppUserModelId(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryGetMediaPropertiesAsync(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTimelineProperties(ITimeline**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPlaybackInfo(IPlaybackInfo**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryPlayAsync(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryPauseAsync(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryStopAsync(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryRecordAsync(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryFastForwardAsync(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryRewindAsync(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TrySkipNextAsync(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TrySkipPreviousAsync(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangeChannelUpAsync(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangeChannelDownAsync(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryTogglePlayPauseAsync(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangeAutoRepeatModeAsync(INT32, IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangePlaybackRateAsync(DOUBLE, IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangeShuffleActiveAsync(boolean, IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangePlaybackPositionAsync(INT64, IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_TimelinePropertiesChanged(IUnknown*, Token*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_TimelinePropertiesChanged(Token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_PlaybackInfoChanged(IUnknown*, Token*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_PlaybackInfoChanged(Token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_MediaPropertiesChanged(IUnknown*, Token*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_MediaPropertiesChanged(Token) = 0;
};
struct IManager : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSession(ISession**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSessions(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_CurrentSessionChanged(IUnknown*, Token*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_CurrentSessionChanged(Token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_SessionsChanged(IUnknown*, Token*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_SessionsChanged(Token) = 0;
};
struct IManagerStatics : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE RequestAsync(IInspectable** op) = 0;
};

namespace {

// Event handler for every TypedEventHandler<...> we subscribe to: it only
// wakes the worker. The event source asks for the handler's parameterized
// interface ID; we accept it (and IAgileObject) but refuse COM plumbing
// interfaces we don't implement.
class WakeHandler final : public IUnknown {
    LONG m_ref = 1;
    HANDLE m_event;

    ~WakeHandler() {} // NOT virtual: a virtual destructor would take vtable slots before Invoke

public:
    explicit WakeHandler(HANDLE e) : m_event(e) {}
    STDMETHODIMP QueryInterface(REFIID iid, void** pp) override {
        const GUID* refuse[] = {&kIID_IMarshal, &kIID_IStdMarshal, &kIID_IExtConn, &kIID_IWeakRefSrc,
                                &kIID_IInspectable, &kIID_INoMarshal, &kIID_ICallFactory};
        for (const GUID* g : refuse)
            if (IsEqualIID(iid, *g)) { *pp = nullptr; return E_NOINTERFACE; }
        *pp = this;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG r = InterlockedDecrement(&m_ref);
        if (!r) delete this;
        return r;
    }
    // TypedEventHandler<TSender, TArgs>::Invoke
    virtual HRESULT STDMETHODCALLTYPE Invoke(IInspectable*, IInspectable*) {
        SetEvent(m_event);
        return S_OK;
    }
};

template <class T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

// ---------- shared state ----------
CRITICAL_SECTION g_cs;
MediaInfo g_info;
HWND g_notify;
UINT g_msg;
HANDLE g_thread, g_stop, g_changed, g_cmdEvent;
volatile LONG g_cmd;

// Waits for an IAsyncOperation<T> and fetches its result (worker thread only).
bool Await(IInspectable* op, void* result, DWORD timeoutMs = 4000) {
    if (!op) return false;
    IAsyncInfoRaw* info = nullptr;
    if (FAILED(op->QueryInterface(kIID_AsyncInfo, (void**)&info))) return false;
    int st = 0;
    DWORD t0 = GetTickCount();
    while (SUCCEEDED(info->get_Status(&st)) && st == 0 && GetTickCount() - t0 < timeoutMs) Sleep(5);
    info->Release();
    if (st != 1) return false;
    return result ? SUCCEEDED(static_cast<IAsyncOpRaw*>(op)->GetResults(result)) : true;
}

void CopyHString(HSTRING h, wchar_t* out, size_t n) {
    out[0] = 0;
    if (!h) return;
    UINT32 len = 0;
    const wchar_t* s = WindowsGetStringRawBuffer(h, &len);
    if (len >= n) len = (UINT32)n - 1;
    wmemcpy(out, s, len);
    out[len] = 0;
    WindowsDeleteString(h);
}

typedef HRESULT(WINAPI* PFN_CreateStreamOverRAS)(IUnknown*, REFIID, void**);

// Decodes the thumbnail, centre-crops it to a square and scales it to `size`.
bool LoadArtwork(IStreamRef* ref, int size, std::vector<BYTE>& out) {
    static PFN_CreateStreamOverRAS createStream =
        (PFN_CreateStreamOverRAS)(void*)GetProcAddress(LoadLibraryW(L"shcore.dll"), "CreateStreamOverRandomAccessStream");
    if (!ref || !createStream) return false;
    bool ok = false;
    IInspectable *op = nullptr, *ras = nullptr;
    IStream* stream = nullptr;
    IWICImagingFactory* wic = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapClipper* clip = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICFormatConverter* conv = nullptr;
    UINT w = 0, h = 0;
    if (FAILED(ref->OpenReadAsync(&op)) || !Await(op, &ras)) goto done;
    if (FAILED(createStream(ras, kIID_IStream, (void**)&stream))) goto done;
    if (FAILED(CoCreateInstance(kCLSID_WIC, nullptr, CLSCTX_INPROC_SERVER, kIID_WICFactory, (void**)&wic))) goto done;
    if (FAILED(wic->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &dec))) goto done;
    if (FAILED(dec->GetFrame(0, &frame)) || FAILED(frame->GetSize(&w, &h)) || !w || !h) goto done;
    {
        UINT side = w < h ? w : h;
        WICRect rc = {(INT)(w - side) / 2, (INT)(h - side) / 2, (INT)side, (INT)side};
        if (FAILED(wic->CreateBitmapClipper(&clip)) || FAILED(clip->Initialize(frame, &rc))) goto done;
    }
    if (FAILED(wic->CreateBitmapScaler(&scaler)) || FAILED(scaler->Initialize(clip, size, size, WICBitmapInterpolationModeFant))) goto done;
    if (FAILED(wic->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(scaler, kFmtPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) goto done;
    out.resize((size_t)size * size * 4);
    ok = SUCCEEDED(conv->CopyPixels(nullptr, size * 4, (UINT)out.size(), out.data()));
done:
    SafeRelease(conv); SafeRelease(scaler); SafeRelease(clip); SafeRelease(frame); SafeRelease(dec);
    SafeRelease(wic); SafeRelease(stream); SafeRelease(ras); SafeRelease(op);
    if (!ok) out.clear();
    return ok;
}

void Publish(const MediaInfo& m) {
    EnterCriticalSection(&g_cs);
    g_info = m;
    LeaveCriticalSection(&g_cs);
    PostMessageW(g_notify, g_msg, 0, 0);
}

DWORD WINAPI Worker(LPVOID) {
    RoInitialize(RO_INIT_MULTITHREADED);
    IManagerStatics* statics = nullptr;
    IManager* mgr = nullptr;
    ISession* session = nullptr;
    WakeHandler* handler = new WakeHandler(g_changed);
    Token tCurrent = {}, tSessions = {}, tProps = {}, tPlayback = {}, tTimeline = {};
    MediaInfo cur;

    {
        HSTRING_HEADER hdr;
        HSTRING cls;
        const wchar_t* name = L"Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager";
        if (SUCCEEDED(WindowsCreateStringReference(name, (UINT32)wcslen(name), &hdr, &cls)))
            RoGetActivationFactory(cls, kIID_Statics, (void**)&statics);
    }
    if (statics) {
        IInspectable* op = nullptr;
        if (SUCCEEDED(statics->RequestAsync(&op))) Await(op, &mgr, 10000);
        SafeRelease(op);
    }
    if (mgr) {
        mgr->add_CurrentSessionChanged(handler, &tCurrent);
        mgr->add_SessionsChanged(handler, &tSessions);
        SetEvent(g_changed);
    }

    HANDLE waits[3] = {g_changed, g_cmdEvent, g_stop};
    for (;;) {
        DWORD w = WaitForMultipleObjects(3, waits, FALSE, INFINITE);
        if (w == WAIT_OBJECT_0 + 2 || w == WAIT_FAILED) break;

        if (w == WAIT_OBJECT_0 + 1) { // a button was pressed
            LONG cmd = InterlockedExchange(&g_cmd, 0);
            if (session && cmd) {
                IInspectable* op = nullptr;
                if (cmd == MEDIA_PLAYPAUSE) session->TryTogglePlayPauseAsync(&op);
                else if (cmd == MEDIA_NEXT) session->TrySkipNextAsync(&op);
                else if (cmd == MEDIA_PREV) session->TrySkipPreviousAsync(&op);
                boolean done = false;
                Await(op, &done, 2000);
                SafeRelease(op);
            }
            continue;
        }
        if (!mgr) continue;
        Sleep(30); // let bursts of change events settle, then read once

        // Follow the current session; move our subscriptions when it changes.
        ISession* now = nullptr;
        mgr->GetCurrentSession(&now);
        bool same = false;
        if (now && session) {
            IUnknown *a = nullptr, *b = nullptr;
            now->QueryInterface(kIID_IUnknownM, (void**)&a);
            session->QueryInterface(kIID_IUnknownM, (void**)&b);
            same = a == b;
            SafeRelease(a); SafeRelease(b);
        }
        if (same) {
            SafeRelease(now);
        } else {
            if (session) {
                session->remove_MediaPropertiesChanged(tProps);
                session->remove_PlaybackInfoChanged(tPlayback);
                session->remove_TimelinePropertiesChanged(tTimeline);
                SafeRelease(session);
            }
            session = now;
            if (session) {
                session->add_MediaPropertiesChanged(handler, &tProps);
                session->add_PlaybackInfoChanged(handler, &tPlayback);
                session->add_TimelinePropertiesChanged(handler, &tTimeline);
            }
        }

        MediaInfo next;
        if (session) {
            next.hasSession = true;
            IPlaybackInfo* pb = nullptr;
            if (SUCCEEDED(session->GetPlaybackInfo(&pb)) && pb) {
                INT32 st = 0;
                pb->get_PlaybackStatus(&st);
                next.playing = st == 4;
                pb->Release();
            }
            ITimeline* tl = nullptr;
            if (SUCCEEDED(session->GetTimelineProperties(&tl)) && tl) {
                INT64 start = 0, end = 0;
                tl->get_StartTime(&start);
                tl->get_EndTime(&end);
                tl->get_Position(&next.pos);
                tl->get_LastUpdatedTime(&next.posAt);
                next.length = end > start ? end - start : 0;
                next.pos -= start;
                tl->Release();
            }
            IInspectable* op = nullptr;
            IMediaProps* props = nullptr;
            if (SUCCEEDED(session->TryGetMediaPropertiesAsync(&op)) && Await(op, &props) && props) {
                HSTRING s = nullptr;
                if (SUCCEEDED(props->get_Title(&s))) CopyHString(s, next.title, 256);
                s = nullptr;
                if (SUCCEEDED(props->get_Artist(&s))) CopyHString(s, next.artist, 256);
                bool trackChanged = wcscmp(next.title, cur.title) != 0 || wcscmp(next.artist, cur.artist) != 0 || !cur.hasSession;
                // Apps often publish the cover a moment after the title, so keep trying while it's missing.
                if (trackChanged || cur.art.empty()) {
                    IStreamRef* thumb = nullptr;
                    props->get_Thumbnail(&thumb);
                    next.artSize = 96;
                    LoadArtwork(thumb, next.artSize, next.art);
                    SafeRelease(thumb);
                    next.artVersion = cur.artVersion + ((trackChanged || !next.art.empty()) ? 1 : 0);
                } else {
                    next.artSize = cur.artSize;
                    next.art = cur.art;
                    next.artVersion = cur.artVersion;
                }
                props->Release();
            }
            SafeRelease(op);
        } else {
            next.artVersion = cur.artVersion + (cur.art.empty() ? 0 : 1);
        }

        bool changed = next.hasSession != cur.hasSession || next.playing != cur.playing ||
                       wcscmp(next.title, cur.title) != 0 || wcscmp(next.artist, cur.artist) != 0 ||
                       next.artVersion != cur.artVersion || next.pos != cur.pos || next.length != cur.length ||
                       next.posAt != cur.posAt;
        cur = next;
        if (changed) Publish(cur);
    }

    if (session) {
        session->remove_MediaPropertiesChanged(tProps);
        session->remove_PlaybackInfoChanged(tPlayback);
        session->remove_TimelinePropertiesChanged(tTimeline);
        session->Release();
    }
    if (mgr) {
        mgr->remove_CurrentSessionChanged(tCurrent);
        mgr->remove_SessionsChanged(tSessions);
        mgr->Release();
    }
    SafeRelease(statics);
    handler->Release();
    RoUninitialize();
    return 0;
}

} // namespace

void Media_Start(HWND notify, UINT msg) {
    InitializeCriticalSection(&g_cs);
    g_notify = notify;
    g_msg = msg;
    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_changed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_cmdEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 512 * 1024, Worker, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
}

void Media_Stop() {
    if (!g_thread) return;
    SetEvent(g_stop);
    WaitForSingleObject(g_thread, 3000);
    CloseHandle(g_thread);
    g_thread = nullptr;
    CloseHandle(g_stop);
    CloseHandle(g_changed);
    CloseHandle(g_cmdEvent);
    DeleteCriticalSection(&g_cs);
}

void Media_Get(MediaInfo& out) {
    EnterCriticalSection(&g_cs);
    out = g_info;
    LeaveCriticalSection(&g_cs);
}

INT64 Media_Position(const MediaInfo& m) {
    INT64 p = m.pos;
    if (m.playing && m.posAt > 0) {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        INT64 now = ((INT64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        if (now > m.posAt) p += now - m.posAt;
    }
    if (m.length > 0 && p > m.length) p = m.length;
    return p < 0 ? 0 : p;
}

void Media_Command(MediaCommand cmd) {
    InterlockedExchange(&g_cmd, cmd);
    SetEvent(g_cmdEvent);
}
