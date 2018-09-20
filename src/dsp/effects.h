/*
 * Copyright 2005-2018 by Erik Hofman.
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

#ifndef _AAX_EFFECTS_H
#define _AAX_EFFECTS_H 1

#if defined(__cplusplus)
extern "C" {
#endif

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <base/geometry.h>

#include "objects.h"
#include "common.h"
#include "api.h"

#define DELAY_EFFECTS_TIME      	0.070f
#define FRAME_REVERB_EFFECTS_TIME	0.210f
#define REVERB_EFFECTS_TIME     	0.700f

aaxEffect _aaxEffectCreateHandle(_aaxMixerInfo*, enum aaxEffectType, unsigned);

void _aaxSetDefaultEffect2d(_aaxEffectInfo*, unsigned int, unsigned slot);
void _aaxSetDefaultEffect3d(_aaxEffectInfo*, unsigned int, unsigned slot);

typedef struct {
  enum aaxEffectType type;
  int pos;
} _eff_cvt_tbl_t;

typedef struct {
   fx4_t min;
   fx4_t max;
} _eff_minmax_tbl_t;

typedef struct
{
   unsigned int id;
   int pos;
   int state;
   enum aaxEffectType type;
   _aaxEffectInfo* slot[_MAX_FE_SLOTS];
   _aaxMixerInfo* info;
   _handle_t *handle;
} _effect_t;

// _effect_t* new_effect(_aaxMixerInfo*, enum aaxEffectType);
_effect_t* new_effect_handle(const aaxConfig, enum aaxEffectType, _aax2dProps*, _aax3dProps*);
_effect_t* get_effect(aaxEffect);

extern const _eff_cvt_tbl_t _eff_cvt_tbl[AAX_EFFECT_MAX];
extern const _eff_minmax_tbl_t _eff_minmax_tbl[_MAX_FE_SLOTS][AAX_EFFECT_MAX];


typedef aaxEffect _aaxEffectCreate(_aaxMixerInfo*, enum aaxEffectType);
typedef int _aaxEffectDestroy(_effect_t*);
typedef aaxEffect _aaxEffectSetState(_effect_t*, int);
typedef aaxEffect _aaxEffectSetData(_effect_t*, aaxBuffer);
typedef _effect_t* _aaxNewEffectHandle(const aaxConfig, enum aaxEffectType, _aax2dProps*, _aax3dProps*);
typedef float _aaxEffectConvert(float, int, unsigned char);

typedef struct
{
   char lite;
   const char *name;
   float version;
   _aaxEffectCreate *create;
   _aaxEffectDestroy *destroy;
   _aaxEffectSetState *state;
   _aaxEffectSetData *data;
   _aaxNewEffectHandle *handle;

   _aaxEffectConvert *set;
   _aaxEffectConvert *get;
   _aaxEffectConvert *limit;

} _eff_function_tbl;

extern _eff_function_tbl _aaxPitchEffect;
extern _eff_function_tbl _aaxDynamicPitchEffect;
extern _eff_function_tbl _aaxTimedPitchEffect;
extern _eff_function_tbl _aaxDistortionEffect;
extern _eff_function_tbl _aaxPhasingEffect;
extern _eff_function_tbl _aaxChorusEffect;
extern _eff_function_tbl _aaxFlangingEffect;
extern _eff_function_tbl _aaxVelocityEffect;
extern _eff_function_tbl _aaxReverbEffect;
extern _eff_function_tbl _aaxConvolutionEffect;
extern _eff_function_tbl _aaxModulatorEffect;
extern _eff_function_tbl *_aaxEffects[AAX_EFFECT_MAX];

/* effects */
#define _EFFECT_GET_SLOT(E, s, p)       E->slot[s]->param[p]
#define _EFFECT_GET_SLOT_STATE(E)       E->slot[0]->state
#define _EFFECT_GET_SLOT_UPDATED(E)     E->slot[0]->updated
#define _EFFECT_GET_SLOT_DATA(E, s)     E->slot[s]->data
#define _EFFECT_SET_SLOT(E, s, p, v)    E->slot[s]->param[p] = v
#define _EFFECT_SET_SLOT_DATA(E, s, v)  E->slot[s]->data = v
#define _EFFECT_SET_SLOT_UPDATED(E)     if (!E->slot[0]->updated) E->slot[0]->updated = 1

