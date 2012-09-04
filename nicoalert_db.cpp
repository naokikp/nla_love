//	ニコ生アラート(Love)
//	Copyright (C) 2012 naoki.kp

#include "nicoalert.h"
#include "nicoalert_db.h"

// DBファイルopen
bool nicoalert_db::open(const TCHAR *fn){
    int st;
    if(dbh != NULL) return true;

    st = sqlite3_topen(fn, &dbh);
    if(st != SQLITE_OK){
        return false;
    }
    return true;
}

// DBファイルクローズ
bool nicoalert_db::close(void){
    int st;
    if(dbh == NULL) return true;

    st = sqlite3_close(dbh);
    if(st != SQLITE_OK){
        return false;
    }
    dbh = NULL;
    return true;
}

// DBに任意のSQLを実行
bool nicoalert_db::execsql(const TCHAR *sql){
    if(dbh == NULL) return false;

    int st;
    sqlite3_stmt* stmt;
    st = sqlite3_tprepare(dbh, sql, -1, &stmt, NULL);
    if(st != SQLITE_OK || !stmt){
        return false;
    }
    while(sqlite3_step(stmt) != SQLITE_DONE);
    sqlite3_finalize(stmt);
    return true;
}

// DB内に指定したテーブルが存在するか判定
bool nicoalert_db::istableexist(const TCHAR *table, bool &exist){
    if(dbh == NULL) return false;

    int st;
    TCHAR sql[256];
    sqlite3_stmt* stmt;

    _stprintf_s(sql, _T("SELECT * FROM sqlite_master WHERE type='table' AND name='%s'"), table);
    st = sqlite3_tprepare(dbh, sql, -1, &stmt, NULL);
    if(st != SQLITE_OK || !stmt){
        return false;
    }

    exist = false;
    if(sqlite3_step(stmt) == SQLITE_ROW){
        exist = true;
    }
    sqlite3_finalize(stmt);

    return true;
}

// テーブル作成
bool nicoalert_db::createtable(const TCHAR *table, const TCHAR *column){
    if(dbh == NULL) return false;

    TCHAR sql[256];
    _stprintf_s(sql, _T("CREATE TABLE '%s' (%s)"), table, column);
    return execsql(sql);
}

// 設定情報DBテーブル読み出し
bool nicoalert_db::loadsetting(setting &setting_info){
    if(dbh == NULL) return false;
    bool exist;

    // テーブルが存在しなかった場合に生成する
    if(!istableexist(NADB_TABLE_SETTING, exist)) return false;
    if(!exist){
        if(!createtable(NADB_TABLE_SETTING, NADB_COLUMN_TYPE_SETTING)) return false;
    }

    int st;
    TCHAR sql[256];
    sqlite3_stmt* stmt;

    // 設定情報テーブルの全レコード読み出し
    _stprintf_s(sql, _T("SELECT %s FROM '%s'"), NADB_COLUMN_SETTING, NADB_TABLE_SETTING);
    st = sqlite3_tprepare(dbh, sql, -1, &stmt, NULL);
    if(st != SQLITE_OK || !stmt){
        return false;
    }

    setting_info.lock();
    setting_info.clear();

    while(sqlite3_step(stmt) == SQLITE_ROW){
        TCHAR *pkey, *pvalue;
        pkey   = (TCHAR *)sqlite3_column_ttext(stmt, 0);
        pvalue = (TCHAR *)sqlite3_column_ttext(stmt, 1);

        if(pkey != NULL && pvalue != NULL){
            setting_info[pkey] = pvalue;
        }
    }
    sqlite3_finalize(stmt);

    setting_info.unlock();

    return true;
}

