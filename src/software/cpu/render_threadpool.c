/*
 * Copyright 2013-2020 by Erik Hofman.
 * Copyright 2013-2020 by Adalin B.V.
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

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#ifdef HAVE_RMALLOC_H
# include <rmalloc.h>
#else
# include <stdlib.h>
#endif

#include <aax/aax.h>

#include <base/threads.h>
#include <base/logging.h>

#include <api.h>

#include "software/renderer.h"
#include "software/rbuf_int.h"

// Semaphores:
//   http://docs.oracle.com/cd/E19683-01/806-6867/sync-27385/index.html
//
// Windows Threadpool with semaphores:
//   http://msdn.microsoft.com/en-us/library/windows/desktop/ms686946%28v=vs.85%29.aspx


/*
 * The Pool renderer uses a thread worker with one thread tied to every
 * physical CPU core. This wil get the optimum rendering speed since it
 * can utilize the SSE registeres for every core simultaniously without
 * the possibility of choking the CPU caches.
 */

static _renderer_detect_fn _aaxWorkerDetect;
static _renderer_new_handle_fn _aaxWorkerSetup;
static _render_get_info_fn _aaxWorkerInfo;
static _renderer_open_fn _aaxWorkerOpen;
static _renderer_close_fn _aaxWorkerClose;
static _render_process_fn _aaxWorkerProcess;


_aaxRenderer*
_aaxDetectPoolRenderer()
{
   const char *env = getenv("AAX_USE_THREADPOOL");
   _aaxRenderer* rv = NULL;

   if ((!env || _aax_getbool(env)) && (_aaxGetNoCores() > 1))
   {
      rv = calloc(1, sizeof(_aaxRenderer));
      if (rv)
      {
         rv->detect = _aaxWorkerDetect;
         rv->setup = _aaxWorkerSetup;
         rv->info = _aaxWorkerInfo;

         rv->open = _aaxWorkerOpen;
         rv->close = _aaxWorkerClose;
         rv->process = _aaxWorkerProcess;
      }
   }
   return rv;
}

/* -------------------------------------------------------------------------- */

#define _AAX_MAX_NO_WORKERS		(2*8)
#define _AAX_MIN_EMITTERS_PER_WORKER	4

typedef struct
{
   _intBuffers *he;

   _aaxSemaphore *worker_start;
   _aaxSemaphore *worker_ready;
   _aaxMutex *mutex;

   int worker_no;
   int no_workers;
   int workers_busy;
   int max_emitters;
   int processed;
   int stage;

   struct threat_t thread[_AAX_MAX_NO_WORKERS];
   _aaxRendererData *data;

} _render_t;

static void* _aaxWorkerThread(void*);


static int
_aaxWorkerDetect() {
   return AAX_TRUE;
}

static void*
_aaxWorkerOpen(void* id)
{
   return id;
}

static int
_aaxWorkerClose(void* id)
{
   _render_t *handle = (_render_t*)id;
   int i;

   // set all worker-threads to inactive
   for (i=0; i<handle->no_workers; i++)
   {
      struct threat_t *thread = &handle->thread[i];
      thread->started = AAX_FALSE;
   }

   // signal and wait for the worker-threads to quit
   for (i=0; i<handle->no_workers; i++)
   {
      _aaxSemaphoreRelease(handle->worker_start);
      _aaxSemaphoreWait(handle->worker_ready);
   }

   // Wait until al worker threads are finished
   for (i=0; i<handle->no_workers; i++)
   {
      struct threat_t *thread = &handle->thread[i];

      _aaxThreadJoin(thread->ptr);
      if (thread->ptr) {
         _aaxThreadDestroy(thread->ptr);
      }
   }
   _aaxSemaphoreDestroy(handle->worker_start);
   _aaxSemaphoreDestroy(handle->worker_ready);
   _aaxMutexDestroy(handle->mutex);

   free(handle);

   return AAX_TRUE;
}

