//	�j�R���A���[�g(Love)
//	Copyright (C) 2012 naoki.kp

#include "nicoalert.h"
#include "nicoalert_bcc.h"
#include "nicoalert_cmm.h"
#include "nicoalert_snd.h"

// �A���[�g�ʒm�f�[�^�L���[
static deque_ts<c_alertinfo> alinfo;
// �A���[�g�ʒm�C�x���g
static HANDLE hBCSCEvent = NULL;
// �A���[�g�ʒm�ςݕ����ԍ��L���[
static deque < pair<time_t, tstring> > bcc_queue;

// �X���b�h�I���ʒm
static HANDLE hBCSCExitEvent = NULL;
static volatile bool isBCSCExit = false;

// �e��R�[���o�b�N
static void(*callback_alinfo_ntf)(unsigned int, BOOL) = NULL;
static BOOL(*callback_msgpopup)(const tstring &msg, const tstring &link) = NULL;
static void(*callback_msginfo)(const TCHAR *) = NULL;

static class nicoalert_snd nasnd;

#ifdef _DEBUG
static volatile int DEBUG_bcc_check_strcnt = 0;
static volatile int DEBUG_bcc_check_endcnt = 0;
static volatile int DEBUG_bcc_check_cnkcnt = 0;
static volatile int DEBUG_bcc_check_errcnt = 0;
#endif

// �A���[�g�ʒm��t(callback)
void alertinfo_ind(const c_alertinfo *ai){
    alinfo.lock();
    alinfo.push_back(*ai);
    SetEvent(hBCSCEvent);
    alinfo.unlock();
}

// �T�E���h�e�X�g��t
bool bcc_soundtest(const TCHAR *fn, int vol){
    nasnd.setVol(vol);
    return nasnd.play(fn);
}
void bcc_soundvol(int vol){
    nasnd.setVolPlaying(vol);
}

#if 0
// �����J�n���v��
static deque_ts<unsigned int> start_count;
const unsigned int start_count_period       =  15;  // �v���P��
const unsigned int start_count_period_num   = 120;  // �v���P�ʐ�(15s*120 = 30min.)
static bool bcc_countup(tstring &lvid){
    static unsigned int start_count_tick;               // �ŏI�v���P�ʎ���

    start_count.lock();
    if(start_count_tick != (now/start_count_period)){
        if(start_count.size() > start_count_period_num){
            start_count.pop_front();
        }
        start_count.push_back(0);
        start_count_tick = (now/start_count_period);
    }
    (*(start_count.end()-1))++;

    if(0){
        tstring tmp;
        TCHAR buf[256];
        FOREACH(it, start_count){
            _stprintf_s(buf, _T("%u "), *it);
            tmp += buf;
        }
        _dbg(_T("%s\n"), tmp.c_str());
    }
    start_count.unlock();

    return true;
}

bool bcc_startstat(unsigned int *count, unsigned int *period){
    unsigned int sum = 0, num = 0;
    start_count.lock();
    num = start_count.size()-1;
    for(unsigned int i = 0; i < num; i++){ sum += start_count[i]; }

    if(1){
        tstring tmp;
        TCHAR buf[256];
        FOREACH(it, start_count){
            _stprintf_s(buf, _T("%u "), *it);
            tmp += buf;
        }
        _dbg(_T("%s\n"), tmp.c_str());
    }
    start_count.unlock();

    *count = sum;
    *period = start_count_period * num;
    return true;
}
#endif

