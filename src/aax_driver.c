/*
 * Copyright 2007-2011 by Erik Hofman.
 * Copyright 2009-2011 by Adalin B.V.
 * All Rights Reserved.
 *
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Adalin B.V.;
 * the contents of this file may not be disclosed to third parties, copied or
 * duplicated in any form, in whole or in part, without the prior written
 * permission of Adalin B.V.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_LIBIO_H
#include <libio.h>		/* for NULL */
#endif
#ifdef HAVE_VALUES_H
#include <values.h>		/* for MAXFLOAT */
#endif
#include <math.h>		/* for INFINITY */
#include <string.h>		/* for calloc */
#include <strings.h>		/* for strcasecmp */
#include <stdlib.h>		/* for getenv */
#include <assert.h>

#include <xml.h>

#include <arch.h>
#include <ringbuffer.h>
#include <base/gmath.h>
#include <base/logging.h>

#include "api.h"
#include "devices.h"

#define USER_CONFIG_FILE	"/.aaxconfig.xml"
#define SYSTEM_CONFIG_FILE	"/etc/aax/config.xml"

static _intBuffers* _backends;
static const char* _aax_default_devname;
static const _oalRingBufferFilterInfo _aaxMixerDefaultEqualizer[2];
static time_t _tvnow = 0;

static _intBuffers* get_backends();
static _handle_t* _open_handle(aaxConfig);
static _aaxConfig* _aaxReadConfig(_handle_t*, const char*);
static void _aaxContextSetupHRTF(void *, unsigned int);
static void _aaxContextSetupSpeakers(void **, unsigned int);
static void removeMixerByPos(void *, unsigned int);
static int _aaxCheckKeyValidity(void*);
static int _aaxCheckKeyValidityStr(char*);

const char*
aaxDriverGetSetup(const aaxConfig config, enum aaxSetupType type)
{
   _handle_t *handle = get_handle(config);
   char *rv = NULL;
   if (handle)
   {
      const _aaxDriverBackend *be = handle->backend.ptr;
      switch(type)
      {
      case AAX_DRIVER_STRING:
         if (handle->backend.driver) {
            rv = (char*)be->renderer;
         } else {
            rv = (char*)be->driver;
         }
         break;
      case AAX_RENDERER_STRING:
         if (handle->backend.driver) {
            rv = (char*)handle->backend.driver;
         } else {
            rv = (char*)be->driver;
         }
         break;
      case AAX_VERSION_STRING:
         rv = (char*)be->version;
         break;
      case AAX_VENDOR_STRING:
         rv = (char*)be->vendor;
         break;
      default:
         _aaxErrorSet(AAX_INVALID_ENUM);
      }
   }
   else {
      _aaxErrorSet(AAX_INVALID_HANDLE);
   }
   return (const char*)rv;
}

unsigned
aaxDriverGetCount(enum aaxRenderMode mode)
{
   unsigned rv = 0;
   if (mode < AAX_MODE_WRITE_MAX)
   {
      _intBuffers* backends = get_backends();
      unsigned count = _intBufGetNumNoLock(backends, _AAX_BACKEND);
      unsigned i, m = (mode == AAX_MODE_READ) ? 0 : 1;
      for (i=0; i<count; i++)
      {
         _aaxDriverBackend *be = _aaxGetDriverBackendByPos(backends, i);
         if (be)
         {
            if (!m && be->support_recording(NULL)) rv++;
            else if (m && be->support_playback(NULL)) rv++;
         }
      }
   }
   else {
      _aaxErrorSet(AAX_INVALID_ENUM);
   }
   return rv;
}

aaxConfig
aaxDriverGetByPos(unsigned pos_req, enum aaxRenderMode mode)
{
   _handle_t *handle = NULL;

   if (mode < AAX_MODE_WRITE_MAX)
   {
      handle = new_handle();
      if (handle)
      {
         unsigned count = _intBufGetNumNoLock(handle->backends, _AAX_BACKEND);
         unsigned i,  m = (mode == AAX_MODE_READ) ? 0 : 1;
         _aaxDriverBackend *be = NULL;
         unsigned p = 0;

         for (i=0; i<count; i++)
         {
            be = _aaxGetDriverBackendByPos(handle->backends, i);
            if (be)
            {
               if ((!m && be->support_recording(NULL)) && (p++ == pos_req)) {
                  break;
               }
               else if ((m && be->support_playback(NULL)) && (p++ == pos_req)) {
                  break;
               }
            }
         }
 
         if (be)
         {
            handle->id = HANDLE_ID;
            handle->backend.ptr = be;
            handle->info->mode = mode;
//          handle->devname[0] = be->driver;
            handle->devname[0] = (char *)_aax_default_devname;

         }
         else
         {
            _aaxErrorSet(AAX_INVALID_PARAMETER);
            aaxDriverDestroy(handle);
            handle = NULL;
         }
      }
      else {
         _aaxErrorSet(AAX_INVALID_HANDLE);
      }
   }
   else {
      _aaxErrorSet(AAX_INVALID_ENUM);
   }
   return (aaxConfig)handle;
}

