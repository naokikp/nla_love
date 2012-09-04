
#ifndef MUTEX_HPP
#define MUTEX_HPP

#include <windows.h>

class mutex {

private:
    CRITICAL_SECTION cs;

public:

    mutex(void){
        InitializeCriticalSection(&cs);
    }
    ~mutex(void){
        DeleteCriticalSection(&cs);
    }
    void lock(){
        EnterCriticalSection(&cs);
    }
    void unlock(){
        LeaveCriticalSection(&cs);
    }

};

#endif  // MUTEX_HPP
