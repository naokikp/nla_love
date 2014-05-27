
#ifndef NICOALERT_DB_H
#define NICOALERT_DB_H

#include "nicoalert.h"
#include "sqlite3/sqlite3.h"

//#pragma comment(lib, "sqlite3.lib")

class nicoalert_db {
private:
    sqlite3* dbh;
    unsigned int transaction_count;

public:
    nicoalert_db(void){
        dbh = NULL;
        transaction_count = 0;
    }
    ~nicoalert_db(void){
        if(transaction_count) tr_rollback();
        if(dbh) sqlite3_close(dbh);
    }

    bool open(const TCHAR *fn);
    bool close(void);
    bool execsql(const TCHAR *sql);

    bool istableexist(const TCHAR *table, bool &exist);
    bool createtable(const TCHAR *table, const TCHAR *column);

    bool loadsetting(setting &setting_info);
    bool savesetting(setting &setting_info);

    bool verupregdata(int oldver, int newver);

    bool loadregdata(regdata &regdata_info);
    bool newregdata(c_regdata &cr);
    bool updateregdata(c_regdata &cr);
    bool deleteregdata(c_regdata &cr);
    bool istableexistregdata(void);


    bool tr_begin(void);
    bool tr_commit(void);
    bool tr_rollback(void);

    bool cleanup(void);
};

#define NADB_TABLE_SETTING              _T("nadb_setting")
#define NADB_COLUMN_TYPE_SETTING        _T("key TEXT, value TEXT")
#define NADB_COLUMN_SETTING             _T("key, value")

#define NADB_TABLE_REGIST               _T("nadb_regist")
#define NADB_COLUMN_TYPE_REGIST         _T("key TEXT PRIMARY KEY, key_name TEXT, memo TEXT, last_lv TEXT, last_start INTEGER, notify INTEGER, label INTEGER")
#define NADB_COLUMN_REGIST              _T("rowid, key, key_name, memo, last_lv, last_start, notify, label")
#define NADB_COLUMN_UPDATESET_REGIST    _T("key_name = ?, memo = ?, last_lv = ?, last_start = ?, notify = ?, label = ?")
#define NADB_COLUMN_UPDATEKEY_REGIST    _T("key = ?")
#define NADB_DBUPDATE_1TO2_REGIST       _T("ALTER TABLE nadb_regist ADD COLUMN label INTEGER")


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