aaxConfig
aaxDriverGetByName(const char* name, enum aaxRenderMode mode)
{
   _handle_t *handle = NULL;

   if (mode < AAX_MODE_WRITE_MAX)
   {
      handle = new_handle();
      if (handle)
      {
         handle->info->mode = mode;
         if (name != NULL)
         {
            char *ptr;

            handle->devname[0] = _aax_strdup(name);
            ptr = strstr(handle->devname[0], " on ");
            if (ptr)
            {
               *ptr = 0;
               handle->devname[1] = ptr+4;	/* 4 = strlen(" on ") */
            }
            handle->backend.ptr = _aaxGetDriverBackendByName(handle->backends,
                                                            handle->devname[0]);
         }
         else /* name == NULL */
         {
            const _aaxDriverBackend *be;

            if (mode == AAX_MODE_READ) {
               be = _aaxGetDriverBackendDefaultCapture(handle->backends);
            } else {
               be = _aaxGetDriverBackendDefault(handle->backends);
            }

            handle->backend.ptr = be;
            if (be) { /* be == NULL should never happen */
               handle->devname[0] = _aax_strdup(be->driver);
            }
         }

         if (handle->backend.ptr == NULL)
         {
            _aaxErrorSet(AAX_INVALID_PARAMETER);
            if (handle->devname[0] != _aax_default_devname)
            {
               free(handle->devname[0]);
               handle->devname[0] = (char *)_aax_default_devname;
            }
            aaxDriverDestroy(handle);
            handle = NULL;
         }
      }
      else
      {
         _aaxErrorSet(AAX_INVALID_HANDLE);
      }
   }
   else {
      _aaxErrorSet(AAX_INVALID_ENUM);
   }
   return handle;
}

int
aaxDriverGetSupport(const aaxConfig config, enum aaxRenderMode mode)
{
   _handle_t *handle = get_handle(config);
   int rv = AAX_FALSE;

   if (handle)
   {
      const _aaxDriverBackend *be = handle->backend.ptr;

      switch (mode)
      {
      case AAX_MODE_READ:
         rv = be->support_recording(NULL);
         break;
      case AAX_MODE_WRITE_STEREO:
      case AAX_MODE_WRITE_SURROUND:
      case AAX_MODE_WRITE_HRTF:
         rv = be->support_playback(NULL);
         break;
      default:
         _aaxErrorSet(AAX_INVALID_ENUM);
      }
   } 
   else {
      _aaxErrorSet(AAX_INVALID_HANDLE);
   }
   return rv;
}

aaxConfig
aaxDriverOpenDefault(enum aaxRenderMode mode)
{
   aaxConfig config = aaxDriverGetByName(NULL, mode);
   return aaxDriverOpen(config);
}

aaxConfig
aaxDriverOpen(aaxConfig config)
{
   _handle_t *handle = _open_handle(config);
   if (handle)
   {
      _aaxConfig *cfg = _aaxReadConfig(handle, NULL);
      const _aaxDriverBackend *be = handle->backend.ptr;
      void *xoid, *nid = 0;

      if (cfg)
      {
         xoid = cfg->backend.output;
         if (be)
         {
            const char* name = handle->devname[1];
            enum aaxRenderMode mode = handle->info->mode;
            handle->backend.handle = be->connect(nid, xoid, name, mode);
         }
         _aaxDriverBackendClearConfigSettings(cfg);
      }
      else {
         _AAX_SYSLOG("invalid personal product key");
      }
   }

   if (!handle || !handle->backend.handle)
   {
      _aaxErrorSet(AAX_INVALID_DEVICE);
      aaxDriverClose(handle);
      aaxDriverDestroy(handle);
      handle = NULL;
   }
   return (aaxConfig)handle;
}

