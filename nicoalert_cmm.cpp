//	�j�R���A���[�g(Love)
//	Copyright (C) 2012 naoki.kp

#include "nicoalert.h"
#include "nicoalert_cmm.h"

// �X���b�h�I���ʒm
static HANDLE hCmmExitEvent = NULL;
static volatile bool isCmmExit = false;

// �e��R�[���o�b�N
static void(*callback_alertinfo)(const c_alertinfo *) = NULL;
static BOOL(*callback_msgpopup)(const tstring &msg, const tstring &link) = NULL;
static void(*callback_msginfo)(const TCHAR *) = NULL;


// �}���`�o�C�g�p�f�o�b�O�o��
static void _dbg_mb(const char *fmt, ...){
#ifdef _DEBUG
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf_s(buf, sizeof(buf), sizeof(buf)-1, fmt, ap);
    OutputDebugStringA(buf);
    va_end(ap);
#endif
}

// ���C���E�B���h�E�ʒm(�}���`�o�C�g�p)
static void _notice(int mode, const char *fmt, ...){
    char buf[1024];
    TCHAR tbuf[1024];
    int ret;
    va_list ap;
    va_start(ap, fmt);
    vsprintf_s(buf, fmt, ap);

    ret = MultiByteToWideChar(CP_THREAD_ACP, MB_PRECOMPOSED, buf, -1, tbuf, ESIZEOF(tbuf));
    if(ret){
        if(mode&1 && callback_msginfo) callback_msginfo(tbuf);
        if(mode&2 && callback_msgpopup) callback_msgpopup(tbuf, _T(""));
        if(mode&4 && callback_msgpopup) callback_msgpopup(tbuf, NICOALERT_ONLINE_HELP);
    }

    va_end(ap);
}
#define notice(...)             _notice(1,__VA_ARGS__)
#define notice_popup(...)       _notice(3,__VA_ARGS__)
#define notice_popup_link(...)  _notice(5,__VA_ARGS__)


// HTML�G���R�[�f�B���O�̃f�R�[�h
static void htmldecode(tstring &str){
    const TCHAR *(dec[])[2] = {
        { _T("&amp;"), _T("&"), },
        { _T("&lt;"), _T("<"), },
        { _T("&gt;"), _T(">"), },
        { _T("&quot;"), _T("\""), },
        { _T("&nbsp;"), _T("\xA0"), },
        { _T("&copy;"), _T("\xA9"), },
    };
    tstring out, in = str;
    int loc_amp, loc_sc, len;
    while(1){
        loc_amp = in.find_first_of(_T("&"));
        if(loc_amp == string::npos){
            out += in; break;
        }
        if(loc_amp > 0){
            out += in.substr(0, loc_amp);
            in.replace(0, loc_amp, _T(""));
        }
        loc_sc = in.find_first_of(_T(";"));
        if(loc_sc == string::npos){
            out += in; break;
        }
        len = loc_sc + 1;
        bool flag = false;
        for(int i = 0; i < ESIZEOF(dec); i++){
            if(in.substr(0, len).compare(dec[i][0]) == 0){
                out += dec[i][1];
                flag = true;
                break;
            }
        }
        if(!flag && in[1] == '#'){
            int num = 0;
            TCHAR *end = NULL;
            if(in[2] == 'x' || in[2] == 'X'){
                num = _tcstol(in.c_str() + 3, &end, 16);
            } else {
                num = _tcstol(in.c_str() + 2, &end, 10);
            }
            if(*end == ';'){
                out += num;
            }
        }
        in.replace(0, len, _T(""));
    }
    str = out;
}

// getalertstatus API��XML���
bool xml_getalertstatus(char *xmlbody, string &status, string &addr, string &port, string &thread){

    xml_document<> doc;
    try {
        doc.parse<0>(xmlbody);
    }
    catch(parse_error& err) {
        _dbg_mb("%s %s\n", err.what(), err.where<char *>());
        return false;
    }

    // <getalertstatus status="">
    xml_node<> *node = doc.first_node("getalertstatus");
    if(!node){
        return false;
    }
    for(xml_attribute<> *attr = node->first_attribute(); attr; attr = attr->next_attribute()){
        if(strcmp(attr->name(), "status") == 0){
            status = attr->value();
        }
    }

    // <getalertstatus><ms>
    xml_node<> *child = node->first_node("ms");
    if(!child){
        return false;
    }

    // <getalertstatus><ms><addr>
    xml_node<> *cchild;
    cchild = child->first_node("addr");
    if(!cchild){
        return false;
    }
    addr = cchild->value();

    // <getalertstatus><ms><port>
    cchild = child->first_node("port");
    if(!cchild){
        return false;
    }
    port = cchild->value();

    // <getalertstatus><ms><thread>
    cchild = child->first_node("thread");
    if(!cchild){
        return false;
    }
    thread = cchild->value();

    return true;
}

