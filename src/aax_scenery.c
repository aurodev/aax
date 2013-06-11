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

#include "api.h"

#if 0
AAX_API int AAX_APIENTRY
aaxScenerySetDiffuseFactor(aaxConfig config, float diffuse_factor)
{
   _handle_t *handle = get_handle(config);
   int rv = AAX_FALSE;
   if (handle)
   {
      if (!is_nan(diffuse_factor))
      {
         _intBufferData *dptr = _intBufGet(handle->sensors, _AAX_SENSOR, 0);
         if (dptr)
         {
            _sensor_t* sensor = _intBufGetDataPtr(dptr);
            _aaxAudioFrame* mixer = sensor->mixer;
            mixer->scene.reflection = diffuse_factor;
            _intBufReleaseData(dptr, _AAX_SENSOR);
            rv = AAX_TRUE;
         }
      }
      else {
         _aaxErrorSet(AAX_INVALID_PARAMETER);
      }
   }
   else {
      _aaxErrorSet(AAX_INVALID_HANDLE); 
   }
   return rv;
}

AAX_API int AAX_APIENTRY
aaxScenerySetDimension(aaxConfig config, aaxVec3f dimension)
{
   _handle_t *handle = get_handle(config);
   int rv = AAX_FALSE;
   if (handle)
   {
      if (dimension && !detect_nan_vec3(dimension))
      {
         _intBufferData *dptr = _intBufGet(handle->sensors, _AAX_SENSOR, 0);
         if (dptr)
         {
            _sensor_t* sensor = _intBufGetDataPtr(dptr);
            _aaxAudioFrame* mixer = sensor->mixer;
            memcpy(&mixer->scene.dimension, dimension, sizeof(aaxVec3f));
            _intBufReleaseData(dptr, _AAX_SENSOR);
            rv = AAX_TRUE;
         }
      }
      else {
         _aaxErrorSet(AAX_INVALID_PARAMETER);
      }
   }
   else {
      _aaxErrorSet(AAX_INVALID_HANDLE);
   }
   return rv;
}

AAX_API int AAX_APIENTRY
aaxScenerySetPosition(aaxConfig config, aaxVec3f pos)
{
   _handle_t *handle = get_handle(config);
   int rv = AAX_FALSE;
   if (handle)
   {
      if (pos && !detect_nan_vec3(pos))
      {
         _intBufferData *dptr = _intBufGet(handle->sensors, _AAX_SENSOR, 0);
         if (dptr)
         {
            _sensor_t* sensor = _intBufGetDataPtr(dptr);
            _aaxAudioFrame* mixer = sensor->mixer;
            memcpy(&mixer->scene.pos, pos, sizeof(aaxVec3f));
            _intBufReleaseData(dptr, _AAX_SENSOR);
            rv = AAX_TRUE;
         }
      }
      else {
         _aaxErrorSet(AAX_INVALID_PARAMETER);
      }
   }
   else {
      _aaxErrorSet(AAX_INVALID_HANDLE);
   }
   return rv;
}
#endif

AAX_API int AAX_APIENTRY
aaxScenerySetFilter(aaxConfig config, aaxFilter f)
{
   _handle_t* handle = get_handle(config);
   int rv = AAX_FALSE;
   if (handle)
   {
      _filter_t* filter = get_filter(f);
      if (filter)
      {
         const _intBufferData* dptr;
         dptr = _intBufGet(handle->sensors, _AAX_SENSOR, 0);
         if (dptr)
         {
            _sensor_t* sensor = _intBufGetDataPtr(dptr);
            _aaxAudioFrame* mixer = sensor->mixer;
            int type = filter->pos;
            switch (filter->type)
            {
            case AAX_FREQUENCY_FILTER:
            {
               _oalRingBuffer2dProps *p2d = mixer->props2d;
               _FILTER_SET(p2d, type, 0, _FILTER_GET_SLOT(filter, 0, 0));
               _FILTER_SET(p2d, type, 1, _FILTER_GET_SLOT(filter, 0, 1));
               _FILTER_SET(p2d, type, 2, _FILTER_GET_SLOT(filter, 0, 2));
               _FILTER_SET(p2d, type, 3, _FILTER_GET_SLOT(filter, 0, 3));
               _FILTER_SET_STATE(p2d, type, _FILTER_GET_SLOT_STATE(filter));
               _FILTER_SWAP_SLOT_DATA(p2d, type, filter, 0);
               rv = AAX_TRUE;
               break;
            }
            case AAX_DISTANCE_FILTER:
            {
               _oalRingBuffer3dProps *p3d = mixer->dprops3d->props3d;
               if (_FILTER_GET_SLOT(filter, 0, AAX_ROLLOFF_FACTOR) < 0)
               {
                  float rf;
                  rf = -1.0f*_FILTER_GET_SLOT(filter, 0, AAX_ROLLOFF_FACTOR);
                  _FILTER_SET_SLOT(filter, 0, AAX_ROLLOFF_FACTOR, rf);
                  mixer->dist_delaying = AAX_TRUE;
               }
               _FILTER_SET(p3d, type, 0, _FILTER_GET_SLOT(filter, 0, 0));
               _FILTER_SET(p3d, type, 1, _FILTER_GET_SLOT(filter, 0, 1));
               _FILTER_SET(p3d, type, 2, _FILTER_GET_SLOT(filter, 0, 2));
               _FILTER_SET(p3d, type, 3, _FILTER_GET_SLOT(filter, 0, 3));
               _FILTER_SET_STATE(p3d, type, _FILTER_GET_SLOT_STATE(filter));
               _FILTER_SWAP_SLOT_DATA(p3d, type, filter, 0);
               rv = AAX_TRUE;
               break;
            }
            default:
               _aaxErrorSet(AAX_INVALID_ENUM);
            }
            _intBufReleaseData(dptr, _AAX_SENSOR);
         }
      }
      else {
         _aaxErrorSet(AAX_INVALID_PARAMETER);
      }
   }
   else {
      _aaxErrorSet(AAX_INVALID_HANDLE);
   }
   return rv;
}