aaxConfig
aaxDriverOpenByName(const char* name, enum aaxRenderMode mode)
{
   _handle_t *handle = NULL;
   if (mode < AAX_MODE_WRITE_MAX)
   {
      if (name != NULL)
      {
         aaxConfig config = aaxDriverGetByName(name, mode);
         handle = _open_handle(config);
         if (handle)
         {
            _aaxConfig *cfg = _aaxReadConfig(handle, name);
            const _aaxDriverBackend *be = handle->backend.ptr;
            void *xoid, *nid = 0;

            if (mode == AAX_MODE_WRITE_STEREO) mode = handle->info->mode;
            else handle->info->mode = mode;

            if (cfg)
            {
               xoid = cfg->backend.output;
               if (be)
               {
                  const char* name = handle->devname[1];
                  handle->backend.handle = be->connect(nid, xoid, name, mode);
               }
               _aaxDriverBackendClearConfigSettings(cfg);
            }
            else {
               _AAX_SYSLOG("invalid personal product key");
            }
         }
   
         if (handle && !handle->backend.handle)
         {
            _aaxErrorSet(AAX_INVALID_PARAMETER);
            aaxDriverClose(handle);
            aaxDriverDestroy(handle);
            handle = NULL;
         }
      }
      else {
         handle = aaxDriverOpenDefault(mode);
      }
   }
   else {
      _aaxErrorSet(AAX_INVALID_ENUM);
   }
   return (aaxConfig)handle;
}

int
aaxDriverDestroy(aaxConfig config)
{
   _handle_t *handle = get_handle(config);
   int rv = AAX_FALSE;
   if (handle)
   {
      assert(handle->backends != NULL);
      _intBufErase(&handle->sensors, _AAX_SENSOR, removeMixerByPos, handle);

      if (handle->devname[0] != _aax_default_devname)
      {
         free(handle->devname[0]);
         handle->devname[0] = (char *)_aax_default_devname;
      }

      free(handle->backend.driver);

      /* safeguard against using already destroyed handles */
      handle->id = 0xdeadbeef;
      free(handle);

      _aaxRemoveDriverBackends(&_backends);
      rv = AAX_TRUE;
   }
   else {
      _aaxErrorSet(AAX_INVALID_HANDLE);
   }
   return rv;
};

int
aaxDriverClose(aaxConfig config)
{
   _handle_t *handle = get_valid_handle(config);
   int rv = AAX_FALSE;

   if (handle)
   {
      const _aaxDriverBackend *be = handle->backend.ptr;

      aaxMixerSetState(config, AAX_STOPPED);
      if (be && handle->backend.handle) {
         be->disconnect(handle->backend.handle);
      }
      rv = AAX_TRUE;
   }
   else {
      _aaxErrorSet(AAX_INVALID_HANDLE);
   }
   return rv;
}

unsigned
aaxDriverGetDeviceCount(const aaxConfig config, enum aaxRenderMode mode)
{
   unsigned int num = 0;

   if (mode < AAX_MODE_WRITE_MAX)
   {
      _handle_t *handle = get_handle(config);
      if (handle)
      {
         const _aaxDriverBackend *be = handle->backend.ptr;
         void* be_handle = handle->backend.handle;
         char *ptr;

         ptr = be->get_devices(be_handle, mode);
         while(ptr && *(ptr+1) != '\0')
         {
            ptr = strchr(ptr+1, '\0');
            ptr++;
            num++;
         }
      }
      else {
         _aaxErrorSet(AAX_INVALID_HANDLE);
      }
   }
   else {
      _aaxErrorSet(AAX_INVALID_ENUM);
   }
   return num;
}

const char*
aaxDriverGetDeviceNameByPos(const aaxConfig config, unsigned pos, enum aaxRenderMode mode)
{
   char *ptr = NULL;

   if (mode < AAX_MODE_WRITE_MAX)
   {
      _handle_t *handle = get_handle(config);
      if (handle)
      {
         const _aaxDriverBackend *be = handle->backend.ptr;
         void* be_handle = handle->backend.handle;
         unsigned int num = 0;

         ptr = be->get_devices(be_handle, mode);
         if (pos)
         {
            while(ptr && *(ptr+1) != '\0')
            {
               if (pos == num) break;
               ptr = strchr(ptr+1, '\0');
               ptr++;
               num++;
            }
            if (ptr == NULL) {
               _aaxErrorSet(AAX_INVALID_PARAMETER);
            }
         }
      }
      else {
         _aaxErrorSet(AAX_INVALID_HANDLE);
      }
   }
   else {
      _aaxErrorSet(AAX_INVALID_ENUM);
   }
   return ptr;
}