#if 0
/*
�y��M�f�[�^�̎�ށz
0. �w�b�_
<thread resultcode="0" thread="1000000019" last_res="41612160" ticket="0xa035240" revision="1" server_time="1343238849"/>
�J�n����ŏ��ɔ��ł���

1. KeepAlive
<chat thread="1000000019" no="41621747" date="1343259541" user_id="394" premium="2">1343259541</chat>
60�b�ԕ����J�n�������ꍇ�ɔ��ł���                                                ���ݎ���

2. ���[�U�[������
<chat thread="1000000019" no="41612160" date="1343238849" user_id="394" premium="2">10xxxxxxx,coxxxxxx,xxxxxxxx</chat>
���[�U�[�������J�n���B�܂�ɓ���f�[�^��2���M���邱�Ƃ�����B                     �����ԍ�  co�ԍ�   ������ID

3. �`�����l��������
<chat thread="1000000019" no="41626008" date="1343267824" user_id="394" premium="2">10xxxxxxx,chxxxxx,xxxxxxxx</chat>
�`�����l���������J�n���B                                                            �����ԍ�  ch�ԍ�  ������ID

4. ����������(1)
<chat thread="1000000019" no="41621787" date="1343260866" user_id="394" premium="2">10xxxxxxx,official,394</chat>
-                                                                                   �����ԍ�  �Œ�     ������ID�H
5. ����������(2)
<chat thread="1000000019" no="41625715" date="1343267403" user_id="394" premium="2">10xxxxxxx,�y7��26���ߑO�z�������[���� �L�҉�@�c</chat>
-                                                                                   �����ԍ�   �����^�C�g��
*/
#endif

// alertinfo API��XML���
bool xml_alertinfo(char *xmlbody, c_alertinfo *ai){
    xml_document<> doc;
    try {
        doc.parse<0>(xmlbody);
    }
    catch(parse_error& err) {
        _dbg_mb("%s %s\n", err.what(), err.where<char *>());
        return false;
    }

    // <thread>
    xml_node<> *node = doc.first_node("thread");
    if(node){
        ai->infotype = INFOTYPE_NODATA;
        return true;
    }

    // <chat></chat>
    node = doc.first_node("chat");
    if(!node){
        _dbg_mb("xml parse : <%s> node not found\n", "chat");
        return false;
    }
    char *chatdata = node->value();
    char *context;
    char *tok1 = strtok_s(chatdata, ",", &context);
    char *tok2 = strtok_s(NULL, ",", &context);
    char *tok3 = strtok_s(NULL, ",", &context);

    if(tok2 == NULL){
        ai->infotype = INFOTYPE_NODATA;
        return true;
    }
    ai->lvid = _T(LVID_PREFIX);
    ai->lvid += mb2ts(tok1);

    if(strncmp(tok2, COID_PREFIX, COID_PREFIX_LEN) == 0){
        ai->infotype = INFOTYPE_USER;
        ai->coid = mb2ts(tok2);
        ai->usrid = _T(USERID_PREFIX);
        ai->usrid += mb2ts(tok3);
        return true;
    }
    if(strncmp(tok2, CHID_PREFIX, CHID_PREFIX_LEN) == 0){
        ai->infotype = INFOTYPE_CHANNEL;
        ai->chid = mb2ts(tok2);
        ai->usrid = _T(USERID_PREFIX);
        ai->usrid += mb2ts(tok3);
        return true;
    }

    ai->infotype = INFOTYPE_OFFICIAL;
    ai->usrid = _T(USERID_PREFIX) _T("0");
    return true;
}

// getstreaminfo API��XML���
bool xml_getstreaminfo(char *xmlbody, c_streaminfo &si){

    xml_document<> doc;
    try {
        doc.parse<0>(xmlbody);
    }
    catch(parse_error& err) {
        _dbg_mb("%s %s\n", err.what(), err.where<char *>());
        return false;
    }

    // <getstreaminfo status="">
    xml_node<> *node = doc.first_node("getstreaminfo");
    if(!node){
        _dbg_mb("xml parse : <%s> node not found\n", "getstreaminfo");
        return false;
    }
    xml_attribute<> *attr = node->first_attribute("status");
    if(!attr){
        _dbg_mb("xml parse : <%s> attribute not found\n", "status");
        return false;
    }
    if(strcmp(attr->value(), "ok") != 0){
        _dbg_mb("xml parse : status = %s\n", attr->value());
        return false;
    }

    // <getstreaminfo><streaminfo>
    xml_node<> *child = node->first_node("streaminfo");
    if(!child){
        _dbg_mb("xml parse : <%s> node not found\n", "streaminfo");
        return false;
    }

    xml_node<> *cchild;

    // <getstreaminfo><streaminfo><title>
    cchild = child->first_node("title");
    if(!child){
        _dbg_mb("xml parse : <%s> node not found\n", "title");
        return false;
    }
    si.title = mb2ts(cchild->value());

    // <getstreaminfo><streaminfo><description>
    cchild = child->first_node("description");
    if(!child){
        _dbg_mb("xml parse : <%s> node not found\n", "description");
        return false;
    }
    si.desc = mb2ts(cchild->value());

    htmldecode(si.title);
    htmldecode(si.desc);

    // <getstreaminfo><streaminfo><provider_type>
    cchild = child->first_node("provider_type");
    if(!child){
        _dbg_mb("xml parse : <%s> node not found\n", "provider_type");
        return false;
    }
    return true;
}

