
#ifndef NICOALERT_NAME_H
#define NICOALERT_NAME_H

#define ACQ_CHECK_TIME     100  // �擾���J�E���g�L���[�Ɏc�鎞��(�b)
#define ACQ_CHECK_TIME_SL    5  // �擾���J�E���g�L���[���K�萔�ȏ�̏ꍇ�̎擾�Ԋu
#define ACQ_CHECK_COUNT     20  // �擾���J�E���g�L���[�K�萔

void con_add(unsigned int mindbidx);
void con_start(void);

bool con_start( void(*)(unsigned int, BOOL), void(*)(const TCHAR *) );
bool con_exit(void);
bool con_check(void);

#endif