// �A���[�g�ʒm����
static void bcc_check(int infotype, tstring &lvid, unsigned int idx[3]){

    time_t now = time(NULL);

    // �ʒm�ςݕ����ԍ��̃f�[�^���폜(BCC_QUEUE_TIME sec�ێ�)
    while(bcc_queue.size() && bcc_queue.front().first + BCC_QUEUE_TIME < now){
        bcc_queue.pop_front();
    }
    // ���łɒʒm�ς݂̕����ԍ��̏ꍇ�ʒm���Ȃ�
    FOREACH(it, bcc_queue){
        if(it->second == lvid) return;
    }
    bcc_queue.push_back(make_pair(now, lvid));

    c_regdata mrd, *trd = NULL;
    regdata_info.lock();

    // �ʒm����������(�ő�3�̈�v�L�[����Y������ʒm������or���Z)
    unsigned int notify = 0;
    for(int i = 0; i < 3; i++){
        if(idx[i] == 0) continue;
        ITER(regdata_info) it = regdata_info.find(idx[i]);
        if(it == regdata_info.end()) continue;

        c_regdata *rd = &(it->second);
        if(rd->idx != idx[i]) continue;

        rd->last_lv = lvid;
        rd->last_start = now;
        if(rd->notify & NOTIFY_ENABLE){
            notify |= rd->notify;
        }
        // ���X�g�r���[�X�V�ʒm
        if(callback_alinfo_ntf){
            callback_alinfo_ntf(idx[i], TRUE);
        }

        if(!trd) trd = rd;
    }
    if(trd) mrd = *trd;

    regdata_info.unlock();

    // dbidx�������Ȃ������ꍇ(�K�[�h)
    if(mrd.key.size() == 0) return;

    // ���O�E�B���h�E�ɒʒm(�vregdata_info����)
    if(callback_msginfo){
        tstring msg;
        msg = mrd.key + _T(" : ") + lvid + _T(" �������J�n���܂����B");
        callback_msginfo(msg.c_str());
    }

    // �o���[���ʒm�r�b�g
    if((notify & NOTIFY_BALLOON)){
        tstring msg, msg2;
        switch(infotype){
        case INFOTYPE_OFFICIAL:
            msg += _T("[����������]");
            break;
        case INFOTYPE_CHANNEL:
            msg += _T("[�`�����l��������]");
            break;
        case INFOTYPE_USER:
            msg += _T("[���[�U�[������]");
            break;
        }
        msg2 = msg + _T(" ") + mrd.key_name + _T(" �������J�n���܂����B");
        //msg += _T("\n");
        msg = msg + _T(" (") + lvid + _T(")\n");
        msg = msg + mrd.key_name + _T("(") + mrd.key + _T(")") + _T(" �������J�n���܂����B\n");

        if(infotype != INFOTYPE_OFFICIAL){
            // �����������͎擾�G���[�ɂȂ邽�ߎ擾���Ȃ�
            c_streaminfo si;
            if(nicoalert_getstreaminfo(lvid, si) == CMM_SUCCESS){
                msg = msg + _T("�y") + si.title + _T("�z\n");
                msg = msg + si.desc + _T("\n");
                msg2 = msg2 + si.title + _T(" / ") + si.desc;
            }
        } else {
            // lv�w�肶��Ȃ������������͕����y�[�W����^�C�g�����擾
            if(mrd.key.substr(0, LVID_PREFIX_LEN) != _T(LVID_PREFIX)){
                int failcnt = 0;
                tstring keyname;
                while(1){
                    if(nicoalert_getkeyname(lvid, keyname) == CMM_SUCCESS) break;
                    keyname = _T("�����^�C�g���擾�Ɏ��s���܂����B");
                    //callback_msginfo(keyname.c_str());

                    failcnt++;
                    if(failcnt >= 5) break;
                    Sleep(1000*failcnt);
                }
                msg = msg + _T("�y") + keyname + _T("�z\n");
                msg2 = msg2 + keyname;
            }
        }

        if(callback_msginfo){
            callback_msginfo(msg2.c_str());
        }

        // �_�ǂ݂����
        if(ReadOptionInt(OPTION_BALLOON_BOUYOMI, DEF_OPTION_BALLOON_BOUYOMI)){
            _dbg(_T("bouyomi send start\n"));
            nicoalert_bouyomi(msg2);
            _dbg(_T("bouyomi send end\n"));
        }

        // �^�X�N�g���C�o���[���\�����s
        if(callback_msgpopup){
            trim_trailws(msg);
            callback_msgpopup(msg, lvid);
        }
    }

    // �T�E���h�ʒm�r�b�g
    if(notify & NOTIFY_SOUND){
        tstring &p = ReadOptionString(OPTION_SOUND_PATH, DEF_OPTION_SOUND_PATH);
        if(p == _T("")){
            // �T�E���h����`�̏ꍇ�A�V�X�e������
            PlaySound(_T("SystemAsterisk"), NULL, SND_ALIAS | SND_ASYNC | SND_NOWAIT);
        } else {
            nasnd.setVol(ReadOptionInt(OPTION_SOUND_VOL, DEF_OPTION_SOUND_VOL));
            nasnd.play(p.c_str());
        }
    }

    // �u���E�U�ʒm�r�b�g
    if(notify & NOTIFY_BROWSER){
        // �Y����������y�[�W���J��
        if(!exec_browser_by_key(lvid)){
            if(callback_msginfo){
                callback_msginfo(_T("�u���E�U�N���Ɏ��s���܂����B"));
            }
        }
    }

    // �O���A�v���ʒm�r�b�g
    if(notify & NOTIFY_EXTAPP){
        int ret;
        tstring &p = ReadOptionString(OPTION_EXTAPP_PATH, _T(""));
        tstring opt;
        if(p == _T("")){
            callback_msginfo(_T("�O���A�v�����w�肳��Ă��܂���B"));
        } else {
            splitpath_opt(p, opt);
            opt += _T(" ");
            opt += lvid;

            ret = (int)ShellExecute(NULL, _T("open"), p.c_str(), opt.c_str(), NULL, SW_SHOWNORMAL);
            if(ret < 32){
                if(callback_msginfo){
                    callback_msginfo(_T("�O���A�v���N���Ɏ��s���܂����B"));
                }
            }
        }
    }
}


