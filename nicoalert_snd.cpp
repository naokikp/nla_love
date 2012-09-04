//	�j�R���A���[�g(Love)
//	Copyright (C) 2012 naoki.kp

#include "nicoalert_snd.h"

// ��������N���X

nicoalert_snd::~nicoalert_snd(void){
    close();
}

// DirectShow�C���^�t�F�[�XOPEN
bool nicoalert_snd::open(void){
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

    bInitialized = true;
    return true;
}

// DirectShow�C���^�t�F�[�XCLOSE
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

// �T�E���h�Đ�(���łɍĐ����̂��̂͒�~)
// TODO: �Đ��I������t�B���^���c�葱����̂��Ȃ�Ƃ�����B
//       ��ffdshow�g���Ă�ƃA�C�R�����c��
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

    // �Đ��J�n
    pMediaControl->Run();

    return true;
}
