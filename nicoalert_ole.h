
#ifndef NICOALERT_OLE_H
#define NICOALERT_OLE_H

#ifdef _UNICODE
#define NICOALERT_SUPPORTED_FORMAT  CF_UNICODETEXT
#else
#define NICOALERT_SUPPORTED_FORMAT  CF_TEXT
#endif

class CDropTarget : public IDropTarget
{
public:
	STDMETHODIMP QueryInterface(REFIID riid, void **ppvObject);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();
	
	STDMETHODIMP DragEnter(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect);
	STDMETHODIMP DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect);
	STDMETHODIMP DragLeave();
	STDMETHODIMP Drop(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect);

	CDropTarget(HWND hWnd, UINT uCallbackMsg);
	~CDropTarget();

private:
	LONG m_Ref;
	HWND m_hWnd;
    UINT m_uCallbackMsg;

	BOOL m_bSupportFormat;
};


#endif