// TCP�R�l�N�V�����ڑ�
//  �߂�l: SOCKET-ID / INVALID_SOCKET
static SOCKET tcp_connect(const char *host, const char *port){

    if(isCmmExit) return INVALID_SOCKET;

    ADDRINFO addrinfo;
    memset(&addrinfo, 0, sizeof(addrinfo));
    addrinfo.ai_family = AF_INET;
    addrinfo.ai_socktype = SOCK_STREAM;
    addrinfo.ai_protocol = IPPROTO_TCP;

    SOCKET s = INVALID_SOCKET;
    LPADDRINFO lpaddrinfo = NULL;
    bool error_flag = true;
    int ret;

    do {
        // DNS�₢���킹
        ret = getaddrinfo(host, port, &addrinfo, &lpaddrinfo);
        if(ret) break;

#ifdef _DEBUG
        sockaddr_in *sa = (sockaddr_in *)lpaddrinfo->ai_addr;
#endif

        s = socket(lpaddrinfo->ai_family, lpaddrinfo->ai_socktype, lpaddrinfo->ai_protocol);
        if(s == SOCKET_ERROR) break;

        ret = connect(s, lpaddrinfo->ai_addr, lpaddrinfo->ai_addrlen);
        if(ret == SOCKET_ERROR) break;

        error_flag = false;
    } while(0);

    if(lpaddrinfo) freeaddrinfo(lpaddrinfo);
    if(error_flag && s != INVALID_SOCKET){
        closesocket(s);
        s = INVALID_SOCKET;
    }
    return s;
}


// HTTP�y�[�W�擾
static int tcp_gethttp(SOCKET s, string &http_request, char *buf, unsigned int bufsize){

    if(isCmmExit) return -1;

    bool recv_err = true;
    int respdatalen = 0;
    if(bufsize == 0) return 0;

    // �^�C���A�E�g�Ƃ��G���[�������C�}�C�`
    do {
        int ret;
        ret = send(s, http_request.c_str(), http_request.length(), 0);
        if(ret < 0) break;
        while(1){
            if(bufsize == 0){
                recv_err = false;
                break;
            }
            ret = recv(s, buf, bufsize, 0);
            if(ret < 0) break;
            if(ret == 0){
                recv_err = false;
                break;
            }
            unsigned int len = ret;
            if(len > bufsize) len = bufsize;
            respdatalen += len;
            buf += len;
            bufsize -= len;
        }
    } while(0);

    if(recv_err) return -1;
    return respdatalen;
}

// �w�肵���T�[�o/�A�h���X��HTML�y�[�W���擾
// (3��܂Ń��_�C���N�V�����Ή�)
static int gethtml(const char *server, const char *urlpath, char *respdata, unsigned int respdatasize){
    char redirectserver[_MAX_PATH];
    char redirecturl[_MAX_PATH];
    int redirectcnt = 3;
    SOCKET s = INVALID_SOCKET;
    int recvlen = -1;

    while(redirectcnt--){

        // �ΏۃT�[�o�ɐڑ�
        s = tcp_connect(server, "http");
        if(s == INVALID_SOCKET){
            return -1;
        }

        // HTTP���N�G�X�g����
        string http_request;
        char buf[128];
        sprintf_s(buf, "GET %s HTTP/1.0\r\n", urlpath);
        http_request += buf;
        sprintf_s(buf, "Host: %s\r\n", server);
        http_request += buf;
        sprintf_s(buf, "User-Agent: %s\r\n", UA_STRING);
        http_request += buf;
        sprintf_s(buf, "\r\n");
        http_request += buf;

        // ���N�G�X�g���M/���X�|���X�擾
        recvlen = tcp_gethttp(s, http_request, respdata, respdatasize);

        closesocket(s); s = INVALID_SOCKET;
        if(recvlen < 0){
            return -1;
        }
        respdata[recvlen] = '\0';

        const char *htmlbody = strstr(respdata, "\r\n\r\n");
        if(htmlbody == NULL){
            // HTML�w�b�_�Ȃ�
            notice("�T�[�o����\�����Ȃ��f�[�^����M���܂����B");
            return -1;
        }
        unsigned int httphdrsize = htmlbody - respdata + 4;

        char *httphdr = new char[httphdrsize+1];
        memcpy(httphdr, respdata, httphdrsize);
        httphdr[httphdrsize] = '\0';

        // Location:�w�b�_���
        bool fRedirect = false;
        char *tok, *context = NULL;
        tok = strtok_s(httphdr, "\r\n", &context);
        do {
            _dbg_mb("tok = %s\n", tok);
            if(_strnicmp(tok, LOCATION_HDR, strlen(LOCATION_HDR)) == 0){
                tok += strlen(LOCATION_HDR);
                while((*tok) == ' ') tok++;
                _dbg_mb("location: %s\n", tok);

                if(_strnicmp(tok, LOCATION_HTTP_PROTO, strlen(LOCATION_HTTP_PROTO)) == 0){
                    // "http://"�t�����_�C���N�V����
                    // "http://"�ȍ~��"/"�܂ł��T�[�o���A����ȍ~���p�X�Ƃ��Ē��o
                    tok += strlen(LOCATION_HTTP_PROTO);
                    char *pathdelim = strchr(tok, '/');
                    if(pathdelim){
                        strncpy_s(redirecturl, pathdelim, sizeof(redirecturl));
                        *pathdelim = '\0';
                    } else {
                        strncpy_s(redirecturl, "/", sizeof(redirecturl));
                    }
                    strncpy_s(redirectserver, tok, sizeof(redirectserver));
                    urlpath = redirecturl;
                    server = redirectserver;
                } else {
                    // �p�X�̂݃��_�C���N�V����
                    // �ڑ��T�[�o�ύX�Ȃ��A�p�X�̂ݏ��������B
                    strncpy_s(redirecturl, tok, sizeof(redirecturl));
                    urlpath = redirecturl;
                }

                fRedirect = true;
                break;
            }
        } while( (tok = strtok_s(NULL, "\r\n", &context)) != NULL);
        delete httphdr;

        if(!fRedirect) break;
    }

    return recvlen;
}