static void*
_aaxWorkerSetup(int dt)
{
   _render_t *handle = calloc(1, sizeof(_render_t));
   if (handle)
   {
      int i, res;

      // Assign a worker thread to every physical core but reserve
      // one core for processing of the mixer and audio-frames.
      handle->no_workers = _MIN(_aaxGetNoCores(), _AAX_MAX_NO_WORKERS);

      handle->mutex = _aaxMutexCreate(NULL);
      handle->worker_start = _aaxSemaphoreCreate(0);
      handle->worker_ready = _aaxSemaphoreCreate(0);

      for (i=0; i<handle->no_workers; i++)
      {
         struct threat_t *thread = &handle->thread[i];

         handle->worker_no = i;
         thread->ptr = _aaxThreadCreate();
         res = _aaxThreadStart(thread->ptr, _aaxWorkerThread, handle, dt);
         if (res == 0)
         {
            int q = 100;
            while (q-- && thread->started != AAX_TRUE) {
               msecSleep(1);
            }
         }
         else {
            _AAX_LOG(LOG_WARNING,  "Thread Pool renderer: thread failed");
         }
      }
      handle->worker_no = 0;
   }
   else {
      _AAX_LOG(LOG_WARNING, "Thread Pool renderer: Insufficient memory");
   }

   return (void*)handle;
}

static const char*
_aaxWorkerInfo(void *id)
{
   _render_t *handle = id;
   static char info[32] = "";

   if (handle && strlen(info) == 0)
   {
      const char *hwstr = _aaxGetSIMDSupportString();
#if RB_FLOAT_DATA
      int gpu = 0;
      snprintf(info, 32, "%s %s using %i cores", gpu ? "CL" : "FP", hwstr, handle->no_workers);
#else
      snprintf(info, 32, "%s using %i cores", hwstr, handle->no_workers);
#endif
   }

   return info;
}

/*
 * Wait for a worker thread to become ready.
 */
static int
_aaxWorkerProcess(struct _aaxRenderer_t *renderer, _aaxRendererData *data)
{
   _render_t *handle = renderer->id;
   _intBuffers *he = data->e3d;
   unsigned int stage;
   int rv = AAX_FALSE;

   assert(data);

   /*
    * process emitters
    */
   if (he)
   {
      stage = 2;
      do
      {
         unsigned int no_emitters, max_emitters;

         max_emitters = _intBufGetMaxNum(he, _AAX_EMITTER);
         no_emitters = _intBufGetNumNoLock(he, _AAX_EMITTER);
#ifdef NDEBUG
         if (no_emitters)
#endif
         {
            int num;

            handle->he = he;
            handle->stage = stage;
            handle->max_emitters = no_emitters ? max_emitters : 0;
            handle->data = data;

            // wake up the worker threads
            num = 1+(no_emitters/_AAX_MIN_EMITTERS_PER_WORKER);
            num = _MIN(handle->no_workers, num);
            _aaxAtomicIntAdd(&handle->workers_busy, num);
            if (num)
            {
               do {
                  _aaxSemaphoreRelease(handle->worker_start);
               }
               while (--num);
            }

            // Wait until al worker threads are finished
            _aaxSemaphoreWait(handle->worker_ready);

#ifndef NDEBUG
            // In DEBUG mode handle->processed is too slow to set the
            // processed flag in a timely manner causing rv always to
            // be AAX_FALSE.
            // As a result the audio-frame will not get rendered even if
            // there are active emitters, which causes silence.
            // Therefore always return AAX_TRUE in DEBUG mode.
            rv = AAX_TRUE;
#else
            rv = _aaxAtomicIntSet(&handle->processed, AAX_FALSE);
#endif
         }
         _intBufReleaseNum(he, _AAX_EMITTER);

         /*
          * stage == 2 is 3d positional audio
          * stage == 1 is stereo audio
          */
         if (stage == 2) {
            he = data->e2d;	/* switch to stereo */
         }
      }
      while (--stage); /* process 3d positional and stereo emitters */
   }

   /*
    * process convolution
    */
   else
   {
      _aaxRingBuffer *rb = data->drb;
      unsigned int no_tracks = rb->get_parami(rb, RB_NO_TRACKS);

      _aaxAtomicIntAdd(&handle->workers_busy, no_tracks);

      // wake up the worker threads
      for (stage=0; stage<no_tracks; ++stage)
      {
         handle->he = NULL;
         handle->stage = 0;
         handle->max_emitters = no_tracks;
         handle->data = data;

         _aaxSemaphoreRelease(handle->worker_start);
      }

      // Wait until al worker threads are finished
      _aaxSemaphoreWait(handle->worker_ready);

      rv = AAX_TRUE;
   }

   return rv;
}