unsigned
aaxDriverGetInterfaceCount(const aaxConfig config, const char* devname, enum aaxRenderMode mode)
{
   unsigned int num = 0;

   if (mode < AAX_MODE_WRITE_MAX)
   {
      _handle_t *handle = get_handle(config);
      if (handle)
      {
         const _aaxDriverBackend *be = handle->backend.ptr;
         void* be_handle = handle->backend.handle;
         char *ptr;

         ptr = be->get_interfaces(be_handle, devname, mode);
         if (ptr && *ptr != '\0')
         {
            num++;
            ptr = strchr(ptr, '\0')+1;
            while(*ptr != '\0')
            {
               ptr = strchr(ptr, '\0')+1 ;
               num++;
            }
         }
      }
      else {
         _aaxErrorSet(AAX_INVALID_HANDLE);
      }
   }
   else {
      _aaxErrorSet(AAX_INVALID_ENUM);
   }
   return num;
}

const char*
aaxDriverGetInterfaceNameByPos(const aaxConfig config, const char* devname, unsigned pos, enum aaxRenderMode mode)
{
   char *ptr = NULL;

   if (mode < AAX_MODE_WRITE_MAX)
   {
      _handle_t *handle = get_handle(config);
      if (handle)
      {
         const _aaxDriverBackend *be = handle->backend.ptr;
         void* be_handle = handle->backend.handle;
         unsigned int num = 0;

         ptr = be->get_interfaces(be_handle, devname, mode);
         if (pos && ptr && *ptr != '\0')
         {
            num++;
            ptr = strchr(ptr, '\0')+1;
            while(*ptr != '\0')
            {
               if (pos == num) break;
               ptr = strchr(ptr, '\0')+1;
               num++;
            }
            if (ptr == NULL) {
               _aaxErrorSet(AAX_INVALID_PARAMETER);
            }
         }
      }
      else {
         _aaxErrorSet(AAX_INVALID_HANDLE);
      }
   }
   else {
      _aaxErrorSet(AAX_INVALID_ENUM);
   }
   return ptr;
}

/* -------------------------------------------------------------------------- */

static _intBuffers* _backends = NULL;
static const char* _aax_default_devname = "None";

static const _oalRingBufferFilterInfo _aaxMixerDefaultEqualizer[2] =
{
  { { 22050.0f, 1.0f, 1.0f }, NULL },
  { { 22050.0f, 1.0f, 1.0f }, NULL }
};

static _intBuffers*
get_backends()
{
   if (_backends == NULL) {
      _backends = _aaxGetDriverBackends();
   }
   return _backends;
}

_handle_t*
new_handle()
{
   unsigned long size;
   _handle_t *rv = NULL;
   void *ptr1;
   char *ptr2;

   size = sizeof(_handle_t);
   ptr2 = (char*)size;
   size += sizeof(_aaxMixerInfo);

   ptr1 = _aax_calloc(&ptr2, 1, size);
   if (ptr1)
   {
      _handle_t* handle = (_handle_t*)ptr1;
      _aaxMixerInfo* info;

      handle->id = HANDLE_ID;
      handle->backends = get_backends();

      handle->pos = UINT_MAX;
      _SET_PROCESSED(handle);

      info = (_aaxMixerInfo*)ptr2;
      _aax_memcpy(info, &_aaxDefaultMixerInfo, sizeof(_aaxMixerInfo));

      handle->info = info;
      rv = handle;
   }

   return rv;
}

_handle_t*
get_handle(aaxConfig config)
{
   _handle_t *handle = (_handle_t *)config;
   _handle_t *rv = NULL;

   if (handle && handle->id == HANDLE_ID) {
      rv = handle;
   }

   return rv;
}

_handle_t*
get_valid_handle(aaxConfig config)
{
   _handle_t *handle = (_handle_t *)config;
   _handle_t *rv = NULL;

   if (handle)
   {
      if (handle->id == HANDLE_ID)
      {
         if (handle->valid & ~HANDLE_ID) {
            rv = handle;
         }
      }
      else if (handle->id == AUDIOFRAME_ID) {
         rv = handle;
      }
   }

   return rv;
}

_handle_t*
get_write_handle(aaxConfig config)
{
   _handle_t *handle = (_handle_t *)config;
   _handle_t *rv = NULL;

   if (handle && handle->id == HANDLE_ID)
   {
      assert(handle->info);
      if (handle->info->mode != AAX_MODE_READ) {
         rv = handle;
      }
   }

   return rv;
}

_handle_t*
get_read_handle(aaxConfig config)
{
   _handle_t *handle = (_handle_t *)config;
   _handle_t *rv = NULL;

   if (handle && handle->id == HANDLE_ID)
   {
      assert(handle->info);
      if (handle->info->mode == AAX_MODE_READ) {
         rv = handle;
      }
   }

   return rv;
}

