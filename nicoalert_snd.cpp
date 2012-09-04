//	ニコ生アラート(Love)
//	Copyright (C) 2012 naoki.kp

#include "nicoalert_snd.h"

// 音声制御クラス

nicoalert_snd::~nicoalert_snd(void){
    close();
}

// DirectShowインタフェースOPEN
bool nicoalert_snd::open(void){
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

    bInitialized = true;
    return true;
}

// DirectShowインタフェースCLOSE
bool nicoalert_snd::close(void){
    if(!bInitialized) return true;

    if(pCapBuilder)   pCapBuilder->Release();
    if(pBasicAudio)   pBasicAudio->Release();
    if(pMediaControl) pMediaControl->Release();
    if(pGraphBuilder) pGraphBuilder->Release();
    if(pDirectSound)  pDirectSound->Release();
    CoUninitialize();

    bInitialized = false;
    return true;
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

// サウンド再生(すでに再生中のものは停止)
// TODO: 再生終了後もフィルタが残り続けるのをなんとかする。
//       →ffdshow使ってるとアイコンが残る
bool nicoalert_snd::play(const TCHAR *fn){
    open();
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

    setVolPlaying(m_vol);

#ifdef _DEBUG
    DbgAllFilter();
#endif

    // 再生開始
    pMediaControl->Run();

    return true;
}
