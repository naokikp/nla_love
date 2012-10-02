//	ニコ生アラート(Love)
//	Copyright (C) 2012 naoki.kp

#include "nicoalert_snd.h"

// 音声制御クラス


// -- 制御スレッド用関数 --

// 中継スレッドエントリ
static unsigned int _stdcall worker_thr(void *arg){
    nicoalert_snd * const c = (nicoalert_snd * const)arg;
    c->thread();
    return 0;
}
// 中継ウィンドウプロシージャ
static LRESULT CALLBACK worker_proc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp){
    nicoalert_snd *c = NULL;

    _dbg(_T("worker msg = %u, wp = %x, lp = %x\n"), msg, wp, lp);

    if(msg == WM_CREATE){
        CREATESTRUCT *cs = (CREATESTRUCT *)lp;
        SetWindowLongPtr(hWnd, GWL_USERDATA, (LONG_PTR)cs->lpCreateParams);
        c = (nicoalert_snd *)(LONG_PTR)cs->lpCreateParams;
    } else {
        c = (nicoalert_snd *)GetWindowLongPtr(hWnd, GWL_USERDATA);
    }
    if(c){
        return c->wndproc(hWnd, msg, wp, lp);
    }
    return DefWindowProc(hWnd, msg, wp, lp);;
}

// 制御スレッド
void nicoalert_snd::thread(void){
    if(!open_thr()){
        bInitFailed = true;
        SetEvent(hEventCtrl);
        _endthreadex(1);
        return;
    }
    SetEvent(hEventCtrl);

	MSG msg;
	while(GetMessage(&msg, NULL, 0, 0)){
        TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

    close_thr();
    SetEvent(hEventCtrl);
    return;
}

// 制御スレッドウィンドウプロシージャ
LRESULT CALLBACK nicoalert_snd::wndproc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp){
    switch(msg){
    case WM_CREATE:
        break;
    case WM_SND_PLAY:
        return play_thr((const TCHAR *)lp);
        break;
    case WM_SND_STOP:
        stop_thr();
        break;
    case WM_SND_NOTIFY:
        notify();
        break;
    case WM_SND_VOLSET:
        setVolPlaying_thr((int)lp);
        break;
    case WM_SND_CLOSE:
        stop_thr();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, msg, wp, lp);
    }

    return TRUE;
}

// DirectShowインタフェースOPEN / 制御ウィンドウ生成
bool nicoalert_snd::open_thr(void){
    if(bInitialized) return true;

    HRESULT hr;
    // COMを初期化
    CoInitialize(NULL);

    // FilterGraphを生成
    hr = CoCreateInstance(CLSID_FilterGraph,
        NULL,
        CLSCTX_INPROC,
        IID_IGraphBuilder,
        (LPVOID *)&pGraphBuilder);
    if(FAILED(hr)){
        _dbg(_T("FilterGraphを生成失敗\n"));
        return false;
    }

    // MediaControlインターフェース取得
    hr = pGraphBuilder->QueryInterface(IID_IMediaControl,
        (LPVOID *)&pMediaControl);
    if(FAILED(hr)){
        _dbg(_T("MediaControlインターフェース取得失敗\n"));
        return false;
    }

    // pMediaEventExインターフェース取得
    hr = pGraphBuilder->QueryInterface(IID_IMediaEventEx,
        (LPVOID *)&pMediaEventEx);
    if(FAILED(hr)){
        _dbg(_T("pMediaEventExインターフェース取得失敗\n"));
        return false;
    }

    // オーディオインターフェース取得
    pGraphBuilder->QueryInterface(IID_IBasicAudio,
        (LPVOID *)&pBasicAudio);
    if(FAILED(hr)){
        _dbg(_T("BasicAudioインターフェース取得失敗\n"));
        return false;
    }

    // DirectSoundFilterを生成
    hr = CoCreateInstance(CLSID_DSoundRender,
        NULL,
        CLSCTX_INPROC,
        IID_IBaseFilter,
        (LPVOID *)&pDirectSound);
    if(FAILED(hr)){
        _dbg(_T("DirectSoundFilter生成失敗\n"));
        return false;
    }

    // CaptureGraphBuilder2を生成
    hr = CoCreateInstance(CLSID_CaptureGraphBuilder2,
        NULL,
        CLSCTX_INPROC_SERVER, 
        IID_ICaptureGraphBuilder2, 
        (void **)&pCapBuilder);
    if(FAILED(hr)){
        _dbg(_T("CaptureGraphBuilder2生成失敗\n"));
        return false;
    } else {
        pCapBuilder->SetFiltergraph(pGraphBuilder);
    }

    WNDCLASS wc;
    memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = worker_proc;
	wc.lpszClassName = NICOALERT_SND_WORKERWND;
	if(!RegisterClass(&wc)){
        return false;
    }

    hWndCtrl = CreateWindowEx(
        0, NICOALERT_SND_WORKERWND, _T(""),
        WS_POPUP | WS_DISABLED, 0, 0, 0, 0, NULL, NULL, 0, this);
    if(hWndCtrl == NULL){
        return false;
    }

    bInitialized = true;
    return true;
}

