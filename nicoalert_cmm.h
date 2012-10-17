
#ifndef NICOALERT_CMM_H
#define NICOALERT_CMM_H

#include "rapidxml/rapidxml.hpp"

#pragma comment(lib, "ws2_32.lib")

using namespace rapidxml;


#define ENTRANCE_API_SERVER         "live.nicovideo.jp"
#define ENTRANCE_API_PATH           "/api/getalertinfo"

// �R�~���j�e�B�� ���O�C���s�v �vHTML���
// http://com.nicovideo.jp/community/co0000
#define COMMUNITY_NAME_API_SERVER   "com.nicovideo.jp"
#define COMMUNITY_NAME_API_PATH     "/community/"

#define CHANNEL_NAME_API_SERVER     "ch.nicovideo.jp"
#define CHANNEL_NAME_API_PATH       "/channel/"

#define LVID_NAME_API_SERVER        "live.nicovideo.jp"
#define LVID_NAME_API_PATH          "/watch/"

// �A���[�g���(�J�n���琔�������擾�ł��Ȃ��͗l�H) ���O�C���s�vAPI
// http://live.nicovideo.jp/api/getstreaminfo/lv0000
#define ALERT_GETSTREAMINFO_API_SERVER   "live.nicovideo.jp"
#define ALERT_GETSTREAMINFO_API_PATH     "/api/getstreaminfo/"


#define CMM_ERROR_WAITTIME          15      // �G���[���ҋ@����(�b)
#define CMM_ERROR_WAITTIME_LIMIT    300     // �G���[���ҋ@���ԍő�l(�b)
#define CMM_ERROR_WAITTIME_RESET    360     // �G���[�J�E���g���Z�b�g����(�b)
#define CMM_MAINT_WAITTIME          1800    // �����e�i���X���ҋ@����(�b)
#define CMM_RECVHB_TIMEOUT          630     // �f�[�^��M�^�C���A�E�g(�b)
#define CMM_SENDHB_TIMEOUT          60      // �R�l�N�V�����ێ��Ď��^�C�}(�b)

#define LOCATION_HDR                "location:"
#define LOCATION_HTTP_PROTO         "http://"

enum cmm_errno {
    CMM_SUCCESS,
    CMM_CONN_FAIL,
    CMM_TRANS_FAIL,
    CMM_ILL_DATA,
    CMM_MAINTENANCE,
};

bool cmm_start( void(*)(const c_alertinfo *), void(*)(const TCHAR *) );
bool cmm_exit(void);

int nicoalert_getstreaminfo(tstring &lvid, c_streaminfo &si);
int nicoalert_getstreaminfo2(tstring &lvid);
int nicoalert_getkeyname(tstring &key, tstring &keyname);

int nicoalert_bouyomi(tstring &msg, int speed = -1, int tone = -1, int volume = -1, int voice = 0, int timeout = 500);

#endif