// �A���[�g�ʒm�f�[�^�����X���b�h
static void bcc_check_thread(void *arg){
    while(!isBCSCExit){
        WaitForSingleObject(hBCSCEvent, INFINITE);

        _DEBUGDO(DEBUG_bcc_check_strcnt++);
        while(!isBCSCExit){

            // �A���[�g�ʒm�f�[�^�L���[����f�L���[
            c_alertinfo ai;
            alinfo.lock();
            if(alinfo.size()){
                ai = alinfo.front();
                alinfo.pop_front();
            } else {
                alinfo.unlock();
                break;
            }
            alinfo.unlock();

            // keepalive�͖���
            if(ai.infotype == INFOTYPE_NODATA) continue;

            unsigned int idx[3];
            idx[0] = idx[1] = idx[2] = 0;

            regdata_hash.lock();

            // �A���[�g�ʒm��ʂ��Ƃɓo�^�L�[���`�F�b�N
            //  ������:       �����ԍ��A���[�UID = user/0
            //  �`�����l����: �����ԍ��A�`�����l���ԍ��A���[�UID
            //  ���[�U�[��:   �����ԍ��A�R�~���j�e�B�ԍ��A���[�UID
            ITER(regdata_hash) it;
            it = regdata_hash.find(ai.lvid);
            if(it != regdata_hash.end()) idx[0] = it->second;
            it = regdata_hash.find(ai.usrid);
            if(it != regdata_hash.end()) idx[1] = it->second;

            switch(ai.infotype){
            case INFOTYPE_OFFICIAL:
                break;
            case INFOTYPE_CHANNEL:
                it = regdata_hash.find(ai.chid);
                if(it != regdata_hash.end()) idx[2] = it->second;
                break;
            case INFOTYPE_USER:
                it = regdata_hash.find(ai.coid);
                if(it != regdata_hash.end()) idx[2] = it->second;
                break;
            }

            regdata_hash.unlock();

            // �o�^�L�[�Ƀq�b�g�����ꍇ�A�ʒm�������{
            if(idx[0] != 0 || idx[1] != 0 || idx[2] != 0){
                bcc_check(ai.infotype, ai.lvid, idx);
            }
        }
        _DEBUGDO(DEBUG_bcc_check_endcnt++);
    }

    bcc_queue.clear();
    // �X���b�h�I���ʒm
    SetEvent(hBCSCExitEvent);
}

// bcc�X���b�h�N��
bool bcc_start(
    void(*cb_alinfo_ntf)(unsigned int, BOOL),
    BOOL(*cb_msgpopup)(const tstring &msg, const tstring &link),
    void(*cb_msginfo)(const TCHAR *) )
{

    callback_alinfo_ntf = cb_alinfo_ntf;
    callback_msgpopup = cb_msgpopup;
    callback_msginfo = cb_msginfo;

    hBCSCEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    hBCSCExitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    isBCSCExit = false;

    if(-1 == _beginthread(bcc_check_thread, 0, NULL)){
        return false;
    }
    return true;
}

// bcc�X���b�h��~
bool bcc_exit(void){
    int waitcount = PROCESS_EXIT_WAITCOUNT;
    isBCSCExit = true;
    DWORD ret;

    callback_alinfo_ntf = NULL;
    callback_msgpopup = NULL;
    callback_msginfo = NULL;

    while(waitcount){
        SetEvent(hBCSCEvent);
        doevent();
        ret = WaitForSingleObject(hBCSCExitEvent, 500);
        if(ret == WAIT_OBJECT_0) break;
        waitcount--;
    }

    nasnd.close();

    alinfo.lock();
    alinfo.clear();
    alinfo.unlock();

    return true;
}

bool bcc_check(void){
#ifdef _DEBUG
    if(DEBUG_bcc_check_strcnt != DEBUG_bcc_check_endcnt){
        if(DEBUG_bcc_check_strcnt == DEBUG_bcc_check_cnkcnt){
            DEBUG_bcc_check_errcnt++;
        }
        DEBUG_bcc_check_cnkcnt = DEBUG_bcc_check_strcnt;
    } else {
        DEBUG_bcc_check_errcnt = 0;
    }
    if(DEBUG_bcc_check_errcnt >= 3){
        return false;
    }
#endif
    return true;
}