// 設定情報をDBテーブルへ書き込み
bool nicoalert_db::savesetting(setting &setting_info){
    if(dbh == NULL) return false;
    bool exist;

    if(!istableexist(NADB_TABLE_SETTING, exist)) return false;
    if(!exist){
        if(!createtable(NADB_TABLE_SETTING, NADB_COLUMN_TYPE_SETTING)) return false;
    }

    int st;
    TCHAR sql[256];
    sqlite3_stmt* stmt;

    // トランザクション開始(return禁止)
    if(!tr_begin()) return false;

    setting_info.lock();
    bool success = false;
    do {
        // 全レコード破棄
        _stprintf_s(sql, _T("DELETE FROM '%s'"), NADB_TABLE_SETTING);
        if(!execsql(sql)) break;

        // 設定情報レコード書き込み
        _stprintf_s(sql, _T("INSERT INTO '%s' (%s) VALUES (?, ?)"), NADB_TABLE_SETTING, NADB_COLUMN_SETTING);
        st = sqlite3_tprepare(dbh, sql, -1, &stmt, NULL);
        if(st != SQLITE_OK || !stmt) break;

        bool insert_success = false;
        FOREACH(it, setting_info){
            sqlite3_bind_ttext(stmt, 1, it->first.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_ttext(stmt, 2, it->second.c_str(), -1, SQLITE_TRANSIENT);
            if(sqlite3_step(stmt) != SQLITE_DONE) break;
            if(sqlite3_reset(stmt) != SQLITE_OK) break;
            if(sqlite3_clear_bindings(stmt) != SQLITE_OK) break;
            insert_success = true;
        }
        st = sqlite3_finalize(stmt);
        if(st != SQLITE_OK) break;

        if(!insert_success) break;

        if(!tr_commit()) break;
        success = true;
    } while(0);
    setting_info.unlock();

    // トランザクション終了
    if(!success){
        tr_rollback();
        return false;
    }

    return true;
}

// 登録情報読み出し
bool nicoalert_db::loadregdata(regdata &regdata_info){
    if(dbh == NULL) return false;
    bool exist;

    // テーブルが存在しなかった場合に生成する
    if(!istableexist(NADB_TABLE_REGIST, exist)) return false;
    if(!exist){
        if(!createtable(NADB_TABLE_REGIST, NADB_COLUMN_TYPE_REGIST)) return false;
    }

    int st;
    TCHAR sql[256];
    sqlite3_stmt* stmt;

    // 登録情報テーブルの全レコード読み出し
    _stprintf_s(sql, _T("SELECT %s FROM '%s'"), NADB_COLUMN_REGIST, NADB_TABLE_REGIST);
    st = sqlite3_tprepare(dbh, sql, -1, &stmt, NULL);
    if(st != SQLITE_OK || !stmt){
        return false;
    }

    regdata_info.lock();
    regdata_info.clear();

    while(sqlite3_step(stmt) == SQLITE_ROW){
        TCHAR *pkey, *pkey_name, *plast_lv, *pmemo;
        unsigned idx, last_start, notify;
        idx         = sqlite3_column_int(stmt, 0);
        pkey        = (TCHAR *)sqlite3_column_ttext(stmt, 1);
        pkey_name   = (TCHAR *)sqlite3_column_ttext(stmt, 2);
        pmemo       = (TCHAR *)sqlite3_column_ttext(stmt, 3);
        plast_lv    = (TCHAR *)sqlite3_column_ttext(stmt, 4);
        last_start  = sqlite3_column_int(stmt, 5);
        notify      = sqlite3_column_int(stmt, 6);

        c_regdata rd;
        rd.idx = idx;
        if(pkey) rd.key = pkey;
        if(pkey_name) rd.key_name = pkey_name;
        if(pmemo) rd.memo = pmemo;
        if(plast_lv) rd.last_lv = plast_lv;
        rd.last_start = last_start;
        rd.notify = notify;

        regdata_info[idx] = rd;

    }
    sqlite3_finalize(stmt);

    regdata_info.unlock();

    return true;
}

// 登録情報の新規追加
bool nicoalert_db::newregdata(c_regdata &cr){
    if(dbh == NULL) return false;
    bool exist;

    // テーブルが存在しなかった場合に生成する
    if(!istableexist(NADB_TABLE_REGIST, exist)) return false;
    if(!exist){
        if(!createtable(NADB_TABLE_REGIST, NADB_COLUMN_TYPE_REGIST)) return false;
    }

    int st;
    TCHAR sql[256];
    sqlite3_stmt* stmt;

    // 登録情報レコード書き込み
    _stprintf_s(sql, _T("INSERT INTO '%s' (%s) VALUES (?)"), NADB_TABLE_REGIST, _T("key"));
    st = sqlite3_tprepare(dbh, sql, -1, &stmt, NULL);
    if(st != SQLITE_OK || !stmt) return false;

    bool insert_success = false;
    do {
        sqlite3_bind_ttext(stmt, 1, cr.key.c_str(), -1, SQLITE_TRANSIENT);
        if(sqlite3_step(stmt) != SQLITE_DONE) break;

        // dbidx取得
        cr.idx = (unsigned int)sqlite3_last_insert_rowid(dbh);

        insert_success = true;
    } while(0);

    st = sqlite3_finalize(stmt);
    if(st != SQLITE_OK) return false;

    return insert_success;
}


// 登録情報の更新
bool nicoalert_db::updateregdata(c_regdata &cr){
    if(dbh == NULL) return false;

    int st;
    TCHAR sql[256];
    sqlite3_stmt* stmt;

    // 登録情報レコード更新
    _stprintf_s(sql, _T("UPDATE '%s' SET %s WHERE %s"), NADB_TABLE_REGIST, NADB_COLUMN_UPDATESET_REGIST, NADB_COLUMN_UPDATEKEY_REGIST);
    st = sqlite3_tprepare(dbh, sql, -1, &stmt, NULL);
    if(st != SQLITE_OK || !stmt) return false;

    bool update_success = false;
    do {
        sqlite3_bind_ttext(stmt, 1, cr.key_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_ttext(stmt, 2, cr.memo.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_ttext(stmt, 3, cr.last_lv.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int  (stmt, 4, cr.last_start);
        sqlite3_bind_int  (stmt, 5, cr.notify);
        sqlite3_bind_ttext(stmt, 6, cr.key.c_str(), -1, SQLITE_TRANSIENT);

        if(sqlite3_step(stmt) != SQLITE_DONE) break;
        if(sqlite3_reset(stmt) != SQLITE_OK) break;
        if(sqlite3_clear_bindings(stmt) != SQLITE_OK) break;
        update_success = true;
    } while(0);

    st = sqlite3_finalize(stmt);
    if(st != SQLITE_OK) return false;

    return update_success;
}

// 登録情報の削除
bool nicoalert_db::deleteregdata(c_regdata &cr){
    if(dbh == NULL) return false;

    int st;
    TCHAR sql[256];
    sqlite3_stmt* stmt;

    // 登録情報レコード削除
    _stprintf_s(sql, _T("DELETE FROM '%s' WHERE %s"), NADB_TABLE_REGIST, NADB_COLUMN_UPDATEKEY_REGIST);
    st = sqlite3_tprepare(dbh, sql, -1, &stmt, NULL);
    if(st != SQLITE_OK || !stmt) return false;

    bool delete_success = false;
    do {
        sqlite3_bind_ttext(stmt, 1, cr.key.c_str(), -1, SQLITE_TRANSIENT);

        if(sqlite3_step(stmt) != SQLITE_DONE) break;
        if(sqlite3_reset(stmt) != SQLITE_OK) break;
        if(sqlite3_clear_bindings(stmt) != SQLITE_OK) break;
        delete_success = true;
    } while(0);

    st = sqlite3_finalize(stmt);
    if(st != SQLITE_OK) return false;

    return delete_success;
}

// トランザクション開始
bool nicoalert_db::tr_begin(void){
    if(transaction) return false;
    transaction = true;
    return execsql(_T("BEGIN"));
}
// トランザクション終了(確定)
bool nicoalert_db::tr_commit(void){
    if(!transaction) return false;
    transaction = false;
    return execsql(_T("COMMIT"));
}
// トランザクション終了(ロールバック)
bool nicoalert_db::tr_rollback(void){
    if(!transaction) return false;
    transaction = false;
    return execsql(_T("ROLLBACK"));
}