static _handle_t*
_open_handle(aaxConfig config)
{
   _handle_t *handle = (_handle_t *)config;

   if (handle && handle->id == HANDLE_ID)
   {
      assert (handle->backend.ptr != NULL);

      if (handle->sensors == NULL)
      {
         unsigned int res = _intBufCreate(&handle->sensors, _AAX_SENSOR);
         if (res != UINT_MAX)
         {
            unsigned long size;
            char* ptr2;
            void* ptr1;

            size = sizeof(_sensor_t) + sizeof(_aaxAudioFrame);
            ptr2 = (char*)size;

            size += sizeof(_oalRingBuffer2dProps);
            ptr1 = _aax_calloc(&ptr2, 1, size);

            if (ptr1)
            {
               _sensor_t* sensor = ptr1;
               _aaxAudioFrame* mixer;

               sensor->filter = handle->filter;
               size = EQUALIZER_MAX*sizeof(_oalRingBufferFilterInfo);
               memcpy(sensor->filter, &_aaxMixerDefaultEqualizer, size);

               size = sizeof(_sensor_t);
               mixer = (_aaxAudioFrame*)((char*)sensor + size);

               sensor->mixer = mixer;
               sensor->mixer->info = handle->info;

               assert(((long int)ptr2 & 0xF) == 0);

               mixer->props2d = (_oalRingBuffer2dProps*)ptr2;
               _aaxSetDefault2dProps(mixer->props2d);
               _EFFECT_SET2D(mixer,PITCH_EFFECT,AAX_PITCH,handle->info->pitch);

               ptr2 = (char*)0;
               size = sizeof(_oalRingBuffer3dProps);
               ptr1 = _aax_malloc(&ptr2, size);
               mixer->props3d_ptr = ptr1;
               mixer->props3d = (_oalRingBuffer3dProps*)ptr2;
               if (ptr1)
               {
                  _aaxSetDefault3dProps(mixer->props3d);
                  _EFFECT_SET3D_DATA(mixer, VELOCITY_EFFECT,
                                            _oalRingBufferDopplerFunc[0]);
                  _FILTER_SET3D_DATA(mixer, DISTANCE_FILTER,
                          _oalRingBufferDistanceFunc[AAX_EXPONENTIAL_DISTANCE]);
               }

               res = _intBufCreate(&mixer->emitters_3d, _AAX_EMITTER);
               if (res != UINT_MAX) {
                  res = _intBufCreate(&mixer->emitters_2d, _AAX_EMITTER);
               }
               if (res != UINT_MAX) {
                  res = _intBufCreate(&mixer->ringbuffers, _AAX_RINGBUFFER);
               }
               if (res != UINT_MAX)
               {
                  res=_intBufAddData(handle->sensors,_AAX_SENSOR, sensor);
                  if (res != UINT_MAX)
                  {
                     _aaxMixerInfo *info = handle->info;
                     unsigned int size, num;

                     sensor->count = 1;
                     sensor->mixer->thread = -1;
                     num = _oalRingBufferGetNoSources();
                     sensor->mixer->info->max_emitters = num;

                     size = _AAX_MAX_SPEAKERS;
                     memcpy(&info->router, &_aaxContextDefaultRouter, size);

                     info->no_tracks = 2;
                     size = _AAX_MAX_SPEAKERS * sizeof(vec4);
                     _aax_memcpy(&info->speaker,&_aaxContextDefaultSpeakers, size);

                     size = 2*sizeof(vec4);
                     _aax_memcpy(&info->hrtf, &_aaxContextDefaultHead, size);

                     _PROP_PITCH_SET_CHANGED(mixer->props3d);
                     _PROP_MTX_SET_CHANGED(mixer->props3d);

                     return handle;
                  }
                  _intBufErase(&mixer->ringbuffers, _AAX_RINGBUFFER, 0, 0);
               }
               /* creating the sensor failed */
               free(ptr1);
            }
            _intBufErase(&handle->sensors, _AAX_SENSOR, 0, 0);
         }
      }
   }
   return NULL;
}

