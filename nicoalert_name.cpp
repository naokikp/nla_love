//	�j�R���A���[�g(Love)
//	Copyright (C) 2012 naoki.kp

#include "nicoalert.h"
#include "nicoalert_name.h"
#include "nicoalert_cmm.h"

// �L�[���̎擾�C�x���g
static HANDLE hConEvent = NULL;

// �X���b�h�I���ʒm
static HANDLE hConExitEvent = NULL;
static volatile bool isConExit = false;

// �e��R�[���o�b�N
static void(*callback_alinfo_ntf)(unsigned int, BOOL) = NULL;
static void(*callback_msginfo)(const TCHAR *) = NULL;

// �L�[���̎擾�v���C���f�b�N�X�L���[
static deque_ts<unsigned int> acq_dbidx;
// �擾�^�C�~���O����p�L���[
static deque<time_t> acq_check;


static void keyname_acq_nummsg(unsigned int num, bool flag){
    if(!flag){
        if(num >= 100 && (num % 100) != 0) return;
        if(num < 100 && (num % 10) != 0) return;
    }
    if(callback_msginfo){
        if(num == 0){
            if(!flag){
                callback_msginfo(_T("�R�~��/�`�����l��/������ �擾����"));
            }
        } else {
            TCHAR msg[128];
            _stprintf_s(msg, _T("�R�~��/�`�����l��/�������擾��: �c%d��"), num);
            callback_msginfo(msg);
        }
    }
}

// �L�[���̎擾�X���b�h
static void keyname_acq_thread(void *arg){
    while(!isConExit){
        WaitForSingleObject(hConEvent, INFINITE);

        while(!isConExit){
            if(ReadOptionInt(OPTION_AUTO_KEYNAME_ACQ, DEF_OPTION_AUTO_KEYNAME_ACQ) == 0) break;

            // �L�[���̎擾�v���C���f�b�N�X�L���[����f�L���[
            unsigned int dbidx;
            acq_dbidx.lock();
            _dbg(_T("acq_dbidx.size() = %d\n"), acq_dbidx.size());
            if(acq_dbidx.size()){
                dbidx = acq_dbidx.front();
                acq_dbidx.pop_front();
            } else {
                acq_dbidx.unlock();
                break;
            }
            acq_dbidx.unlock();

            // �o�^�A�C�e��DB���������A
            // �����擾�ς݃t���O�������Ă���ꍇ�͉������Ȃ�
            tstring key = _T("");
            tstring keyname;
            regdata_info.lock();
            ITER(regdata_info) it = regdata_info.find(dbidx);
            if(it != regdata_info.end()){
                if(!(it->second.notify & NOTIFY_AUTOCOACQ)){
                    key = it->second.key;
                }
            }
            regdata_info.unlock();
            if(key == _T("")){
                keyname_acq_nummsg(acq_dbidx.size(), false);
                continue;
            }

            // �L�[���̎擾�v��
            if(nicoalert_getkeyname(key, keyname) != CMM_SUCCESS){
                keyname = _T("<�擾���s>");
            }

            // �擾�����L�[���̂���������ꍇ �K�蒷�Őؒf�� ...��t����
            if(keyname.size() > REGDATA_KEYNAME_MAXLEN){
                keyname.replace(REGDATA_KEYNAME_MAXLEN-3, keyname.size(), _T("..."));
            }

            // �o�^�A�C�e��DB�ɕۑ�(�擾�ς݃t���OON)
            regdata_info.lock();
            it = regdata_info.find(dbidx);
            if(it != regdata_info.end()){
                if(it->second.key_name == _T("")){
                    it->second.key_name = keyname;
                }
                it->second.notify |= NOTIFY_AUTOCOACQ;
            }
            regdata_info.unlock();

            _dbg(_T("key:%s : %s\n"), key.c_str(), keyname.c_str());

            // ���C���E�B���h�E�̃��X�g�r���[�X�V
            if(callback_alinfo_ntf){
                callback_alinfo_ntf(dbidx, FALSE);
            }
            keyname_acq_nummsg(acq_dbidx.size(), false);

            // �����擾�Ԋu�̐���
            //  �擾���� ACQ_CHECK_COUNT �ɒB����܂ł͍Œ�wait(1�b)
            //  ����ȏ�̏ꍇ�AACQ_CHECK_TIME_SL �b�Ԋu�ɐ���
            //  ACQ_CHECK_TIME �b�ȏ�o�߂���Ǝ擾���J�E���g�L���[���甲����
            while(!isConExit){
                if(acq_check.size() >= ACQ_CHECK_COUNT){
                    if(acq_check.front() + ACQ_CHECK_TIME < time(NULL)){
                        acq_check.pop_front();
                    }
                    if(acq_check.size()){
                        if(acq_check.back() + ACQ_CHECK_TIME_SL < time(NULL)){
                            break;
                        }
                    }
                } else {
                    break;
                }
                Sleep(500);
            }
            acq_check.push_back(time(NULL));
            Sleep(1000);

        }
    }

    acq_check.clear();
    // �X���b�h�I���ʒm
    SetEvent(hConExitEvent);
}

// �����擾�����̊J�n
void con_start(void){
    if(ReadOptionInt(OPTION_AUTO_KEYNAME_ACQ, DEF_OPTION_AUTO_KEYNAME_ACQ)){
        keyname_acq_nummsg(acq_dbidx.size(), true);
        SetEvent(hConEvent);
    }
}

// �����擾�����L���[�ɒǉ�(IN: dbidx)
void con_add(unsigned int mindbidx){
    acq_dbidx.lock();
    regdata_info.lock();
    FOREACH(it, regdata_info){
        if( it->second.idx >= mindbidx &&
            !(it->second.notify & NOTIFY_AUTOCOACQ) )
        {
            acq_dbidx.push_back(it->second.idx);
        }
    }
    regdata_info.unlock();
    acq_dbidx.unlock();
    con_start();
}

// �����擾�����X���b�h�N��
bool con_start( void(*cb_alinfo_ntf)(unsigned int, BOOL), void(*cb_msginfo)(const TCHAR *) ){
    callback_alinfo_ntf = cb_alinfo_ntf;
    callback_msginfo = cb_msginfo;

    hConEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    hConExitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    isConExit = false;

    if(-1 == _beginthread(keyname_acq_thread, 0, NULL)){
        return false;
    }
    con_add(0);

    return true;
}

// �����擾�����X���b�h�I��
bool con_exit(void){
    int waitcount = PROCESS_EXIT_WAITCOUNT;
    isConExit = true;
    DWORD ret;

    callback_alinfo_ntf = NULL;
    callback_msginfo = NULL;

    while(waitcount){
        SetEvent(hConEvent);
        doevent();
        ret = WaitForSingleObject(hConExitEvent, 500);
        if(ret == WAIT_OBJECT_0) break;
        waitcount--;
    }

    acq_dbidx.lock();
    acq_dbidx.clear();
    acq_dbidx.unlock();

    return true;
}
