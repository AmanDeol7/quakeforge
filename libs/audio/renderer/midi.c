/*
	midi.c

	midi file loading for use with libWildMidi

	Copyright (C) 2003  Chris Ison

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

*/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

static __attribute__ ((unused)) const char rcsid[] = 
	"$Id$";

#ifdef HAVE_WILDMIDI

#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif
#include <stdlib.h>
#include <wildmidi_lib.h>

#include "QF/cvar.h"
#include "QF/sound.h"
#include "QF/sys.h"
#include "QF/quakefs.h"

#include "snd_render.h"

static int midi_intiialized = 0;
	
static cvar_t  *wildmidi_volume;
static cvar_t  *wildmidi_config;

static int
midi_init ( void ) {
	wildmidi_volume = Cvar_Get ("wildmidi_volume", "100", CVAR_ARCHIVE, NULL, "Set the Master Volume");
	wildmidi_config = Cvar_Get ("wildmidi_config", "/etc/timidity.cfg", CVAR_ROM, NULL, "path/filename of timidity.cfg");
	
	if (WildMidi_Init(wildmidi_config->string, shm->speed, 0) == -1)
		return 1;
	midi_intiialized = 1;
	return 0;
}

static wavinfo_t
get_info (void * handle) {
	wavinfo_t   info;
	struct _WM_Info *wm_info;
	
	if ((wm_info = WildMidi_GetInfo(handle)) == NULL) {
		Sys_Printf ("Could not obtain midi information\n");
		return info;
	}
	
	info.rate = shm->speed;
	info.width = 2;
	info.channels = 2;
	info.loopstart = -1;
	info.samples = wm_info->approx_total_samples;
	info.dataofs = 0;
	info.datalen = info.samples * 4;
	return info;
}
	
static int
midi_stream_read (void *file, byte *buf, int count, wavinfo_t *info)
{
	// FIXME: need to check what the return of this function /should/ be
	return WildMidi_GetOutput (file, (char *)buf, (unsigned long int)count);
}

static int
midi_stream_seek (void *file, int pos, wavinfo_t *info)
{
	unsigned long int new_pos;
	pos *= info->width * info->channels;
	pos += info->dataofs;
	new_pos = pos;
	
	// FIXME: need to check what the return of this function /should/ be
	return WildMidi_SampledSeek(file, &new_pos);
}

static void
midi_stream_close (sfx_t *sfx)
{
	sfxstream_t *stream = (sfxstream_t *)sfx->data;

	WildMidi_Close (stream->file);
	free (stream);
	free (sfx);
}

/*
 * Note: we only set the QF stream up here.
 * The WildMidi stream was setup when SND_OpenMidi was called
 * so stream->file contains the WildMidi handle for the midi
 */

static sfx_t *
midi_stream_open (sfx_t *_sfx)
{
	sfx_t      *sfx;
	sfxstream_t *stream = (sfxstream_t *) _sfx->data;
	wavinfo_t  *info = &stream->wavinfo;
	int         samples;
	int         size;
	QFile	   *file;
	midi	   *handle;
	unsigned char *local_buffer;
	unsigned long int local_buffer_size;

	QFS_FOpenFile (stream->file, &file);
	
	local_buffer_size = Qfilesize(file);
	
	local_buffer = malloc(local_buffer_size);
	Qread(file, local_buffer, local_buffer_size);
	Qclose(file);

	handle = WildMidi_OpenBuffer(local_buffer, local_buffer_size);
	
	if (handle == NULL) 
		return NULL;	
	
	sfx = calloc (1, sizeof (sfx_t));
	samples = shm->speed * 0.3;
	size = samples = (samples + 255) & ~255;
	
	// WildMidi audio data is 16bit stereo
	size *= 4;
	
	stream = calloc (1, sizeof (sfxstream_t) + size);
	memcpy (stream->buffer.data + size, "\xde\xad\xbe\xef", 4);

	sfx->name = _sfx->name;
	sfx->data = stream;
	sfx->wavinfo = SND_CacheWavinfo;
	sfx->touch = sfx->retain = SND_StreamRetain;
	sfx->release = SND_StreamRelease;
	sfx->close = midi_stream_close;

	stream->sfx = sfx;
	stream->file = handle;
	stream->resample = SND_NoResampleStereo;

	stream->read = midi_stream_read;
	stream->seek = midi_stream_seek;
	stream->wavinfo = *info;

	stream->buffer.length = samples;
	stream->buffer.advance = SND_StreamAdvance;
	stream->buffer.setpos = SND_StreamSetPos;
	stream->buffer.sfx = sfx;

	stream->seek (stream->file, 0, &stream->wavinfo);
	stream->buffer.advance (&stream->buffer, 0);

	stream->resample (&stream->buffer, 0, 0, 0);
	
	return sfx;
}

void
SND_LoadMidi (QFile *file, sfx_t *sfx, char *realname)
{
	
	wavinfo_t   info;
	sfxstream_t *stream = calloc (1, sizeof (sfxstream_t));
	midi * handle;
	unsigned char *local_buffer;
	unsigned long int local_buffer_size = Qfilesize(file);

	if (!midi_intiialized) {
		if (midi_init()) {
			return;
		}
	}
		
	
	local_buffer = malloc(local_buffer_size);
	Qread(file, local_buffer, local_buffer_size);
	Qclose(file);

	// WildMidi takes ownership, so be damned if you touch it
	handle = WildMidi_OpenBuffer(local_buffer, local_buffer_size);
	

	if (handle == NULL) 
		return;
	
	info = get_info (handle);
	
	WildMidi_Close (handle);
		
	Sys_DPrintf ("stream %s\n", realname);
	
	// we init stream here cause we will only ever stream
	
	sfx->open = midi_stream_open;
	sfx->wavinfo = SND_CacheWavinfo;
	sfx->touch = sfx->retain = SND_StreamRetain;
	sfx->release = SND_StreamRelease;
	sfx->data = stream;
	
	stream->file = realname;
	stream->wavinfo = info;
}
#endif // HAVE_WILDMIDI