_aaxConfig*
_aaxReadConfig(_handle_t *handle, const char *devname)
{
   _aaxConfig* config = calloc(1, sizeof(_aaxConfig));

   if (config)
   {
      _intBufferData *dptr;
      long tract_now;
      char *ptr, *name;
      void *xid, *be;
      float fq, iv;
      int key;

      /* read the default setup */
      key = AAX_TRUE;
      tract_now = _aaxDriverBackendSetConfigSettings(handle->backends,
                                                     handle->devname, config);
      if (!_tvnow) _tvnow = tract_now;

      /* read the system wide configuration file */
      xid = xmlOpen(SYSTEM_CONFIG_FILE);
      if (xid != NULL)
      {
         
         key = _aaxCheckKeyValidity(xid);
         _aaxDriverBackendReadConfigSettings(xid, handle->devname, config,
                                             SYSTEM_CONFIG_FILE);
         xmlClose(xid);
      }

      /* read the user configurstion file */
      ptr = getenv("HOME");
      if (ptr)
      {
         char *path = malloc(strlen(ptr) + strlen(USER_CONFIG_FILE)+1);
         if (path)
         {
            strcpy(path, ptr);
            strcat(path, USER_CONFIG_FILE);
            xid = xmlOpen(path);
            if (xid)
            {
               int res = _aaxCheckKeyValidity(xid);
               if ((key == AAX_TRUE) && res) key = res;
               _aaxDriverBackendReadConfigSettings(xid, handle->devname,
                                                   config, path);
               xmlClose(xid);
            }
            free(path);
         }
      }

      if (key)
      {
         name = handle->devname[0];
         if (name == _aax_default_devname)
         {
            if (devname) {
               name = strdup((char *)devname);
            }
            else if (config->node[0].devname) {
               name = strdup(config->node[0].devname);
            }

            handle->devname[0] = name;
            ptr = strstr(name, " on ");
            if (ptr)
            {
               *ptr = 0;
               handle->devname[1] = ptr+4;		/* 4 = strlen(" on ") */
            }
         }

         be = _aaxGetDriverBackendByName(handle->backends, name);
         if (be || (handle->devname[0] != _aax_default_devname)) {
            handle->backend.ptr = be;
         }
         handle->backend.driver = strdup(config->backend.driver);

         key ^= 0x21051974;
         if (config->node[0].no_emitters)
         {
            int emitters = config->node[0].no_emitters;
            int system_max = _oalRingBufferGetNoSources();
            handle->info->max_emitters = _MINMAX(emitters, 4, system_max);
            _oalRingBufferSetNoSources(handle->info->max_emitters);
         }
         else {
            handle->info->max_emitters = _oalRingBufferGetNoSources();
         }

         ptr = config->node[0].setup;
         if (ptr && handle->info->mode == AAX_MODE_WRITE_STEREO)
         {
            if (!strcasecmp(ptr, "surround")) {
               handle->info->mode = AAX_MODE_WRITE_SURROUND;
            } else if (!strcasecmp(ptr, "hrtf")) {
               handle->info->mode = AAX_MODE_WRITE_HRTF;
            } else if (!strcasecmp(ptr, "spatial")) {
               handle->info->mode = AAX_MODE_WRITE_SPATIAL;
            } else if (strcasecmp(ptr, "stereo")) {
               handle->info->mode = AAX_MODE_WRITE_STEREO;
            }
         }

         key = _bswap32(key);
         if (config->node[0].no_speakers > 0) {
            handle->info->no_tracks = config->node[0].no_speakers;
         }

         fq = config->node[0].frequency;
         iv = config->node[0].interval;

         /* place config info in the syslog, if enabled */
         /* extra validiy time check */
         /* first generated key was at time(NULL) = 1315324133 */
         if (1315324133 <= key && key < _tvnow)
         {
            if (fq < _AAX_MIN_MIXER_FREQUENCY) {
               fq = _AAX_MIN_MIXER_FREQUENCY;
            }
            if (fq > _AAX_MAX_MIXER_FREQUENCY) {
               fq = _AAX_MAX_MIXER_FREQUENCY;
            }
            if (iv < _AAX_MIN_MIXER_REFRESH_RATE) {
               iv = _AAX_MIN_MIXER_REFRESH_RATE;
            }
            if (iv > _AAX_MAX_MIXER_REFRESH_RATE) {
               iv = _AAX_MAX_MIXER_REFRESH_RATE;
            }
            iv = fq / (float)get_pow2((unsigned)ceilf(fq / iv));
            handle->info->refresh_rate = iv;
            handle->info->frequency = fq;
            handle->info->update_rate = iv/config->node[0].update;

            /* key is valid */
            handle->valid = HANDLE_ID;
         } 
         else
         {
            if (fq < _AAX_MIN_MIXER_FREQUENCY) {
               fq = _AAX_MIN_MIXER_FREQUENCY;
            }
            if (fq > _AAX_MAX_MIXER_FREQUENCY_LT) {
               fq = _AAX_MAX_MIXER_FREQUENCY_LT;
            }
            if (iv < _AAX_MIN_MIXER_REFRESH_RATE) {
               iv = _AAX_MIN_MIXER_REFRESH_RATE;
            }
            if (iv > _AAX_MAX_MIXER_REFRESH_RATE_LT) {
               iv = _AAX_MAX_MIXER_REFRESH_RATE_LT;
            }
            iv = fq / (float)get_pow2((unsigned)ceilf(fq / iv));
            handle->info->refresh_rate = iv;
            handle->info->frequency = fq;
            handle->info->update_rate = iv/config->node[0].update;
            handle->info->max_emitters =  _AAX_MAX_MIXER_SOURCES_LT;
            _oalRingBufferSetNoSources(handle->info->max_emitters);
         }

         dptr = _intBufGet(handle->sensors, _AAX_SENSOR, 0);
         if (dptr)
         {
            _sensor_t* sensor = _intBufGetDataPtr(dptr);
            _aaxMixerInfo* info = sensor->mixer->info;
            unsigned int size;

            size = _AAX_MAX_SPEAKERS * sizeof(vec4);
            if (handle->info->mode == AAX_MODE_WRITE_HRTF)
            {
               handle->info->no_tracks = 2;
               _aax_memcpy(&info->speaker,&_aaxContextDefaultSpeakersHRTF,size);

               /*
                * By mulitplying the delays with the sample frequency the delays
                * in seconds get converted into sample offsets.
                */
               _aaxContextSetupHRTF(config->node[0].hrtf, 0);
               vec4Copy(info->hrtf[0], _aaxContextDefaultHead[0]);
               vec4ScalarMul(info->hrtf[0], fq);

               vec4Copy(info->hrtf[1], _aaxContextDefaultHead[1]);
               vec4ScalarMul(info->hrtf[1], fq);
            }
            else
            {
               _aaxContextSetupSpeakers(config->node[0].speaker,info->no_tracks);
               _aax_memcpy(&info->speaker,&_aaxContextDefaultSpeakers, size);
            }
            _intBufReleaseData(dptr, _AAX_SENSOR);
         }

         do
         {
            char buffer[1024], *buf = (char *)&buffer;
            _intBuffers* backends = get_backends();
            unsigned int i, count;

            for (i=0; i<config->no_nodes; i++)
            {
               snprintf(buf,1024,"output[%i]: '%s'\n", i, config->node[i].devname);
              _AAX_SYSLOG(buf);

               snprintf(buf,1024,"setup: %s\n", config->node[i].setup);
               _AAX_SYSLOG(buf);

               snprintf(buf,1024,"frequency: %5.1f, interval: %5.1f\n",
                        handle->info->frequency, handle->info->refresh_rate);
               _AAX_SYSLOG(buf);
            }

            count = _intBufGetNumNoLock(backends, _AAX_BACKEND);
            for (i=0; i<count; i++)
            {
               _aaxDriverBackend *be = _aaxGetDriverBackendByPos(backends, i);
               if (be)
               {
                  snprintf(buf,1024,"backend[%i]: '%s'\n", i, be->driver);
                  _AAX_SYSLOG(buf);
               }
            }
         }
         while (0);
      }
      else
      {
         free(config);
         config = NULL;
      }
   }

   return config;
}

