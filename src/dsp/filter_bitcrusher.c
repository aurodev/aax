/*
 * Copyright 2007-2018 by Erik Hofman.
 * Copyright 2009-2018 by Adalin B.V.
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
# include <malloc.h>
#endif

#include <aax/aax.h>

#include "common.h"
#include "filters.h"
#include "api.h"

static float _aaxBitCrusherFilterMinMax(float, int, unsigned char);

static aaxFilter
_aaxBitCrusherFilterCreate(_aaxMixerInfo *info, enum aaxFilterType type)
{
   _filter_t* flt = _aaxFilterCreateHandle(info, type, 1);
   aaxFilter rv = NULL;

   if (flt)
   {
      _aaxSetDefaultFilter2d(flt->slot[0], flt->pos, 0);
      flt->slot[0]->destroy = _lfo_destroy;
      rv = (aaxFilter)flt;
   }
   return rv;
}

static int
_aaxBitCrusherFilterDestroy(_filter_t* filter)
{
   filter->slot[0]->destroy(filter->slot[0]->data);
   filter->slot[0]->data = NULL;
   free(filter);

   return AAX_TRUE;
}

static aaxFilter
_aaxBitCrusherFilterSetState(_filter_t* filter, int state)
{
   void *handle = filter->handle;
   aaxFilter rv = AAX_FALSE;
   int stereo;

   assert(filter->info);

   stereo = (state & AAX_LFO_STEREO) ? AAX_TRUE : AAX_FALSE;
   state &= ~AAX_LFO_STEREO;

   filter->state = state;
   switch (state & ~AAX_INVERSE)
   {
   case AAX_CONSTANT_VALUE:
   case AAX_TRIANGLE_WAVE:
   case AAX_SINE_WAVE:
   case AAX_SQUARE_WAVE:
   case AAX_SAWTOOTH_WAVE:
   case AAX_ENVELOPE_FOLLOW:
   {
      _aaxLFOData* lfo = filter->slot[0]->data;
      if (lfo == NULL) {
         filter->slot[0]->data = lfo = _lfo_create();
      }

      if (lfo)
      {
         float offs, depth;
         int constant;

         lfo->convert = _linear;
         lfo->state = filter->state;
         lfo->fs = filter->info->frequency;
         lfo->period_rate = filter->info->period_rate;
         lfo->envelope = AAX_FALSE;
         lfo->stereo_lnk = !stereo;

         offs = filter->slot[0]->param[AAX_LFO_OFFSET];
         depth = filter->slot[0]->param[AAX_LFO_DEPTH];
         if ((offs + depth) > 1.0f) {
            depth = 1.0f - offs;
         }

         lfo->min_sec = offs/lfo->fs;
         lfo->max_sec = lfo->min_sec + depth/lfo->fs;
         lfo->depth = 1.0f;
         lfo->offset = 0.0f;
         lfo->f = filter->slot[0]->param[AAX_LFO_FREQUENCY];
         lfo->inv = (state & AAX_INVERSE) ? AAX_TRUE : AAX_FALSE;

         if ((state & ~AAX_INVERSE) == AAX_ENVELOPE_FOLLOW)
         {
            lfo->min_sec = 0.5f*filter->slot[0]->param[AAX_LFO_OFFSET]/lfo->fs;
            lfo->max_sec = 0.5f*filter->slot[0]->param[AAX_LFO_DEPTH]/lfo->fs + lfo->min_sec;
         }

         constant = _lfo_set_timing(lfo);
         if (!_lfo_set_function(lfo, constant)) {
            _aaxErrorSet(AAX_INVALID_PARAMETER);
         }
      }
      else _aaxErrorSet(AAX_INSUFFICIENT_RESOURCES);
      break;
   }
   case AAX_FALSE:
      filter->slot[0]->destroy(filter->slot[0]->data);
      filter->slot[0]->data = NULL;
      break;
   default:
      _aaxErrorSet(AAX_INVALID_PARAMETER);
      break;
   }
   rv = filter;
   return rv;
}

static _filter_t*
_aaxNewBitCrusherFilterHandle(const aaxConfig config, enum aaxFilterType type, _aax2dProps* p2d, UNUSED(_aax3dProps* p3d))
{
   _handle_t *handle = get_driver_handle(config);
   _aaxMixerInfo* info = handle ? handle->info : _info;
   _filter_t* rv = _aaxFilterCreateHandle(info, type, 1);

   if (rv)
   {
      unsigned int size = sizeof(_aaxFilterInfo);

      memcpy(rv->slot[0], &p2d->filter[rv->pos], size);
      rv->slot[0]->destroy = p2d->filter[rv->pos].destroy;
      rv->slot[0]->data = NULL;

      rv->state = p2d->filter[rv->pos].state;
   }
   return rv;
}

static float
_aaxBitCrusherFilterSet(float val, int ptype, UNUSED(unsigned char param))
{
   float rv = val;
   return rv;
}

static float
_aaxBitCrusherFilterGet(float val, int ptype, UNUSED(unsigned char param))
{
   float rv = val;
   return rv;
}

static float
_aaxBitCrusherFilterMinMax(float val, int slot, unsigned char param)
{
  static const _flt_minmax_tbl_t _aaxBitCrusherRange[_MAX_FE_SLOTS] =
   {    /* min[4] */                  /* max[4] */
    { { 0.0f, 0.01f, 0.0f, 0.0f }, { 0.0f, 50.0f, 1.0f, 1.0f } },
    { { 0.0f, 0.0f,  0.0f, 0.0f }, { 0.0f,  0.0f, 0.0f, 0.0f } }, 
    { { 0.0f, 0.0f,  0.0f, 0.0f }, { 0.0f,  0.0f, 0.0f, 0.0f } },
    { { 0.0f, 0.0f,  0.0f, 0.0f }, { 0.0f,  0.0f, 0.0f, 0.0f } }
   };
   
   assert(slot < _MAX_FE_SLOTS);
   assert(param < 4);
   
   return _MINMAX(val, _aaxBitCrusherRange[slot].min[param],
                       _aaxBitCrusherRange[slot].max[param]);
}

/* -------------------------------------------------------------------------- */

_flt_function_tbl _aaxBitCrusherFilter =
{
   AAX_TRUE,
   "AAX_bitcrusher_filter", 1.0f,
   (_aaxFilterCreate*)&_aaxBitCrusherFilterCreate,
   (_aaxFilterDestroy*)&_aaxBitCrusherFilterDestroy,
   (_aaxFilterSetState*)&_aaxBitCrusherFilterSetState,
   (_aaxNewFilterHandle*)&_aaxNewBitCrusherFilterHandle,
   (_aaxFilterConvert*)&_aaxBitCrusherFilterSet,
   (_aaxFilterConvert*)&_aaxBitCrusherFilterGet,
   (_aaxFilterConvert*)&_aaxBitCrusherFilterMinMax
};