// DirectShowインタフェースCLOSE
bool nicoalert_snd::close_thr(void){
    if(!bInitialized) return true;

    stop_thr();

    if(pCapBuilder)   pCapBuilder->Release();
    if(pBasicAudio)   pBasicAudio->Release();
    if(pMediaControl) pMediaControl->Release();
    if(pGraphBuilder) pGraphBuilder->Release();
    if(pDirectSound)  pDirectSound->Release();
    CoUninitialize();

    bInitialized = false;
    return true;
}


// サウンド再生(すでに再生中のものは停止)
bool nicoalert_snd::play_thr(const TCHAR *fn){
    if(!bInitialized) return false;
    if(!pMediaControl) return false;
    if(!pGraphBuilder) return false;
    if(!pCapBuilder) return false;

    HRESULT hr;
    IBaseFilter *pSrc = NULL;

    pMediaControl->Stop();
    RemoveAllFilter();

    hr = pGraphBuilder->AddSourceFilter(fn, NULL, &pSrc);
    _dbg(_T("AddSourceFilter() = %x\n"), hr);
    if(FAILED(hr)) return false;

    //hr = pGraphBuilder->AddFilter(pSrc, fn);
    //_dbg(_T("AddFilter(pSrc) = %x\n"), hr);

    hr = pGraphBuilder->AddFilter(pDirectSound, _T("DirectSound"));
    _dbg(_T("AddFilter(pDirectSound) = %x\n"), hr);
    if(FAILED(hr)) return false;

    hr = pCapBuilder->RenderStream(NULL, NULL, pSrc, NULL, pDirectSound);
    _dbg(_T("RenderStream() = %x\n"), hr);
    if(FAILED(hr)) return false;

    pSrc->Release(); pSrc = NULL;

    if(hr == VFW_E_NOT_IN_GRAPH){
        return false;
    }

    setVolPlaying_thr(m_vol);

#ifdef _DEBUG
    DbgAllFilter();
#endif

    // イベント通知
    pMediaEventEx->SetNotifyWindow((OAHWND)hWndCtrl, WM_SND_NOTIFY, NULL);

    // 再生開始
    pMediaControl->Run();

    return true;
}

bool nicoalert_snd::stop_thr(void){
    if(!bInitialized) return false;
    if(!pMediaControl) return false;
    if(!pGraphBuilder) return false;

    pMediaControl->Stop();
    RemoveAllFilter();
    return true;
}

void nicoalert_snd::notify(void){
    long evCode;
    LONG param1, param2;
    HRESULT hr;

    while(1){
        hr = pMediaEventEx->GetEvent(&evCode, &param1, &param2, 0);
        if(FAILED(hr)) break;
        hr = pMediaEventEx->FreeEventParams(evCode, param1, param2);
        if(FAILED(hr)) break;

        switch(evCode){
        case EC_COMPLETE:   // 再生終了
            _dbg(_T("nicoalert_snd::notify EC_COMPLETE\n"));
            stop_thr();
            break;
        default:
            _dbg(_T("nicoalert_snd::notify 0x%04x\n"), evCode);
            break;
        }
    }
    return;
}