static void
_aaxContextSetupHRTF(void *xid, unsigned int n)
{
   if (xid)
   {
      float f = xmlNodeGetDouble(xid, "gain");
      _aaxContextDefaultHead[HRTF_FACTOR][GAIN] = f;

      /* need to adjust for a range of 0.0 .. +1.0 where 0.5 is the center */
      f = xmlNodeGetDouble(xid, "side-delay-sec");
      _aaxContextDefaultHead[HRTF_FACTOR][DIR_RIGHT] = -f*2.0f;
      _aaxContextDefaultHead[HRTF_OFFSET][DIR_RIGHT] = f;

      f = xmlNodeGetDouble(xid, "forward-delay-sec");
      _aaxContextDefaultHead[HRTF_FACTOR][DIR_BACK] = f;

      f = xmlNodeGetDouble(xid, "up-delay-sec");
      _aaxContextDefaultHead[HRTF_FACTOR][DIR_UPWD] = -f;

      f += xmlNodeGetDouble(xid, "up-offset-sec");
      _aaxContextDefaultHead[HRTF_OFFSET][DIR_UPWD] = f;
   }
}

static void
_aaxContextSetupSpeakers(void **speaker, unsigned int n)
{
   unsigned int i;

   for (i=0; i<n; i++)
   {
      void *xsid = speaker[i];

      if (xsid)
      {
         unsigned int channel;
         float f;
         vec3 v;

         channel = xmlNodeGetInt(xsid, "channel");
         if (channel >= n) channel = n-1;
         _aaxContextDefaultRouter[i] = channel;

         f = xmlNodeGetDouble(xsid, "volume-norm");
         _aaxContextDefaultSpeakers[channel][GAIN] = f;

         v[0] = -xmlNodeGetDouble(xsid, "pos-x");
         v[1] = -xmlNodeGetDouble(xsid, "pos-y");
         v[2] = xmlNodeGetDouble(xsid, "pos-z");
         /* vec3Normalize(_aaxContextDefaultSpeakers[channel], v); */
         vec3Copy(_aaxContextDefaultSpeakers[channel], v);
      }
   }
}


