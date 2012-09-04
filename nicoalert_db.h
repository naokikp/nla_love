
#ifndef NICOALERT_DB_H
#define NICOALERT_DB_H

#include "nicoalert.h"
#include "sqlite3.h"

//#pragma comment(lib, "sqlite3.lib")

class nicoalert_db {
private:
    sqlite3* dbh;
    bool transaction;

public:
    nicoalert_db(void){
        dbh = NULL;
        transaction = false;
    }
    ~nicoalert_db(void){
        if(transaction) tr_rollback();
        if(dbh) sqlite3_close(dbh);
    }

    bool open(const TCHAR *fn);
    bool close(void);
    bool execsql(const TCHAR *sql);

    bool istableexist(const TCHAR *table, bool &exist);
    bool createtable(const TCHAR *table, const TCHAR *column);

    bool loadsetting(setting &setting_info);
    bool savesetting(setting &setting_info);

    bool loadregdata(regdata &regdata_info);
    bool newregdata(c_regdata &cr);
    bool updateregdata(c_regdata &cr);
    bool deleteregdata(c_regdata &cr);


    bool tr_begin(void);
    bool tr_commit(void);
    bool tr_rollback(void);
};

#define NADB_TABLE_SETTING              _T("nadb_setting")
#define NADB_COLUMN_TYPE_SETTING        _T("key TEXT, value TEXT")
#define NADB_COLUMN_SETTING             _T("key, value")

#define NADB_TABLE_REGIST               _T("nadb_regist")
#define NADB_COLUMN_TYPE_REGIST         _T("key TEXT PRIMARY KEY, key_name TEXT, memo TEXT, last_lv TEXT, last_start INTEGER, notify INTEGER")
#define NADB_COLUMN_REGIST              _T("rowid, key, key_name, memo, last_lv, last_start, notify")
#define NADB_COLUMN_UPDATESET_REGIST    _T("key_name = ?, memo = ?, last_lv = ?, last_start = ?, notify = ?")
#define NADB_COLUMN_UPDATEKEY_REGIST    _T("key = ?")


#ifdef UNICODE
#define sqlite3_topen           sqlite3_open16
#define sqlite3_tprepare        sqlite3_prepare16
#define sqlite3_column_ttext    sqlite3_column_text16
#define sqlite3_bind_ttext      sqlite3_bind_text16
#else
#define sqlite3_topen           sqlite3_open
#define sqlite3_tprepare        sqlite3_prepare
#define sqlite3_column_ttext    sqlite3_column_text
#define sqlite3_bind_ttext      sqlite3_bind_text
#endif


#endif
