/*
	QF/plugin/snd_render.h

	Sound Renderer plugin data types

	Copyright (C) 2001 Jeff Teunissen <deek@quakeforge.net>

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

	See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to:

		Free Software Foundation, Inc.
		59 Temple Place - Suite 330
		Boston, MA  02111-1307, USA

	$Id$
*/
#ifndef __QF_plugin_snd_render_h_
#define __QF_plugin_snd_render_h_

#include <QF/qtypes.h>
#include <QF/plugin.h>

/*
	All sound plugins must export these functions
*/

struct sfx_s;

typedef void (QFPLUGIN *P_S_Init) (void);
typedef void (QFPLUGIN *P_S_Shutdown) (void);
typedef void (QFPLUGIN *P_S_AmbientOff) (void);
typedef void (QFPLUGIN *P_S_AmbientOn) (void);
typedef void (QFPLUGIN *P_S_TouchSound) (const char *sample);
typedef void (QFPLUGIN *P_S_ClearBuffer) (void);
typedef void (QFPLUGIN *P_S_StartSound) (int entnum, int entchannel, struct sfx_s *sfx, const vec3_t origin, float fvol, float attenuation);
typedef void (QFPLUGIN *P_S_StaticSound) (struct sfx_s *sfx, const vec3_t origin, float vol, float attenuation);
typedef void (QFPLUGIN *P_S_StopSound) (int entnum, int entchannel);
typedef struct sfx_s * (QFPLUGIN *P_S_PrecacheSound) (const char *sample);
typedef void (QFPLUGIN *P_S_ClearPrecache) (void);
typedef void (QFPLUGIN *P_S_Update) (const vec3_t origin, const vec3_t v_forward, const vec3_t v_right, const vec3_t v_up);
typedef void (QFPLUGIN *P_S_StopAllSounds) (qboolean clear);
typedef void (QFPLUGIN *P_S_BeginPrecaching) (void);
typedef void (QFPLUGIN *P_S_EndPrecaching) (void);
typedef void (QFPLUGIN *P_S_ExtraUpdate) (void);
typedef void (QFPLUGIN *P_S_LocalSound) (const char *s);
typedef void (QFPLUGIN *P_S_BlockSound) (void);
typedef void (QFPLUGIN *P_S_UnblockSound) (void);

typedef struct snd_render_funcs_s {
	P_S_AmbientOff 		pS_AmbientOff;
	P_S_AmbientOn  		pS_AmbientOn;
	P_S_TouchSound 		pS_TouchSound;
	P_S_ClearBuffer		pS_ClearBuffer;
	P_S_StaticSound		pS_StaticSound;
	P_S_StartSound		pS_StartSound;
	P_S_StopSound		pS_StopSound;
	P_S_PrecacheSound	pS_PrecacheSound;
	P_S_ClearPrecache	pS_ClearPrecache;
	P_S_Update			pS_Update;
	P_S_StopAllSounds	pS_StopAllSounds;
	P_S_BeginPrecaching pS_BeginPrecaching;
	P_S_EndPrecaching	pS_EndPrecaching;
	P_S_ExtraUpdate 	pS_ExtraUpdate;
	P_S_LocalSound		pS_LocalSound;
	P_S_BlockSound		pS_BlockSound;
	P_S_UnblockSound	pS_UnblockSound;
} snd_render_funcs_t;

typedef struct snd_render_data_s {
	struct model_s **worldmodel;
	double *host_frametime;
	int *viewentity;

	int *soundtime;
	int *paintedtime;
} snd_render_data_t;

#endif // __QF_plugin_snd_render_h_