/* ------------------------------------------------------------------------- */

static void*
_aaxWorkerThread(void *id)
{
   _render_t *handle = (_render_t*)id;
   struct threat_t *thread;
   int worker_no;

   worker_no = handle->worker_no;
   thread = &handle->thread[worker_no];

   _aaxThreadSetAffinity(thread->ptr, worker_no % _aaxGetNoCores());
   thread->started = AAX_TRUE;

   // wait for our first job
   _aaxSemaphoreWait(handle->worker_start);
   if (thread->started == AAX_TRUE)
   {
      int *busy = &handle->workers_busy;
      int *num = &handle->max_emitters;
      _aaxRendererData *data;
      _aaxRingBuffer *drb;

      data = handle->data;
      assert(data);

      drb = data->drb->duplicate(data->drb, AAX_TRUE, AAX_TRUE);
      drb->set_state(drb, RB_STARTED);

      _aaxThreadSetPriority(thread->ptr, AAX_HIGH_PRIORITY);
      do
      {
         data = handle->data;

         /*
          * process convolution
          */
         if (!handle->he)
         {
            int track = _aaxAtomicIntSub(num, 1) - 1;
            data->callback(data->drb, data, NULL, track);
         }

         /*
          * else: process up to 32 emitters at a time
          */
         else if (handle->max_emitters)
         {
            int max = _aaxAtomicIntSub(num, _AAX_MIN_EMITTERS_PER_WORKER);
            int r = AAX_FALSE;

             /*
             * It might be possible that other threads aleady processed
             * all emitters which causes max to turn negative here.
             */
            if (max > 0)
            {
               do
               {
                  int pos = _MAX(max - _AAX_MIN_EMITTERS_PER_WORKER, 0);
                  do
                  {
                     _intBufferData *dptr_src;

                     dptr_src =_intBufGet(handle->he, _AAX_EMITTER, pos);
                     if (dptr_src != NULL)
                     {
                        // _aaxProcessEmitter calls
                        // _intBufReleaseData(dptr_src, _AAX_EMITTER);
                        r |= data->callback(drb, data, dptr_src, handle->stage);
                     }
                  }
                  while(++pos < max);
                  max = _aaxAtomicIntSub(num, _AAX_MIN_EMITTERS_PER_WORKER);
               }
               while (max > 0);

               if (r) {
                  _aaxAtomicIntSet(&handle->processed, AAX_TRUE);
               }

               /* mix our own ringbuffer with that of the mixer */
               _aaxMutexLock(handle->mutex);
               data->drb->data_mix(data->drb, drb, NULL, AAX_TRACK_ALL);
               _aaxMutexUnLock(handle->mutex);

               /* clear our own ringbuffer for future use */
               drb->data_clear(drb);
               drb->set_state(drb, RB_REWINDED);
            }
         }

         /* if we're the last active worker trigger the signal */
         if (_aaxAtomicIntDecrement(busy) == 0) {
             _aaxSemaphoreRelease(handle->worker_ready);
         }

         _aaxSemaphoreWait(handle->worker_start);
      }
      while (thread->started == AAX_TRUE);

      drb->destroy(drb);
   }

   _aaxSemaphoreRelease(handle->worker_ready);

   return handle;
}