// 再生中に音量変更(保存なし)
void nicoalert_snd::setVolPlaying_thr(int vol){
    if(!bInitialized) return;
    if(!pBasicAudio) return;
    if(vol < 0) vol = 0;
    if(vol > 100) vol = 100;
    LONG lVol = -10000;
    if(vol > 0) lVol = (LONG)(log10(vol/100.0)*2000);  //vol比 ⇒ dB計算
    _dbg(_T("vol = %d, lVol = %ld\n"), vol, lVol);
    pBasicAudio->put_Volume(lVol); 
}


#ifdef _DEBUG
// 構築済みフィルタの表示(debug)
bool nicoalert_snd::DbgAllFilter(){
    IEnumFilters *pEnum;
    FILTER_INFO info;
    HRESULT hr = pGraphBuilder->EnumFilters(&pEnum);
    if(FAILED(hr)) return false;

    IBaseFilter *bf;
    while(pEnum->Next(1, &bf, NULL) == S_OK){
        hr = bf->QueryFilterInfo(&info);
        if(FAILED(hr)) return false;
        _dbg(_T("name = %s\n"), info.achName);
        info.pGraph->Release();

        hr = bf->Release();
        if(FAILED(hr)) return false;
    }
    pEnum->Release();
    return true;
}
#endif

// 全フィルタの削除
bool nicoalert_snd::RemoveAllFilter(){
    if(!pGraphBuilder) return false;

    IEnumFilters *pEnum;
    HRESULT hr = pGraphBuilder->EnumFilters(&pEnum);
    if(FAILED(hr)) return false;

    IBaseFilter *bf;
    while(pEnum->Next(1, &bf, NULL) == S_OK){
        hr = pGraphBuilder->RemoveFilter(bf);
        if(FAILED(hr)) return false;
        hr = pEnum->Reset();
        if(FAILED(hr)) return false;
        hr = bf->Release();
        if(FAILED(hr)) return false;
    }
    pEnum->Release();
    return true;
}


// -- 制御受付関数 --


// 音声制御クラス初期化
bool nicoalert_snd::open(void){
    m.lock();
    if(bInitialized){
        m.unlock();
        return true;
    }

    bInitFailed = false;
    hEventCtrl = CreateEvent(NULL, FALSE, FALSE, NULL);

    uintptr_t hThr = _beginthreadex(NULL, 0, worker_thr, this, 0, NULL);

    while(1){
        WaitForSingleObject(hEventCtrl, INFINITE);
        if(bInitialized || bInitFailed) break;
    }

    if(bInitFailed){
        m.unlock();
        return false;
    }
    m.unlock();
    return true;
}

bool nicoalert_snd::close(void){
    m.lock();
    if(!bInitialized){
        m.unlock();
        return true;
    }
    int waitcount = PROCESS_EXIT_WAITCOUNT;
    DWORD ret;

    PostMessage(hWndCtrl, WM_SND_CLOSE, 0, 0);

    while(waitcount){
        ret = WaitForSingleObject(hEventCtrl, 500);
        if(ret == WAIT_OBJECT_0) break;
        waitcount--;
    }

    m.unlock();
    return true;
}

bool nicoalert_snd::play(const TCHAR *fn){
    if(!open()) return false;
    if(!hWndCtrl) return false;

    m.lock();
    BOOL result = SendMessage(hWndCtrl, WM_SND_PLAY, 0, (LPARAM)fn);
    m.unlock();
    return result?true:false;
}

// 音量取得
int nicoalert_snd::getVol(void){
    return m_vol;
}
// 音量変更
void nicoalert_snd::setVol(int vol){
    if(vol < 0) vol = 0;
    if(vol > 100) vol = 100;
    m_vol = vol;
}

// 再生中に音量変更(保存なし)
void nicoalert_snd::setVolPlaying(int vol){
    if(!bInitialized) return;
    if(!hWndCtrl) return;

    m.lock();
    BOOL result = SendMessage(hWndCtrl, WM_SND_VOLSET, 0, (LPARAM)vol);
    m.unlock();
    return;
}