static int
_aaxCheckKeyValidityStr(char *keystr)
{
   int rv = 0;
   if (keystr && (strlen(keystr) == 26))
   {
      int base = strlen(keystr)+1;   /* 27 */
      char *nptr, *eptr;
      uint64_t key, tmp;

      nptr = keystr;
      eptr = strchr(nptr, '-');
      key = strtoll(nptr, &eptr, base);

      nptr = eptr+1;
      eptr = strchr(nptr, '-');
      tmp = strtoll(nptr, &eptr, base);
      if (is_bigendian()) {
         key += *((uint32_t*)(&tmp)+1);
      } else {
         key += *(uint32_t*)(&tmp);
      }
#if 0
// printf("tmp: %llx\n", tmp);
// printf("*(uint32_t*)(&tmp): %x\n((uint32_t*)(&tmp)+1): %x\n", *(uint32_t*)(&tmp), *((uint32_t*)(&tmp)+1));
#endif

      nptr = eptr+1;
      eptr = nptr+strlen(nptr);
      key -= strtoll(nptr, &eptr, base);
      if (((key^HANDLE_ID) % 29723) == (7*strlen(keystr)-5)) {	/* 177 */
         rv = tmp & 0xFFFFFFFF;
      }
   }
   return rv;
}

static int
_aaxCheckKeyValidity(void *xid)
{
   void *xcid = xmlNodeGet(xid, "/configuration");
   int rv = AAX_FALSE;

   if (xcid)
   {
      char keystr[27];
      xmlNodeCopyString(xcid, "product-key", keystr, 27);
      rv = _aaxCheckKeyValidityStr(keystr);
      xmlFree(xcid);
   }
   return rv;
}

void
_aaxRemoveRingBufferByPos(void *frame, unsigned int pos)
{
   _aaxAudioFrame* mixer = (_aaxAudioFrame*)frame;
   _oalRingBuffer *rb;

   rb = _intBufRemove(mixer->ringbuffers, _AAX_RINGBUFFER, pos, AAX_FALSE);
   if (rb) {
      _oalRingBufferDelete(rb);
   }
}

static void
removeMixerByPos(void *config, unsigned int pos)
{
   _handle_t* handle = (_handle_t*)config;
   _sensor_t* sensor;

   sensor = _intBufRemove(handle->sensors, _AAX_SENSOR, pos, AAX_FALSE);
   if (sensor)
   {
      _aaxAudioFrame* mixer = sensor->mixer;

      /* frees both EQUALIZER_LF and EQUALIZER_HF */
      free(sensor->filter[EQUALIZER_LF].data);

      free(_FILTER_GET2D_DATA(mixer, FREQUENCY_FILTER));
      free(_FILTER_GET2D_DATA(mixer, DYNAMIC_GAIN_FILTER));
      free(_FILTER_GET2D_DATA(mixer, TIMED_GAIN_FILTER));
      free(_EFFECT_GET2D_DATA(mixer, DYNAMIC_PITCH_EFFECT));
      free(mixer->props3d_ptr);
      /* mixer->ringbuffer gets removed bij the mixer thread */
      /* _oalRingBufferDelete(mixer->ringbuffer); */
      _intBufErase(&mixer->frames, _AAX_FRAME, 0, 0);
      _intBufErase(&mixer->emitters_2d, _AAX_EMITTER, 0, 0);
      _intBufErase(&mixer->emitters_3d, _AAX_EMITTER, 0, 0);
      _intBufErase(&mixer->ringbuffers, _AAX_RINGBUFFER,
                   _aaxRemoveRingBufferByPos, sensor->mixer);
      free(sensor);
   }
}
