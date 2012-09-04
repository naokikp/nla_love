
#ifndef NICOALERT_SND_H
#define NICOALERT_SND_H

#include "nicoalert.h"

#pragma warning( disable : 4819 )
#include <dshow.h>
#pragma warning( default : 4819 )

#pragma comment(lib, "amstrmid.lib")

class nicoalert_snd {
private:
    bool bInitialized;
    IGraphBuilder *pGraphBuilder;
    IMediaControl *pMediaControl;
    IBasicAudio   *pBasicAudio;
    IBaseFilter   *pDirectSound;
    ICaptureGraphBuilder2 *pCapBuilder;

    int m_vol;

#ifdef _DEBUG
    bool DbgAllFilter();
#endif
    bool RemoveAllFilter();

public:
    nicoalert_snd(void){
        bInitialized    = false;
        pGraphBuilder   = NULL;
        pMediaControl   = NULL;
        pDirectSound    = NULL;
        pCapBuilder     = NULL;
        m_vol = 100;
    }
    ~nicoalert_snd(void);

    bool open(void);
    bool play(const TCHAR *);
    bool close(void);

    int getVol(void);
    void setVol(int);
    void setVolPlaying(int);

};

#endif
