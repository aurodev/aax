/*
 * Copyright 2007-2017 by Erik Hofman.
 * Copyright 2009-2017 by Adalin B.V.
 *
 * This file is part of AeonWave
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  version 3 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 */

#ifndef AAX_API_H
#define AAX_API_H 1

#if defined(__cplusplus)
extern "C" {
#endif

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <aax/aax.h>

#include <base/threads.h>
#include <base/gmath.h>

#include "ringbuffer.h"
#include "objects.h"

#define USE_SPATIAL_FOR_SURROUND	AAX_TRUE
#define	FADEDBAD			0xfadedbad

#if _WIN32
# ifndef WIN32
#  pragma waring _WIN32 defined but not WIN32
#  define WIN32			_WIN32
# endif
#endif

#define TEST_FOR_TRUE(x)	(x != AAX_FALSE)
#define TEST_FOR_FALSE(x)	(x == AAX_FALSE)

#define INFO_ID			0xFEDCBA98
//#define EBF_VALID(a)		((a)->info && ((a)->info)->id == INFO_ID)
#define EBF_VALID(a)		((a)->info && VALID_HANDLE((_handle_t*)((a)->info)->backend))


#define _READ			AAX_FALSE
#define _WRITE			AAX_TRUE

#define _NOLOCK			AAX_FALSE
#define _LOCK			AAX_TRUE

/* --- data buffers -- */
#define DATA_ID	0xDFA82736
typedef struct _data_st
{
   unsigned int id;

   unsigned int blocksize;
   size_t size;
   size_t avail;
   unsigned char *data;

} _data_t;

_data_t* _aaxDataCreate(size_t, unsigned int);
int _aaxDataDestroy(_data_t*);
size_t _aaxDataAdd(_data_t*, void*, size_t);
size_t _aaxDataMove(_data_t*, void*, size_t);
size_t _aaxDataMoveData(_data_t*, _data_t*, size_t);


/* --- Error support -- */
#define _aaxErrorSet(a)		__aaxDriverErrorSet(handle,(a),__func__)
enum aaxErrorType __aaxDriverErrorSet(aaxConfig,enum aaxErrorType, const char*);
enum aaxErrorType __aaxErrorSet(enum aaxErrorType, const char*);

unsigned long long _aax_get_free_memory();

/* --- Sensor --- */
#define CAPTURE_ID	0x8FB82DEF

typedef struct
{
   _aaxAudioFrame *mixer;

   /* parametric equalizer, located at _handle_t **/
   void *mutex;
   _aaxFilterInfo *filter;

} _sensor_t;


/* --- Driver --- */
/* Note: several validation mechanisms rely on this value.
 *       be sure before changing.
 */
#define HANDLE_ID	0xFF2701E0

#define VALID_HANDLE(h)		((h) && ((h)->valid & ~AAX_TRUE) == HANDLE_ID)
#define VALID_MIXER(h)		(VALID_HANDLE(h) && ((h)->valid & AAX_TRUE))
#define INTERVAL(a)		(rintf((a)/64)*64)

extern int __release_mode;
extern _aaxMixerInfo* _info;

struct backend_t
{
   char *driver;
   void *handle;
   const _aaxDriverBackend *ptr;
};

struct threat_t
{
   void *ptr;
   _aaxSignal signal;
   char started;
   char initialized;
};

typedef struct
{
   unsigned int id;

   unsigned int valid;
   int state;
   enum aaxErrorType error;
   unsigned int be_pos;
   unsigned int mixer_pos;
   int registered_sensors;
   void *parent;		/* assigned when registered to a (sub)mixer  */
   void *root;			/* reference to the mixer object             */

   char *devname[2];
   char *data_dir;
   _aaxMixerInfo *info;
   _intBuffers *sensors;		/* locked sensor and scene properies */
   _intBuffers *backends;
   struct backend_t backend;
   struct backend_t file;		/* file recording backend */

   struct threat_t thread;
   _aaxSignal buffer_ready;
   _aaxSemaphore *finished;		/* released after a rendering period */

   /* destination ringbuffer */
   _aaxRingBuffer *ringbuffer;
   float dt_ms;

   /* timing */
   _aaxTimer *timer;
   float elapsed;

   /* parametric equalizer **/
   _aaxFilterInfo filter[EQUALIZER_MAX];

   /* for AAXS defined filters and effects */
   aaxBuffer buffer;

   /* thread for AAXS defined waveform generation */
   struct threat_t aaxs_thread;

} _handle_t;

_handle_t* new_handle();
_handle_t* get_handle(aaxConfig, const char*);
_handle_t* get_valid_handle(aaxConfig, const char*);
_handle_t* get_read_handle(aaxConfig, const char*);
_handle_t* get_write_handle(aaxConfig, const char*);
void _aaxDriverFree(void*);
int get_low_resource();

/* --- AudioFrame --- */
#define AUDIOFRAME_ID   0x3137ABFF

typedef struct
{
   unsigned int id;

   unsigned int mixer_pos;
   unsigned int max_emitters;

   void *parent;		/* assigned when registered to a (sub)mixer */
   _handle_t *root;		/* reference to the mixer object            */

   _aaxAudioFrame *submix;

   /* parametric equalizer **/
   void *mutex;
   _aaxFilterInfo *filter;

   char mtx_set;

} _frame_t;

_frame_t* get_frame(aaxFrame, int, const char*);
void put_frame(aaxFrame);
_handle_t *get_driver_handle(aaxFrame);
int _aaxAudioFrameStop(_frame_t*);
void _aaxAudioFrameResetDistDelay(_aaxAudioFrame*, _aaxAudioFrame*);
void _aaxAudioFrameFree(void*);

/* --- Buffer --- */
#define BUFFER_ID       0x81ACFE07

#define DEFAULT_IMA4_BLOCKSIZE		36
#define IMA4_SMP_TO_BLOCKSIZE(a)	(((a)/2)+4)
#define IMA4_BLOCKSIZE_TO_SMP(a)	((a) > 1) ? (((a)-4)*2) : 1
#define MAX_PITCH_LEVELS		8

typedef enum
{
   WAVEFORM_LIMIT_NORMAL  = 0x00,
   WAVEFORM_LIMIT_DELAYED = 0x01,
   WAVEFORM_LIMIT_STRONG  = 0x02
} limitType;

typedef struct
{
   unsigned int id;	/* always first */
   unsigned int ref_counter;

   int blocksize;
   unsigned int pos;
   unsigned int no_tracks, no_samples;
   unsigned int loop_start, loop_end;
   enum aaxFormat format;
   float rms, peak, gain;
   float frequency;
   float rate;

   char to_mixer;
   char mipmap;

   char pitch_levels;
   _aaxRingBuffer *ringbuffer[MAX_PITCH_LEVELS];
   _aaxMixerInfo **info;
   void *root;			/* reference to the mixer object */
   void *aaxs;
   void *url;

} _buffer_t;

typedef struct
{
   _buffer_t* parent;
   const void *aaxs;
   float frequency;
   enum aaxErrorType error;

} _buffer_aax_t;

_buffer_t* new_buffer(_handle_t*, unsigned int, enum aaxFormat, unsigned);
_buffer_t* get_buffer(aaxBuffer, const char*);
int free_buffer(_buffer_t*);

char** _bufGetDataFromStream(const char*, int*, unsigned int*, float*, size_t*, size_t*);
void _aaxFileDriverWrite(const char*, enum aaxProcessingType, void*, size_t, size_t, char, enum aaxFormat);

/* --- Emitter --- */
#define EMITTER_ID	0x17F533AA
#define EMBUFFER_ID	0xABD82641

typedef struct
{
   unsigned int id;	/* always first */

   unsigned int mixer_pos;	/* rigsitered emitter position            */
   unsigned char track;		/* which track to use from the buffer     */
   char looping;
   char mtx_set;

   _aaxEmitter *source;

   void *parent;		/* assigned when registered to a (sub)mixer */
   _handle_t *root;		/* reference to the mixer                   */

} _emitter_t;

typedef struct
{
   unsigned int id;	/* always first */

   void *aaxs;
   _buffer_t *buffer;
   _aaxRingBuffer *ringbuffer;
} _embuffer_t;

_emitter_t* get_emitter(aaxEmitter, int, const char*);
_emitter_t* get_emitter_unregistered(aaxEmitter, const char*);
void put_emitter(aaxEmitter);
int destory_emitter(aaxEmitter);
void emitter_remove_buffer(_aaxEmitter *);

void _aaxEMitterResetDistDelay(_aaxEmitter*, _aaxAudioFrame*);

/* -- Filters and Effects -- */

#define FILTER_ID	0x887AFE21
#define EFFECT_ID	0x21EFA788

/* --- WaveForms --- */
void *_bufferWaveCreateWhiteNoise(float, int, float, unsigned int*);
void *_bufferWaveCreatePinkNoise(float, int, float, unsigned int*);
void *_bufferWaveCreateSine(float, float, int, float, unsigned int*);
void *_bufferWaveCreateTriangle(float, float, int, float, unsigned int*);
void *_bufferWaveCreateSquare(float, float, int, float, unsigned int*);
void *_bufferWaveCreateSawtooth(float, float, int, float, unsigned int*);
void *_bufferWaveCreateImpulse(float, float, int, float, float, unsigned int*);

/* --- Logging --- */
# include <base/logging.h>

enum
{
    _AAX_NONE = 0,
    _AAX_BACKEND,
    _AAX_DEVICE,
    _AAX_BUFFER,
    _AAX_EMITTER,
    _AAX_EMITTER_BUFFER,
    _AAX_SENSOR,
    _AAX_FRAME,
    _AAX_RINGBUFFER,
    _AAX_EXTENSION,
    _AAX_DELAYED3D,
    _AAX_BUFFER_CACHE,
    _AAX_EMITTER_CACHE,
    _AAX_FRAME_CACHE,

    _AAX_NOTE,

    _AAX_MAX_ID
};

extern const char* _aax_id_s[_AAX_MAX_ID];

#ifndef NDEBUG
# define LOG_LEVEL	LOG_ERR
# define _AAX_LOG(a, c)	__aax_log((a),0,(const char*)(c), _aax_id_s, LOG_LEVEL)
# define _AAX_SYSLOG(c) __aax_log(LOG_SYSLOG, 0, (c), _aax_id_s, LOG_SYSLOG)
#else
#ifdef HAVE_RMALLOC_H
# include <rmalloc.h>
#else
# include <stdlib.h>
# include <string.h>
#endif
# define _AAX_LOG(a, c)
# define _AAX_SYSLOG(c) __aax_log(LOG_SYSLOG, 0, (c), _aax_id_s, LOG_SYSLOG)
#endif

#ifndef NDEBUG
# define DBG_TESTNAN(a, b)		do { unsigned int i; for (i=0;i<(b);i++) if (is_nan((a)[i])) { printf("%s line %i\n\tNaN detetced at pos %i\n", __FILE__, __LINE__, i); exit(-1); } } while(0);
# define DBG_MEMCLR(a, b, c, d)         if (a) memset((void*)(b), 0, (c)*(d))
# define WRITE(a, b, dptr, ds, no_samples) \
   if (a) { static int ct = 0; if (++ct > (b)) { \
             WRITE_BUFFER_TO_FILE(dptr-ds, ds+no_samples); } }
#else
# define DBG_TESTNAN(a, b)
# define DBG_MEMCLR(a, b, c, d)
# define WRITE(a, b, dptr, ds, no_samples) \
        printf("Need to turn on debugging to use the WRITE macro\n")
#endif


/* --- System Specific & Config file related  --- */

void _aaxBackendDriverToDeviceConnecttor(char **, char **);
void _aaxDeviceConnecttorToBackendDriver(char **, char **);
void _aaxConnectorDeviceToDeviceConnector(char *);
void _aaxURLSplit(char*, char**, char**, char**, char**, int*);
char *_aaxURLConstruct(char*, char*);

const char* tmpDir();
const char* userHomeDir();
char* systemDataFile(const char*);
char* systemConfigFile(const char*);
char* userConfigFile();
char* userCacheFile(const char*);
int mkDir(const char*);
char* systemLanguage(char**);

#ifdef WIN32
char* _aaxGetEnv(const char*);
int _aaxSetEnv(const char*, const char*, int);
int _aaxUnsetEnv(const char*);
#else
# define _aaxGetEnv(a)			getenv(a)
# define _aaxSetEnv(a,b,c)		setenv((a),(b),(c))
# define _aaxUnsetEnv(a)		unsetenv(a)
#endif

#if defined(__cplusplus)
}  /* extern "C" */
#endif

#endif