AAX_API const aaxFilter AAX_APIENTRY
aaxSceneryGetFilter(aaxConfig config, enum aaxFilterType type)
{
   _handle_t* handle = get_handle(config);
   aaxFilter rv = AAX_FALSE;
   if (handle)
   {
      const _intBufferData* dptr;
      switch (type)
      {
      case AAX_FREQUENCY_FILTER:
      case AAX_DISTANCE_FILTER:
         dptr = _intBufGet(handle->sensors, _AAX_SENSOR, 0);
         if (dptr)
         {
            _sensor_t* sensor = _intBufGetDataPtr(dptr);
            _aaxAudioFrame* mixer = sensor->mixer;
            rv = new_filter_handle(handle->info, type, mixer->props2d,
                                                       mixer->dprops3d->props3d);
            _intBufReleaseData(dptr, _AAX_SENSOR);
         }
         break;
      default:
         _aaxErrorSet(AAX_INVALID_PARAMETER);
      }
   }
   else {
      _aaxErrorSet(AAX_INVALID_HANDLE);
   }
   return rv;
}

AAX_API int AAX_APIENTRY
aaxScenerySetEffect(aaxConfig config, aaxEffect e)
{
   _handle_t* handle = get_handle(config);
   int rv = AAX_FALSE;
   if (handle)
   {
      _filter_t* effect = get_effect(e);
      if (effect)
      {
         const _intBufferData* dptr;
         dptr = _intBufGet(handle->sensors, _AAX_SENSOR, 0);
         if (dptr)
         {
            _sensor_t* sensor = _intBufGetDataPtr(dptr);
            _aaxAudioFrame* mixer = sensor->mixer;
            int type = effect->pos;
            switch (effect->type)
            {
            case AAX_VELOCITY_EFFECT:
            {
               _oalRingBuffer3dProps *p3d = mixer->dprops3d->props3d;
               _EFFECT_SET(p3d, type, 0, _EFFECT_GET_SLOT(effect, 0, 0));
               _EFFECT_SET(p3d, type, 1, _EFFECT_GET_SLOT(effect, 0, 1));
               _EFFECT_SET(p3d, type, 2, _EFFECT_GET_SLOT(effect, 0, 2));
               _EFFECT_SET(p3d, type, 3, _EFFECT_GET_SLOT(effect, 0, 3));
               _EFFECT_SET_STATE(p3d, type, _EFFECT_GET_SLOT_STATE(effect));
               _EFFECT_SWAP_SLOT_DATA(p3d, type, effect, 0);
               rv = AAX_TRUE;
               break;
            }
            default:
               _aaxErrorSet(AAX_INVALID_ENUM);
            }
            _intBufReleaseData(dptr, _AAX_SENSOR);
         }
      }
      else {
         _aaxErrorSet(AAX_INVALID_PARAMETER);
      }
   }
   else {
      _aaxErrorSet(AAX_INVALID_HANDLE);
   }
   return rv;
}

AAX_API const aaxEffect AAX_APIENTRY
aaxSceneryGetEffect(aaxConfig config, enum aaxEffectType type)
{
   _handle_t* handle = get_handle(config);
   aaxEffect rv = AAX_FALSE;
   if (handle)
   {
      const _intBufferData* dptr;
      switch(type)
      {
      case AAX_VELOCITY_EFFECT:
         dptr = _intBufGet(handle->sensors, _AAX_SENSOR, 0);
         if (dptr)
         {
            _sensor_t* sensor = _intBufGetDataPtr(dptr);
            _aaxAudioFrame* mixer = sensor->mixer;
            rv = new_effect_handle(handle->info, type, mixer->props2d,
                                                      mixer->dprops3d->props3d);
            _intBufReleaseData(dptr, _AAX_SENSOR);
         }
         break;
      default:
         _aaxErrorSet(AAX_INVALID_PARAMETER);
      }
   }
   else {
      _aaxErrorSet(AAX_INVALID_HANDLE);
   }
   return rv;
}

