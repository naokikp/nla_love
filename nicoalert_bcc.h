
#ifndef NICOALERT_BCC_H
#define NICOALERT_BCC_H

#define BCC_QUEUE_TIME  120

void alertinfo_ind(const c_alertinfo *ai);
bool bcc_soundtest(const TCHAR *fn, int vol);
void bcc_soundvol(int vol);
bool bcc_startstat(unsigned int *count, unsigned int *period);

bool bcc_start( void(*)(unsigned int, BOOL), BOOL(*)(const tstring &msg, const tstring &link), void(*)(const TCHAR *) );
bool bcc_exit(void);
bool bcc_check(void);

#endif