// getstreaminfo API�f�[�^�擾
int nicoalert_getstreaminfo(tstring &lvid, c_streaminfo &si){

    if(isCmmExit) return CMM_CONN_FAIL;

    // �A���[�g���(�J�n���琔�������擾�ł��Ȃ��͗l�H) ���O�C���s�vAPI
    // http://live.nicovideo.jp/api/getstreaminfo/lv0000

    char respdata[1024*4];
    unsigned int respdatalen = 0;
    SOCKET s = INVALID_SOCKET;

    s = tcp_connect(ALERT_GETSTREAMINFO_API_SERVER, "http");
    if(s == INVALID_SOCKET){
        return CMM_CONN_FAIL;
    }

    string http_request;
    char buf[128];
    sprintf_s(buf, "GET %s%s HTTP/1.0\r\n", ALERT_GETSTREAMINFO_API_PATH, ts2mb(lvid).c_str());
    http_request += buf;
    sprintf_s(buf, "Host: %s\r\n", ALERT_GETSTREAMINFO_API_SERVER);
    http_request += buf;
    sprintf_s(buf, "User-Agent: %s\r\n", UA_STRING);
    http_request += buf;
    sprintf_s(buf, "\r\n");
    http_request += buf;

    int recvlen = tcp_gethttp(s, http_request, respdata, sizeof(respdata)-1);

    closesocket(s); s = INVALID_SOCKET;
    if(recvlen < 0){
        return CMM_TRANS_FAIL;
    }
    respdata[recvlen] = '\0';

    char *xmlbody = NULL;
    xmlbody = strstr(respdata, "\r\n\r\n");
    if(xmlbody == NULL){
        notice("�T�[�o����\�����Ȃ��f�[�^����M���܂����B");
        return CMM_ILL_DATA;
    }
    xmlbody += 4;

    if(! xml_getstreaminfo(xmlbody, si)){
        notice("�T�[�o����\�����Ȃ��f�[�^����M���܂����B");
        return CMM_ILL_DATA;
    }

    return CMM_SUCCESS;
}

// �R�~���j�e�B���擾
// http://com.nicovideo.jp/community/co0000
int nicoalert_getconame(tstring &coid, tstring &coname){

    if(isCmmExit) return CMM_CONN_FAIL;

    char respdata[1024*32];
    unsigned int respdatalen = 0;

    string urlpath = COMMUNITY_NAME_API_PATH;
    urlpath += ts2mb(coid).c_str();

    // �R�~���j�e�B�y�[�WHTML�擾
    int recvlen = gethtml(COMMUNITY_NAME_API_SERVER, urlpath.c_str(), respdata, sizeof(respdata)-1);
    if(recvlen < 0){
        return CMM_TRANS_FAIL;
    }
    respdata[recvlen] = '\0';

    char *htmlbody = NULL;
    htmlbody = strstr(respdata, "\r\n\r\n");
    if(htmlbody == NULL){
        notice("�T�[�o����\�����Ȃ��f�[�^����M���܂����B");
        return CMM_ILL_DATA;
    }
    htmlbody += 4;

    // HTML�^�O��� <h1>�R�~���j�e�B��</h1>
    bool flag = false;
    do {
        char *p_h1_o = strstr(htmlbody, "<h1 ");
        if(p_h1_o == NULL) break;
        char *p_h1_c = strstr(p_h1_o, ">");
        if(p_h1_c == NULL) break;
        p_h1_c++;
        char *p_sh1_o = strstr(p_h1_c, "</h1>");
        if(p_sh1_o == NULL) break;
        *p_sh1_o = '\0';
        coname = mb2ts(p_h1_c);
        htmldecode(coname);
        flag = true;
    } while(0);

    if(flag){
        return CMM_SUCCESS;
    }

    // �N���[�Y�R�~���j�e�B�̏ꍇ�A<h1>�^�O������Ȃ��̂�
    // <title>�^�O������(������Ǝd�l�ύX�Ɏア)
    flag = false;
    do {
        char *p_ts = strstr(htmlbody, "<title>");
        if(p_ts == NULL) break;
        p_ts += 7;
        char *p_te = strstr(p_ts, "</title>");
        if(p_te == NULL) break;
        *p_te = '\0';
        coname = mb2ts(p_ts);

        unsigned int i = 0;
        while(i < coname.size()){
            if(coname[i] == '\r' || coname[i] == '\n'){
                coname.replace(i, 1, _T(""));
            } else {
                i++;
            }
        }
        int loc = coname.rfind(_T("-�j�R�j�R�~���j�e�B"));
        if(loc == string::npos) break;
        coname.replace(loc, coname.size(), _T(""));

        htmldecode(coname);
        flag = true;
    } while(0);

    if(!flag){
        return CMM_ILL_DATA;
    }

    return CMM_SUCCESS;
}


