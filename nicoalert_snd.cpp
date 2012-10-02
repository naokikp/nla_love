//	�j�R���A���[�g(Love)
//	Copyright (C) 2012 naoki.kp

#include "nicoalert_snd.h"

// ��������N���X


// -- ����X���b�h�p�֐� --

// ���p�X���b�h�G���g��
static unsigned int _stdcall worker_thr(void *arg){
    nicoalert_snd * const c = (nicoalert_snd * const)arg;
    c->thread();
    return 0;
}
// ���p�E�B���h�E�v���V�[�W��
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

// ����X���b�h
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

// ����X���b�h�E�B���h�E�v���V�[�W��
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

// DirectShow�C���^�t�F�[�XOPEN / ����E�B���h�E����
bool nicoalert_snd::open_thr(void){
    if(bInitialized) return true;

    HRESULT hr;
    // COM��������
    CoInitialize(NULL);

    // FilterGraph�𐶐�
    hr = CoCreateInstance(CLSID_FilterGraph,
        NULL,
        CLSCTX_INPROC,
        IID_IGraphBuilder,
        (LPVOID *)&pGraphBuilder);
    if(FAILED(hr)){
        _dbg(_T("FilterGraph�𐶐����s\n"));
        return false;
    }

    // MediaControl�C���^�[�t�F�[�X�擾
    hr = pGraphBuilder->QueryInterface(IID_IMediaControl,
        (LPVOID *)&pMediaControl);
    if(FAILED(hr)){
        _dbg(_T("MediaControl�C���^�[�t�F�[�X�擾���s\n"));
        return false;
    }

    // pMediaEventEx�C���^�[�t�F�[�X�擾
    hr = pGraphBuilder->QueryInterface(IID_IMediaEventEx,
        (LPVOID *)&pMediaEventEx);
    if(FAILED(hr)){
        _dbg(_T("pMediaEventEx�C���^�[�t�F�[�X�擾���s\n"));
        return false;
    }

    // �I�[�f�B�I�C���^�[�t�F�[�X�擾
    pGraphBuilder->QueryInterface(IID_IBasicAudio,
        (LPVOID *)&pBasicAudio);
    if(FAILED(hr)){
        _dbg(_T("BasicAudio�C���^�[�t�F�[�X�擾���s\n"));
        return false;
    }

    // DirectSoundFilter�𐶐�
    hr = CoCreateInstance(CLSID_DSoundRender,
        NULL,
        CLSCTX_INPROC,
        IID_IBaseFilter,
        (LPVOID *)&pDirectSound);
    if(FAILED(hr)){
        _dbg(_T("DirectSoundFilter�������s\n"));
        return false;
    }

    // CaptureGraphBuilder2�𐶐�
    hr = CoCreateInstance(CLSID_CaptureGraphBuilder2,
        NULL,
        CLSCTX_INPROC_SERVER, 
        IID_ICaptureGraphBuilder2, 
        (void **)&pCapBuilder);
    if(FAILED(hr)){
        _dbg(_T("CaptureGraphBuilder2�������s\n"));
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

// DirectShow�C���^�t�F�[�XCLOSE
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


// �T�E���h�Đ�(���łɍĐ����̂��̂͒�~)
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

    // �C�x���g�ʒm
    pMediaEventEx->SetNotifyWindow((OAHWND)hWndCtrl, WM_SND_NOTIFY, NULL);

    // �Đ��J�n
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
        case EC_COMPLETE:   // �Đ��I��
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

// �Đ����ɉ��ʕύX(�ۑ��Ȃ�)
void nicoalert_snd::setVolPlaying_thr(int vol){
    if(!bInitialized) return;
    if(!pBasicAudio) return;
    if(vol < 0) vol = 0;
    if(vol > 100) vol = 100;
    LONG lVol = -10000;
    if(vol > 0) lVol = (LONG)(log10(vol/100.0)*2000);  //vol�� �� dB�v�Z
    _dbg(_T("vol = %d, lVol = %ld\n"), vol, lVol);
    pBasicAudio->put_Volume(lVol); 
}


#ifdef _DEBUG
// �\�z�ς݃t�B���^�̕\��(debug)
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

// �S�t�B���^�̍폜
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


// -- �����t�֐� --


// ��������N���X������
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

// ���ʎ擾
int nicoalert_snd::getVol(void){
    return m_vol;
}
// ���ʕύX
void nicoalert_snd::setVol(int vol){
    if(vol < 0) vol = 0;
    if(vol > 100) vol = 100;
    m_vol = vol;
}

// �Đ����ɉ��ʕύX(�ۑ��Ȃ�)
void nicoalert_snd::setVolPlaying(int vol){
    if(!bInitialized) return;
    if(!hWndCtrl) return;

    m.lock();
    BOOL result = SendMessage(hWndCtrl, WM_SND_VOLSET, 0, (LPARAM)vol);
    m.unlock();
    return;
}
