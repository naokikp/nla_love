
#ifndef NICOALERT_NAME_H
#define NICOALERT_NAME_H

#define ACQ_CHECK_TIME     120
#define ACQ_CHECK_TIME_SL   12
#define ACQ_CHECK_COUNT     10

void con_add(unsigned int mindbidx);
void con_start(void);

bool con_start( void(*)(unsigned int), void(*)(const TCHAR *) );
bool con_exit(void);

#endif