// �`�����l�����擾
// http://ch.nicovideo.jp/channel/ch0000
int nicoalert_getchname(tstring &chid, tstring &chname){

    if(isCmmExit) return CMM_CONN_FAIL;

    char respdata[1024*32];

    string urlpath = CHANNEL_NAME_API_PATH;
    urlpath += ts2mb(chid).c_str();

    // �`�����l���y�[�WHTML�擾
    int recvlen = gethtml(CHANNEL_NAME_API_SERVER, urlpath.c_str(), respdata, sizeof(respdata)-1);
    if(recvlen < 0){
        return CMM_TRANS_FAIL;
    }
    respdata[recvlen] = '\0';

    char *htmlbody = NULL;
    htmlbody = strstr(respdata, "\r\n\r\n");
    if(htmlbody == NULL){
        notice("�T�[�o����\�����Ȃ��f�[�^����M���܂����B");
        return CMM_ILL_DATA;
    }
    htmlbody += 4;

    // HTML�^�O���
    // �`�����l���y�[�W�̏ꍇ�A<title>�^�O������(������Ǝd�l�ύX�Ɏア)
    bool flag = false;
    do {
        char *p_ts = strstr(htmlbody, "<title>");
        if(p_ts == NULL) break;
        p_ts += 7;
        char *p_te = strstr(p_ts, "</title>");
        if(p_te == NULL) break;
        *p_te = '\0';
        chname = mb2ts(p_ts);

        unsigned int i = 0;
        while(i < chname.size()){
            if(chname[i] == '\r' || chname[i] == '\n'){
                chname.replace(i, 1, _T(""));
            } else {
                i++;
            }
        }
        int loc = chname.rfind(_T(" - �j�R�j�R�`�����l��"));
        if(loc == string::npos) break;
        chname.replace(loc, chname.size(), _T(""));

        htmldecode(chname);
        flag = true;
    } while(0);

    if(!flag){
        return CMM_ILL_DATA;
    }

    return CMM_SUCCESS;
}


// �����ԑg���擾
// http://live.nicovideo.jp/watch/lv0000
int nicoalert_getlvname(tstring &lvid, tstring &lvname){
    if(isCmmExit) return CMM_CONN_FAIL;

    char respdata[1024*32];

    string urlpath = LVID_NAME_API_PATH;
    urlpath += ts2mb(lvid).c_str();

    // �����y�[�WHTML�擾
    int recvlen = gethtml(LVID_NAME_API_SERVER, urlpath.c_str(), respdata, sizeof(respdata)-1);
    if(recvlen < 0){
        return CMM_TRANS_FAIL;
    }
    respdata[recvlen] = '\0';

    char *htmlbody = NULL;
    htmlbody = strstr(respdata, "\r\n\r\n");
    if(htmlbody == NULL){
        notice("�T�[�o����\�����Ȃ��f�[�^����M���܂����B");
        return CMM_ILL_DATA;
    }
    htmlbody += 4;

    // HTML�^�O���
    // �����y�[�W�̏ꍇ�A<title>�^�O������(������Ǝd�l�ύX�Ɏア)
    bool flag = false;
    do {
        char *p_ts = strstr(htmlbody, "<title>");
        if(p_ts == NULL) break;
        p_ts += 7;
        char *p_te = strstr(p_ts, "</title>");
        if(p_te == NULL) break;
        *p_te = '\0';
        lvname = mb2ts(p_ts);

        unsigned int i = 0;
        while(i < lvname.size()){
            if(lvname[i] == '\r' || lvname[i] == '\n'){
                lvname.replace(i, 1, _T(""));
            } else {
                i++;
            }
        }
        int loc = lvname.rfind(_T(" - �j�R�j�R������"));
        if(loc == string::npos) break;
        lvname.replace(loc, lvname.size(), _T(""));

        htmldecode(lvname);
        flag = true;
    } while(0);

    if(!flag){
        return CMM_ILL_DATA;
    }

    return CMM_SUCCESS;
}

// �L�[��ʂ���e�擾�֐��ɐU�蕪��
int nicoalert_getkeyname(tstring &key, tstring &keyname){

#ifdef DEBUG_NOT_CONNECT
    notice("�f�o�b�O�ɂ���ڑ����[�h(DEBUG_NOT_CONNECT)");
    return CMM_MAINTENANCE;
#endif

    if(key.substr(0, COID_PREFIX_LEN) == _T(COID_PREFIX)){
        return nicoalert_getconame(key, keyname);
    }
    if(key.substr(0, CHID_PREFIX_LEN) == _T(CHID_PREFIX)){
        return nicoalert_getchname(key, keyname);
    }
    if(key.substr(0, LVID_PREFIX_LEN) == _T(LVID_PREFIX)){
        return nicoalert_getlvname(key, keyname);
    }
    keyname = _T("");
    return CMM_SUCCESS;
}