#define _EFFECT_GET(P, e, p)            P->effect[e].param[p]
#define _EFFECT_GET_STATE(P, e)         P->effect[e].state
#define _EFFECT_GET_UPDATED(P, e)	P->effect[e].updated
#define _EFFECT_LOCK_DATA(P, e)		_aaxMutexLock(P->effect[e].mutex)
#define _EFFECT_UNLOCK_DATA(P, e)	_aaxMutexUnLock(P->effect[e].mutex)
#define _EFFECT_FREE_LOCK(P, e)		_aaxMutexDestroy(P->effect[e].mutex)
#define _EFFECT_GET_DATA(P, e)          P->effect[e].data
#define _EFFECT_FREE_DATA(P, e)         if (P->effect[e].destroy) P->effect[e].destroy(P->effect[e].data)
#define _EFFECT_SET(P, e, p, v)         P->effect[e].param[p] = v
#define _EFFECT_SET_STATE(P, e, v)      P->effect[e].state = v
#define _EFFECT_SET_UPDATED(P, e, v)    P->effect[e].updated = v
#define _EFFECT_SET_DATA(P, e, v)       P->effect[e].data = v
#define _EFFECT_COPY(P1, P2, e, p)      \
                                P1->effect[e].param[p] = P2->effect[e].param[p]
#define _EFFECT_COPY_DATA(P1, P2, e)    P1->effect[e].data = P2->effect[e].data

#define _EFFECT_GET2D(G, e, p)          _EFFECT_GET(G->props2d, e, p)
#define _EFFECT_LOCK2D_DATA(G, e)	_EFFECT_LOCK_DATA(G->props2d, e)
#define _EFFECT_UNLOCK2D_DATA(G, e)	_EFFECT_UNLOCK_DATA(G->props2d, e)
#define _EFFECT_FREE2D_LOCK(G, e)	_EFFECT_FREE_LOCK(G->props2d, e)
#define _EFFECT_GET2D_DATA(G, e)        _EFFECT_GET_DATA(G->props2d, e)
#define _EFFECT_FREE2D_DATA(G, e)	_EFFECT_FREE_DATA(G->props2d, e)
#define _EFFECT_GET3D(G, e, p)          _EFFECT_GET(G->props3d, e, p)
#define _EFFECT_GET3D_DATA(G, e)        _EFFECT_GET_DATA(G->props3d, e)
#define _EFFECT_FREE3D_DATA(G, e)	_EFFECT_FREE_DATA(G->props3d, e)
#define _EFFECT_SET2D(G, e, p, v)       _EFFECT_SET(G->props2d, e, p, v)
#define _EFFECT_SET2D_DATA(G, e, v)     _EFFECT_SET_DATA(G->props2d, e, v)
#define _EFFECT_SET3D(G, e, p, v)       _EFFECT_SET(G->props3d, e, p, v)
#define _EFFECT_SET3D_DATA(G, e, v)     _EFFECT_SET_DATA(G->props3d, e, v)
#define _EFFECT_COPY2D(G1, G2, e, p)    _EFFECT_COPY(G1->props2d, G2->props2d, e, p)
#define _EFFECT_COPY3D(G1, G2, e, p)    _EFFECT_COPY(G1->props3d, G2->props3d, e, p)
#define _EFFECT_COPY2D_DATA(G1, G2, e)  _EFFECT_COPY_DATA(G1->props2d, G2->props2d, e)
#define _EFFECT_COPY3D_DATA(G1, G2, e)  _EFFECT_COPY_DATA(G1->props3d, G2->props3d, e)

#define _EFFECT_GETD3D(G, e, p)         _EFFECT_GET(G->props3d, e, p)
#define _EFFECT_SETD3D_DATA(G, e, v)    _EFFECT_SET_DATA(G->props3d, e, v)
#define _EFFECT_COPYD3D(G1, G2, e, p)   _EFFECT_COPY(G1->props3d, G2->props3d, e, p)
#define _EFFECT_COPYD3D_DATA(G1, G2, e) _EFFECT_COPY_DATA(G1->props3d, G2->props3d, e)

#define _EFFECT_SWAP_SLOT_DATA(P, e, E, s)                              \
    do {                                                                \
    E->slot[s]->data = _aaxAtomicPointerSwap(&P->filter[e].data, E->slot[s]->data); \
    P->effect[e].destroy = E->slot[s]->destroy;                         \
    if (!s) aaxEffectSetState(E, P->effect[e].state); } while (0);

#define _EFFECT_SWAP_SLOT(P, t, f, s)                                    \
    _EFFECT_SET(P, t, 0, _EFFECT_GET_SLOT(f, s, 0));                     \
    _EFFECT_SET(P, t, 1, _EFFECT_GET_SLOT(f, s, 1));                     \
    _EFFECT_SET(P, t, 2, _EFFECT_GET_SLOT(f, s, 2));                     \
    _EFFECT_SET(P, t, 3, _EFFECT_GET_SLOT(f, s, 3));                     \
    _EFFECT_SET_STATE(P, t, _EFFECT_GET_SLOT_STATE(f));                  \
    _EFFECT_SWAP_SLOT_DATA(P, t, f, s);

float _velocity_calculcate_vs(_aaxEnvData*);
FLOAT _velocity_prepare(_aax3dProps*, _aaxDelayed3dProps*, _aaxDelayed3dProps*, _aaxDelayed3dProps*, vec3f_ptr, float, float, float);

#endif /* _AAX_EFFECTS_H */

