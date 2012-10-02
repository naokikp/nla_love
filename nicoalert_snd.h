
#ifndef NICOALERT_SND_H
#define NICOALERT_SND_H

#include "nicoalert.h"

#pragma warning( disable : 4819 )
#include <dshow.h>
#pragma warning( default : 4819 )

#pragma comment(lib, "amstrmid.lib")

#define NICOALERT_SND_WORKERWND _T("snd_worker_class")

#define WM_SND_PLAY     (WM_APP+1)
#define WM_SND_STOP     (WM_APP+2)
#define WM_SND_VOLSET   (WM_APP+3)
#define WM_SND_CLOSE    (WM_APP+4)
#define WM_SND_NOTIFY   (WM_APP+5)


class nicoalert_snd {
private:
    bool bInitialized, bInitFailed;
    HWND hWndCtrl;
    IGraphBuilder *pGraphBuilder;
    IMediaControl *pMediaControl;
    IMediaEventEx *pMediaEventEx;
    IBasicAudio   *pBasicAudio;
    IBaseFilter   *pDirectSound;
    ICaptureGraphBuilder2 *pCapBuilder;

    mutex m;
    HANDLE hEventCtrl;

    int m_vol;

    bool open_thr(void);
    bool close_thr(void);
    bool play_thr(const TCHAR *);
    bool stop_thr(void);
    void notify(void);
    void setVolPlaying_thr(int);

#ifdef _DEBUG
    bool DbgAllFilter();
#endif
    bool RemoveAllFilter();

public:
    nicoalert_snd(void){
        bInitialized    = false;
        bInitFailed     = false;
        hWndCtrl        = NULL;
        pGraphBuilder   = NULL;
        pMediaControl   = NULL;
        pMediaEventEx   = NULL;
        pDirectSound    = NULL;
        pCapBuilder     = NULL;
        m_vol           = 100;
    }
    ~nicoalert_snd(void){ close(); }

    bool open(void);
    bool play(const TCHAR *);
    bool close(void);

    int getVol(void);
    void setVol(int);
    void setVolPlaying(int);

    void thread(void);
    LRESULT CALLBACK wndproc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);


};

#endif