// �_�ǂ݂���� �����R�}���h���M
int nicoalert_bouyomi(tstring &msg, int speed, int tone, int volume, int voice, int timeout){
    if(isCmmExit) return CMM_CONN_FAIL;

    sockaddr_in sa;
    SOCKET s;
    int ret;
    struct timeval tv;
    fd_set fds, wfds;

#ifdef _UNICODE
    int code = 1;
#else
    int code = 2;
#endif

    int len = msg.size() * sizeof(msg.c_str()[0]);

    char buf[15];
    *(short *)(buf +  0) = 0x0001; // �R�}���h          �i 1:���b�Z�[�W�ǂݏグ�j
    *(short *)(buf +  2) = speed;  // ���x              �i-1:�_�ǂ݂�����ʏ�̐ݒ�j
    *(short *)(buf +  4) = tone;   // ����              �i-1:�_�ǂ݂�����ʏ�̐ݒ�j
    *(short *)(buf +  6) = volume; // ����              �i-1:�_�ǂ݂�����ʏ�̐ݒ�j
    *(short *)(buf +  8) = voice;  // ����              �i 0:�_�ǂ݂�����ʏ�̐ݒ�A1:����1�A2:����2�A3:�j��1�A4:�j��2�A5:�����A6:���{�b�g�A7:�@�B1�A8:�@�B2�A10001�`:SAPI5�j
    *(char  *)(buf + 10) = code;   // ������̕����R�[�h�i 0:UTF-8, 1:Unicode, 2:Shift-JIS�j
    *(long  *)(buf + 11) = len;    // ������̒���

	//�ڑ���w��p�\���̂̏���
	sa.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");
	sa.sin_port             = htons(50001);
	sa.sin_family           = AF_INET;

	//�\�P�b�g�쐬
	s = socket(AF_INET, SOCK_STREAM, 0);
    if(s == INVALID_SOCKET){
        _dbg(_T("socket() failed."));
        return CMM_CONN_FAIL;
    }

    bool success = false;
    do {
        // �m���u���b�N��
        u_long flag = 1;
        if(ioctlsocket(s, FIONBIO, &flag) == SOCKET_ERROR){
            _dbg(_T("ioctlsocket() failed."));
            break;
        }

        //�T�[�o�ɐڑ�
        ret = connect(s, (struct sockaddr *)&sa, sizeof(sa));
        if(ret == INVALID_SOCKET){
            if(WSAGetLastError() != WSAEWOULDBLOCK){
                _dbg(_T("connect() failed."));
                break;
            }
        }

        // �ڑ��҂�
        FD_ZERO(&fds);
        FD_SET(s, &fds);
        tv.tv_sec = timeout/1000;
        tv.tv_usec = timeout%1000;

        wfds = fds;
        ret = select(0, NULL, &wfds, NULL, &tv);
        if(ret == SOCKET_ERROR){
            _dbg(_T("select() failed."));
            break;
        }
        if(ret > 0 && FD_ISSET(s, &wfds)){
            //�f�[�^���M
            send(s, buf, 15, 0);
            send(s, (char *)msg.c_str(), len, 0);
            success = true;
        }

    } while(0);
    //�\�P�b�g�I��
    closesocket(s);

    return (success)?CMM_SUCCESS:CMM_CONN_FAIL;
}


