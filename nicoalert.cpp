//	�j�R���A���[�g(Love)
//	Copyright (C) 2012 naoki.kp

#include "nicoalert.h"
#include "nicoalert_db.h"
#include "nicoalert_snd.h"

BOOL InitApp(HINSTANCE, LPCTSTR);

TCHAR IconName[]    = _T("APPICON");
TCHAR szClassName[] = _T("APPLICATIONCLASS_nicoalert");
TCHAR szMutexName[] = _T("APPLICATIONMUTEX_nicoalert");

HINSTANCE hInst;
HWND hWnd;

setting setting_info;
bool setting_info_darty;
regdata regdata_info;
regdata_hash_t regdata_hash;


// �f�o�b�O�o��
void _dbg(const TCHAR *fmt, ...){
#ifdef _DEBUG
	TCHAR buf[1024];
    _stprintf_s(buf, _T("%u :"), GetTickCount());
	OutputDebugString(buf);
	va_list ap;
	va_start(ap, fmt);
	_vsntprintf_s(buf, sizeof(buf)-1, fmt, ap);
	OutputDebugString(buf);
	va_end(ap);
#endif
}

// UTF-8����UTF-16�֕ϊ�
tstring mb2ts(const char *buf){
    tstring str;
#ifdef UNICODE
    TCHAR tbuf[1024];
    int ret = MultiByteToWideChar(CP_UTF8, 0, buf, -1, tbuf, ESIZEOF(tbuf));
    if(ret) str = tbuf;
#else
    str = buf;
#endif
    return str;
}

// UTF-16����UTF-8�֕ϊ�
string ts2mb(tstring &tstr){
    string str;
#ifdef UNICODE
    char buf[1024];
    int ret = WideCharToMultiByte(CP_UTF8, 0, tstr.c_str(), -1, buf, ESIZEOF(buf), NULL, NULL);
    if(ret) str = buf;
#else
    str = tstr;
#endif
    return str;
}

// �v���O�����N���p�X�̃I�v�V��������
void splitpath_opt(tstring &path, tstring &opt){
    opt = _T("");
    int loc = path.rfind(_T(".exe "));
    if(loc != tstring::npos){
        opt = path.substr(loc + 5, -1);
        path.replace(loc + 4, -1, _T(""));
    }
    _dbg(_T("path = %s, opt = %s\n"), path.c_str(), opt.c_str());
}

// �u���E�U�N��
bool exec_browser(tstring &url){
    int ret = 0;
    tstring &p = ReadOptionString(OPTION_BROWSER_PATH, _T(""));
    if(ReadOptionInt(OPTION_DEFAULT_BROWSER_USE, DEF_OPTION_DEFAULT_BROWSER_USE) || p == _T("")){
        // �f�t�H���g�u���E�U�ŋN��
        ret = (int)ShellExecute(NULL, _T("open"), url.c_str(), NULL, NULL, SW_SHOWNORMAL);
    } else {
        // �w��u���E�U�ŋN��
        tstring opt;
        splitpath_opt(p, opt);
        opt += _T(" ");
        opt += url;
        ret = (int)ShellExecute(NULL, _T("open"), p.c_str(), opt.c_str(), NULL, SW_SHOWNORMAL);
    }
    if(ret < 32){
        return false; // �N�����s
    }
    return true;
}

// �w�肳�ꂽ�L�[�ɑΉ�����WEB�y�[�W���J��
bool exec_browser_by_key(tstring &key){
    tstring param;
    if(key.compare(0, COID_PREFIX_LEN, _T(COID_PREFIX)) == 0){
        param = NICOLIVE_COID_URL;
    }
    else if(key.compare(0, CHID_PREFIX_LEN, _T(CHID_PREFIX)) == 0){
        param = NICOLIVE_CHID_URL;
    }
    else if(key.compare(0, USERID_PREFIX_LEN, _T(USERID_PREFIX)) == 0){
        param = NICOLIVE_USERID_URL;
    }
    else if(key.compare(0, LVID_PREFIX_LEN, _T(LVID_PREFIX)) == 0){
        param = NICOLIVE_LVID_URL;
    }
    param += key;

    return exec_browser(param);
}


