
#ifndef __THREADS_H
#define __THREADS_H 1

#if defined(__cplusplus)
extern "C" {
#endif


#if HAVE_CONFIG_H
#include "config.h"
#endif

#include "types.h"

#if HAVE_TIME_H
#include <time.h>
#endif

#include "timer.h"

#if HAVE_PTHREAD_H
# include <pthread.h>			/* UNIX */

 typedef pthread_t	_aaxThread;

 typedef struct _aaxCondition
 {
   pthread_cond_t condition;
   pthread_mutex_t mutex;
   char triggered;
 } _aaxCondition;

 typedef struct _aaxMutex
 { 
   pthread_mutex_t mutex;

# ifndef NDEBUG
   const char *name;
   const char *function;
# endif
   char initialized;
 } _aaxMutex;



#elif defined( WIN32 )
# include <Windows.h>			/* WINDOWS */

 typedef struct _aaxThread 
 {
   HANDLE handle;
   HANDLE task;
   int taskCount;

   void *(*callback_fn)(void*);
   void *callback_data;
 } _aaxThread;

 typedef HANDLE _aaxCondition;

 typedef struct _aaxMutex
 {
#ifndef NDEBUG
   HANDLE mutex;
#else
   CRITICAL_SECTION mutex;
   char ready;
#endif
 } _aaxMutex;

#endif


void *_aaxThreadCreate();
int _aaxThreadSetAffinity(void *, int);
void _aaxThreadDestroy(void *);
int _aaxThreadStart(void *,  void *(*handler)(void*), void *);
// int _aaxThreadCancel(void *);
int _aaxThreadJoin(void *);


#ifndef NDEBUG
#define _aaxMutexCreate(a) _aaxMutexCreateDebug(a, __FILE__, __FUNCTION__);
#define _aaxMutexLock(a) _aaxMutexLockDebug(a, __FILE__, __LINE__)
#define _aaxMutexUnLock(a) _aaxMutexUnLockDebug(a, __FILE__, __LINE__)
void *_aaxMutexCreateDebug(void *, const char *, const char *);
int _aaxMutexLockDebug(void *, char *, int);
int _aaxMutexUnLockDebug(void *, char *, int);
#else
void *_aaxMutexCreate(void *);
int _aaxMutexLock(void *);
int _aaxMutexUnLock(void *);
#endif
void _aaxMutexDestroy(void *);

void *_aaxConditionCreate();
void _aaxConditionDestroy(void *);
int _aaxConditionWait(void *, void *);
int _aaxConditionWaitTimed(void *, void *, struct timespec *);
int _aaxConditionSignal(void *);

#if defined(__cplusplus)
}  /* extern "C" */
#endif

#endif /* !__THREADS_H */