// �j�R���A���[�gAPI��t
// http://live.nicovideo.jp/api/getalertinfo
static int nicoalert_cmm(void){

    char respdata[1024*4];
    unsigned int respdatalen = 0;
    SOCKET s = INVALID_SOCKET;

    notice("�G���g�����X�T�[�o�ɐڑ����B (%s%s)", ENTRANCE_API_SERVER, ENTRANCE_API_PATH);

#ifdef DEBUG_NOT_CONNECT
    notice("�f�o�b�O�ɂ���ڑ����[�h(DEBUG_NOT_CONNECT)");
    return CMM_MAINTENANCE;
#endif

    // �j�R���A���[�gAPI�G���g�����X�T�[�o�ڑ�
    s = tcp_connect(ENTRANCE_API_SERVER, "http");
    if(s == INVALID_SOCKET){
        notice("�ڑ��Ɏ��s���܂����B");
        return CMM_CONN_FAIL;
    }

    notice("�G���g�����X�T�[�o�ɐڑ����܂����B");

    // HTML���N�G�X�g���� Host/User-Agent �K�{
    string http_request;
    char buf[128];
    sprintf_s(buf, "GET %s HTTP/1.0\r\n", ENTRANCE_API_PATH);
    http_request += buf;
    sprintf_s(buf, "Host: %s\r\n", ENTRANCE_API_SERVER);
    http_request += buf;
    sprintf_s(buf, "User-Agent: %s\r\n", UA_STRING);
    http_request += buf;
    sprintf_s(buf, "\r\n");
    http_request += buf;

    // ���N�G�X�g���M/���X�|���X�擾
    int recvlen = tcp_gethttp(s, http_request, respdata, sizeof(respdata)-1);

    closesocket(s); s = INVALID_SOCKET;
    if(recvlen < 0){
        notice("�ʐM�Ɏ��s���܂����B");
        return CMM_TRANS_FAIL;
    }
    respdata[recvlen] = '\0';

    notice("�f�[�^�擾�����B");

    char *xmlbody = NULL;
    xmlbody = strstr(respdata, "\r\n\r\n");
    if(xmlbody == NULL){
        notice("�T�[�o����\�����Ȃ��f�[�^����M���܂����B");
        return CMM_ILL_DATA;
    }
    xmlbody += 4;

    string status, addr, port, thread;
    if(! xml_getalertstatus(xmlbody, status, addr, port, thread)){
        notice("�T�[�o����\�����Ȃ��f�[�^����M���܂����B");
        return CMM_ILL_DATA;
    }

    xmlbody = NULL;
    respdatalen = 0;

    if(status != "ok"){
        notice("�T�[�o�����e�i���X���̂��ߐڑ��𒆎~���܂��B");
        return CMM_MAINTENANCE;
    }

    // �����J�n���T�[�o�ڑ�
    notice("�A���[�g���API�T�[�o�ɐڑ����B (%s:%s)", addr.c_str(), port.c_str());

    s = tcp_connect(addr.c_str(), port.c_str());
    if(s == INVALID_SOCKET){
        notice("�ڑ��Ɏ��s���܂����B");
        return CMM_CONN_FAIL;
    }

    notice("�A���[�g���API�T�[�o�ɐڑ����܂����B");

    bool recv_err = true;
    do {
        int ret;
        string xml_string;
        char buf[128];
        sprintf_s(buf, "<thread thread=\"%s\" version=\"20061206\" res_from=\"-1\"/>", thread.c_str());
        xml_string += buf;

        // '\0'���܂߂đ��M
        ret = send(s, xml_string.c_str(), xml_string.length() + 1, 0);
        if(ret < 0){
            break;
        }

        fd_set fds, rfds;
        int nfds;
        struct timeval tv = {0, 500000};    // 500ms.

        FD_ZERO(&fds);
        FD_SET(s, &fds);
        nfds = s + 1;

        time_t now, lastrecv, lastsend;
        lastrecv = lastsend = time(NULL);

        while(!isCmmExit){
            if(respdatalen >= sizeof(respdata)){
                _dbg(_T("ERROR: ��M�o�b�t�@FULL\n"));
                // �ʏ킠�肦�Ȃ����A�ُ폈���Ƃ��ăo�b�t�@�S�j��
                respdata[sizeof(respdata)-1] = '\0';
                respdatalen = 0;
            }

            now = time(NULL);
            if(lastrecv + CMM_RECVHB_TIMEOUT < now){
                _dbg(_T("ERROR: RECV HB�^�C���A�E�g\n"));
                break;
            }
            if(lastsend + CMM_SENDHB_TIMEOUT < now){
                _dbg(_T("SEND HB\n"));
                ret = send(s, "", 1, 0);
                lastsend = now;
            }

            // �f�[�^��M�҂�
            rfds = fds;
            ret = select(nfds, &rfds, NULL, NULL, &tv);
            if(ret < 0) break;
            if(ret == 0){
                // timeout
                continue;
            }

            ret = recv(s, respdata + respdatalen, sizeof(respdata)-respdatalen, 0);
            if(ret < 0) break;
            if(ret == 0){
                _dbg(_T("�R�l�N�V��������ؒf\n"));
                recv_err = false;
                break;
            }
            lastrecv = now;

            respdatalen += ret;

            while(1){
                unsigned int i;
                for(i = 0; i < respdatalen; i++){
                    if(respdata[i] == '\0') break;
                }
                if(i == respdatalen) break;
                int movelen = i + 1;

                c_alertinfo ai;
                // alertinfo API��XML���
                xml_alertinfo(respdata, &ai);

                if(callback_alertinfo){
                    callback_alertinfo(&ai);
                }

                memmove(respdata, respdata + movelen, respdatalen - movelen);
                respdatalen -= movelen;
            }
        }

        // ����I�����̐ؒf
        recv_err = false;

    } while(0);

    closesocket(s); s = INVALID_SOCKET;
    if(recv_err){
        notice("�ʐM�Ɏ��s���܂����B");
        return CMM_TRANS_FAIL;
    }

    if(isCmmExit){
        return CMM_SUCCESS;
    }

    notice("�A���[�g���API�T�[�o����ؒf����܂����B");
    return CMM_TRANS_FAIL;
}

// �A���[�gAPI�T�[�o�ւ̐ڑ����s���̑ҋ@����
static void waitmsg(unsigned int wait){
    unsigned int min = wait / 60;
    unsigned int sec = wait % 60;
    char tstr[16];

    if(min){
        if(min && sec){
            sprintf_s(tstr, "%u:%usec.", min, sec);
        } else {
            sprintf_s(tstr, "%umin.", min);
        }
    } else {
        sprintf_s(tstr, "%usec.", sec);
    }
    notice("�Đڑ��ҋ@��(%s)", tstr);

    for(unsigned int i = 0; i < wait*2 && !isCmmExit; i++){
        Sleep(500);
    }
}

static int getversion(string &verstr){
    if(isCmmExit) return CMM_CONN_FAIL;

#ifdef DEBUG_NOT_CONNECT
    notice("�f�o�b�O�ɂ���ڑ����[�h(DEBUG_NOT_CONNECT)");
    return CMM_MAINTENANCE;
#endif

    char respdata[1024*4];
    unsigned int respdatalen = 0;
    SOCKET s = INVALID_SOCKET;

    s = tcp_connect(VERSION_CHECK_SERVER, "http");
    if(s == INVALID_SOCKET){
        return CMM_CONN_FAIL;
    }

    string http_request;
    char buf[128];
    sprintf_s(buf, "GET %s HTTP/1.0\r\n", VERSION_CHECK_PATH);
    http_request += buf;
    sprintf_s(buf, "Host: %s\r\n", VERSION_CHECK_SERVER);
    http_request += buf;
    sprintf_s(buf, "User-Agent: %s\r\n", UA_STRING);
    http_request += buf;
    sprintf_s(buf, "\r\n");
    http_request += buf;

    int recvlen = tcp_gethttp(s, http_request, respdata, sizeof(respdata)-1);

    closesocket(s); s = INVALID_SOCKET;
    if(recvlen < 0) return CMM_TRANS_FAIL;
    respdata[recvlen] = '\0';

    char *htmlbody = strstr(respdata, "\r\n\r\n");
    if(htmlbody == NULL){
        // HTML�w�b�_�Ȃ�
        return CMM_ILL_DATA;
    }
    htmlbody += 4;
    int httphdrsize = htmlbody - respdata;
    int httpbodysize = recvlen - httphdrsize;

    if(httpbodysize > 15) return CMM_ILL_DATA;

    // sanity check
    for(int i = 0; i < httpbodysize; i++){
        if(isdigit(htmlbody[i]) || htmlbody[i] == '.') continue;
        if(htmlbody[i] == '\r' || htmlbody[i] == '\n'){
            htmlbody[i] = 0; break;
        }
        return CMM_ILL_DATA;
    }
    verstr = htmlbody;

    return CMM_SUCCESS;
}