// �^�C�v�X�^���v������擾
void timefmt(TCHAR *p, int len, unsigned int t){
    time_t tim = t;
    tm ltm;
    if(t == 0){
        time(&tim);
    }
    localtime_s(&ltm, &tim);
    _stprintf_s(p, len, _T("%04d/%02d/%02d %02d:%02d:%02d"),
        ltm.tm_year + 1900, ltm.tm_mon + 1, ltm.tm_mday,
        ltm.tm_hour, ltm.tm_min, ltm.tm_sec);
}

// �I�v�V�����p�����[�^�ǂݏo��(������)
tstring ReadOptionString(const TCHAR *key, const TCHAR *default_val){
    tstring ret = default_val;
    setting_info.lock();
    ITER(setting_info) it = setting_info.find(key);
    if(it != setting_info.end()){
        _dbg(_T("%s = %s\n"), key, setting_info[key].c_str());
        ret = setting_info[key];
    }
    setting_info.unlock();
    return ret;
}

// �I�v�V�����p�����[�^�ǂݏo��(����)
unsigned int ReadOptionInt(const TCHAR *key, unsigned int default_val){
    unsigned int ret = default_val;
    setting_info.lock();
    ITER(setting_info) it = setting_info.find(key);
    if(it != setting_info.end()){
        _dbg(_T("%s = %s\n"), key, setting_info[key].c_str());
        ret = _tcstoul(setting_info[key].c_str(), NULL, 10);
    } 
    setting_info.unlock();
    return ret;
}

// �I�v�V�����p�����[�^��������(������)
void SaveOptionString(const TCHAR *key, const TCHAR *value){
    setting_info.lock();
    setting_info[key] = value;
    setting_info_darty = true;
    setting_info.unlock();
}

// �I�v�V�����p�����[�^��������(����)
void SaveOptionInt(const TCHAR *key, unsigned int value){
    TCHAR buf[32];
    _stprintf_s(buf, _T("%u"), value);
    SaveOptionString(key, buf);
}

// �A�C�R�����\�[�X�ǂݏo��
HICON LoadIconRsc(const TCHAR *res){
    return (HICON)LoadImage(hInst, res, IMAGE_ICON, 0, 0, LR_DEFAULTCOLOR | LR_SHARED);
}

// OS�o�[�W�����擾(WINNT�n��̂�)
unsigned int GetOSVer(void){
    static unsigned int winver;
    if(winver != 0) return winver;

    OSVERSIONINFO osvi;
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    if(GetVersionEx(&osvi) && osvi.dwPlatformId == VER_PLATFORM_WIN32_NT){
        winver = (osvi.dwMajorVersion << 8) + osvi.dwMinorVersion;
    }
    return winver;
}

// ���b�Z�[�W���[�v
void doevent(void){
	MSG msg;
	while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)){
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}


// �G���g���|�C���g
int WINAPI WinMain(HINSTANCE hCurInst, HINSTANCE hPrevInst, LPSTR lpsCmdLine, int nCmdShow){

    _CrtSetDbgFlag(_CRTDBG_LEAK_CHECK_DF | _CRTDBG_ALLOC_MEM_DF);

    if(!hPrevInst){
		if(!InitApp(hCurInst, szClassName)){
			return FALSE;
        }
	}

	CreateMutex(NULL, FALSE, szMutexName);
	if(GetLastError() == ERROR_ALREADY_EXISTS){
		hWnd = FindWindow(szClassName, NULL);
		if (hWnd){
			SetForegroundWindow(GetLastActivePopup(hWnd));
		}
		return 0;
	}
	hInst = hCurInst;
	DialogBox(hInst, _T("DIALOG_MAIN"), NULL, (DLGPROC)MainDlgProc);

    _dbg(_T("process exit\n"));
    return(0);
}

BOOL InitApp(HINSTANCE hInst, LPCTSTR szClassName){
	WNDCLASS wc;
	wc.style = 0;//CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = DefDlgProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = DLGWINDOWEXTRA;
	wc.hInstance = hInst;
	wc.hIcon = LoadIcon(hInst, IconName);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = szClassName;
	return (RegisterClass(&wc));
}