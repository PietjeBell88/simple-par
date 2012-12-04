#ifndef SPAR_OSDEP_H
#define SPAR_OSDEP_H

#define _LARGEFILE_SOURCE 1
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <sys/stat.h>
#include <inttypes.h>

#include "config.h"

#ifdef __ICL
#define inline __inline
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define snprintf _snprintf
#define strtok_r strtok_s
#define S_ISREG(x) (((x) & S_IFMT) == S_IFREG)
#endif

#if (defined(__GNUC__) || defined(__INTEL_COMPILER)) && (ARCH_X86 || ARCH_X86_64)
#define HAVE_X86_INLINE_ASM 1
#endif

#ifdef __ICL
#define DECLARE_ALIGNED( var, n ) __declspec(align(n)) var
#else
#define DECLARE_ALIGNED( var, n ) var __attribute__((aligned(n)))
#endif
#define ALIGNED_16( var ) DECLARE_ALIGNED( var, 16 )
#define ALIGNED_8( var )  DECLARE_ALIGNED( var, 8 )
#define ALIGNED_4( var )  DECLARE_ALIGNED( var, 4 )

#if defined(__GNUC__) && (__GNUC__ > 3 || __GNUC__ == 3 && __GNUC_MINOR__ > 0)
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#else
#ifdef __ICL
#define ALWAYS_INLINE __forceinline
#else
#define ALWAYS_INLINE inline
#endif
#endif

/* threads */
#if HAVE_POSIXTHREAD
#include <pthread.h>

#elif HAVE_WIN32THREAD
#include "extern/w32pthreads.h"

#else
#define pthread_t               int
#define pthread_create(t,u,f,d) f(d)
#define pthread_join(t,s)
#define pthread_mutex_t         int
#define pthread_mutex_init(m,f) 0
#define pthread_mutex_destroy(m)
#define pthread_mutex_lock(m)
#define pthread_mutex_unlock(m)
#define pthread_cond_t          int
#define pthread_cond_init(c,f)  0
#define pthread_cond_destroy(c)
#define pthread_cond_broadcast(c)
#define pthread_cond_wait(c,m)
#define pthread_attr_t          int
#define pthread_attr_init(a)    0
#define pthread_attr_destroy(a)
#define PTHREAD_MUTEX_INITIALIZER 0
#endif

#define WORD_SIZE sizeof(void*)

#endif /* SPAR_OSDEP_H */