static unsigned int verval(const char *ver){
    int l = strlen(ver);
    int v[4], c = 0;
    v[0]=v[1]=v[2]=v[3]=0;

    bool f = false;
    for(int i = 0; i < l; i++){
        if(!f && isdigit(ver[i])){
            v[c] = atoi(ver+i);
            if(v[c]<0) v[c] = 0;
            if(v[c]>255) v[c] = 255;
            c++;
            if(c >= 4) break;
            f = true;
        }
        else if(!isdigit(ver[i])) f = false;
    }
    return v[0]<<24|v[1]<<16|v[2]<<8|v[3];
}

// �o�[�W�����`�F�b�N
static void verchk(){
    string verstr;
    notice("�o�[�W�����`�F�b�N���ł��B");
    int ret = getversion(verstr);
    if(ret != CMM_SUCCESS){
        switch(ret){
        case CMM_CONN_FAIL:
            notice("�ڑ��Ɏ��s���܂����B");
            break;
        case CMM_TRANS_FAIL:
            notice("����M�Ɏ��s���܂����B");
            break;
        case CMM_ILL_DATA:
            notice("��M�f�[�^���s���ł��B");
            break;
        }
        notice_popup("�o�[�W�����`�F�b�N���ɃG���[���������܂����B");
        return;
    }
    if(verval(verstr.c_str()) > verval(VERSION_STRING_CHK)){
        notice_popup_link("�V�����o�[�W����(%s)������܂��B", verstr.c_str());
    } else {
        notice_popup("���̃o�[�W�����͍ŐV�łł��B");
    }
}


// �ʐM�X���b�h
void commthread(void *arg){
    unsigned int wait;
    unsigned int errcount = 0;
    time_t lasterr = 0;

    // �o�[�W�����`�F�b�N
    if(ReadOptionInt(OPTION_VERCHK_ENABLE, DEF_OPTION_VERCHK_ENABLE)){
        verchk();
    }

    // �o�^�ǉ������܂őҋ@
    while(!isCmmExit){
        regdata_info.lock();
        if(regdata_info.size() > 0){
            regdata_info.unlock();
            break;
        }
        regdata_info.unlock();
        Sleep(500);
    }

    // �X���b�h���샋�[�v
    while(!isCmmExit){
        int ret = nicoalert_cmm();
        if(isCmmExit) break;

        // API�T�[�o�����e�i���X���̏ꍇ�A��莞�ԑҋ@(30��)
        if(ret == CMM_MAINTENANCE){
            waitmsg(CMM_MAINT_WAITTIME);
            continue;
        }

        // �G���[�ؒf�̏ꍇ�A�G���[���A�������x�ɑҋ@���ԑ���
        if(lasterr + CMM_ERROR_WAITTIME_RESET > time(NULL)){
            wait *= 2;
            if(wait >= CMM_ERROR_WAITTIME_LIMIT)
                wait = CMM_ERROR_WAITTIME_LIMIT;
        } else {
            wait = CMM_ERROR_WAITTIME;
        }

        lasterr = time(NULL);
        waitmsg(wait);

    }
    SetEvent(hCmmExitEvent);
    return;
}

// cmm�X���b�h�J�n
bool cmm_start(
    void(*cb_alertinfo)(const c_alertinfo *),
    BOOL(*cb_msgpopup)(const tstring &msg, const tstring &link),
    void(*cb_msginfo)(const TCHAR *) ){
    WSADATA wsaData;
    int ret;

    ret = WSAStartup(MAKEWORD(1,1), &wsaData);
    if(ret == SOCKET_ERROR){
        return 1;
    }

    callback_alertinfo = cb_alertinfo;
    callback_msgpopup = cb_msgpopup;
    callback_msginfo = cb_msginfo;

    hCmmExitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    isCmmExit = false;

    if(-1 == _beginthread(commthread, 0, NULL)){
        return false;
    }
    return true;
}

// cmm�X���b�h��~
bool cmm_exit(void){
    int waitcount = PROCESS_EXIT_WAITCOUNT;
    isCmmExit = true;
    DWORD ret;

    callback_alertinfo = NULL;
    callback_msgpopup = NULL;
    callback_msginfo = NULL;

    while(waitcount){
        doevent();
        ret = WaitForSingleObject(hCmmExitEvent, 500);
        if(ret == WAIT_OBJECT_0) break;
        waitcount--;
    }
    WSACleanup();
    return true;
}
