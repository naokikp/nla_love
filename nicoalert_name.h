
#ifndef NICOALERT_NAME_H
#define NICOALERT_NAME_H

#define ACQ_CHECK_TIME     100  // 取得数カウントキューに残る時間(秒)
#define ACQ_CHECK_TIME_SL    5  // 取得数カウントキューが規定数以上の場合の取得間隔
#define ACQ_CHECK_COUNT     20  // 取得数カウントキュー規定数

void con_add(unsigned int mindbidx);
void con_start(void);

bool con_start( void(*)(unsigned int, BOOL), void(*)(const TCHAR *) );
bool con_exit(void);
bool con_check(void);

#endif
