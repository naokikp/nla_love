//	ニコ生アラート(Love)
//	Copyright (C) 2012 naoki.kp

#include "nicoalert.h"
#include "nicoalert_ole.h"

// OLE D&D制御クラス(IDropTarget継承)

CDropTarget::CDropTarget(HWND hWnd, UINT uCallbackMsg){
	m_Ref = 1;
	m_hWnd = hWnd;
    m_uCallbackMsg = uCallbackMsg;
}

CDropTarget::~CDropTarget(){
    // no action
}

STDMETHODIMP CDropTarget::QueryInterface(REFIID riid, void **ppvObject){
	*ppvObject = NULL;
	if(IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDropTarget)){
		*ppvObject = (IDropTarget *)this;
    } else {
		return E_NOINTERFACE;
    }
	AddRef();
	return S_OK;
}

STDMETHODIMP_(ULONG) CDropTarget::AddRef(){
	return InterlockedIncrement(&m_Ref);
}

STDMETHODIMP_(ULONG) CDropTarget::Release(){
	if(InterlockedDecrement(&m_Ref) == 0){
		delete this;
		return 0;
	}
	return m_Ref;
}

#ifdef _DEBUG
void EnumSupportedFormat(IDataObject *pDataObj){
    HRESULT   hr;
    FORMATETC formatetc;

    for(int i = 1; i < CF_MAX; i++){
        formatetc.cfFormat = i;
        formatetc.ptd      = NULL;
        formatetc.dwAspect = DVASPECT_CONTENT;
        formatetc.lindex   = -1;
        formatetc.tymed    = TYMED_HGLOBAL;

        hr = pDataObj->QueryGetData(&formatetc);
        if(hr == S_OK){
            _dbg(_T("format %d support\n"), i);
        }
    }
}
#endif

STDMETHODIMP CDropTarget::DragEnter(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect){
	HRESULT   hr;
	FORMATETC formatetc;

    _dbg(_T("CDropTarget::DragEnter()\n"));

#ifdef _DEBUG
    EnumSupportedFormat(pDataObj);
#endif

    formatetc.cfFormat = NICOALERT_SUPPORTED_FORMAT;
	formatetc.ptd      = NULL;
	formatetc.dwAspect = DVASPECT_CONTENT;
	formatetc.lindex   = -1;
	formatetc.tymed    = TYMED_HGLOBAL;

    // DROP対象がサポートフォーマットか調べる
	hr = pDataObj->QueryGetData(&formatetc);
	if(hr == S_OK){
		m_bSupportFormat = TRUE;
	} else {
		m_bSupportFormat = FALSE;
		*pdwEffect = DROPEFFECT_NONE;
    }
	return S_OK; 
}

STDMETHODIMP CDropTarget::DragLeave(){
    _dbg(_T("CDropTarget::DragLeave()\n"));
	return S_OK;
}

STDMETHODIMP CDropTarget::DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect){
	if(!m_bSupportFormat){
		*pdwEffect = DROPEFFECT_NONE;
    }
	return S_OK;
}


// DROP確定時の処理
STDMETHODIMP CDropTarget::Drop(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect){
	HRESULT   hr;
	FORMATETC formatetc;
	STGMEDIUM medium;
	
    _dbg(_T("CDropTarget::Drop()\n"));
	*pdwEffect = DROPEFFECT_COPY;

    formatetc.cfFormat = NICOALERT_SUPPORTED_FORMAT;
	formatetc.ptd      = NULL;
	formatetc.dwAspect = DVASPECT_CONTENT;
	formatetc.lindex   = -1;
	formatetc.tymed    = TYMED_HGLOBAL;

    // DROPデータを取得
	hr = pDataObj->GetData(&formatetc, &medium);
	if(FAILED(hr)){
		*pdwEffect = DROPEFFECT_NONE;
		return E_FAIL;
    }

    // 選択部分のDROPなら選択範囲の文字列、
    // ハイパーリンクのDROPならURLが文字列として入っている
    TCHAR *p = (TCHAR *)GlobalLock(medium.hGlobal);
    if(p){
        _dbg(_T("GlobalSize: %u\n"), GlobalSize(medium.hGlobal));
        _dbg(_T("%s\n"), p);
        SendMessage(m_hWnd, m_uCallbackMsg, (WPARAM)p, 0);

        GlobalUnlock(medium.hGlobal);
    }
	return S_OK;
}
