/*
	snd_sun.c

	(description)

	Copyright (C) 1996-1997  Id Software, Inc.
	Copyright (C) 1999,2000  contributors of the QuakeForge project
	Please see the file "AUTHORS" for a list of contributors

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
static const char rcsid[] = 
	"$Id$";

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/audioio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "QF/plugin.h"
#include "QF/qargs.h"
#include "QF/qtypes.h"
#include "QF/sound.h"
#include "QF/sys.h"

static int		audio_fd;
static int		snd_inited;
static int		snd_blocked = 0;

static int  wbufp;
static audio_info_t info;

#define BUFFER_SIZE		8192

static unsigned char dma_buffer[BUFFER_SIZE];
static volatile dma_t sn;

static plugin_t           plugin_info;
static plugin_data_t      plugin_info_data;
static plugin_funcs_t     plugin_info_funcs;
static general_data_t     plugin_info_general_data;
static general_funcs_t    plugin_info_general_funcs;
static snd_output_data_t  plugin_info_sound_data;
static snd_output_funcs_t plugin_info_sound_funcs;


static qboolean
SNDDMA_Init (void)
{
	if (snd_inited) {
		Sys_Printf ("Sound already init'd\n");
		return 0;
	}

	shm = &sn;
	shm->splitbuffer = 0;

	audio_fd = open ("/dev/audio", O_WRONLY | O_NDELAY);

	if (audio_fd < 0) {
		if (errno == EBUSY) {
			Sys_Printf ("Audio device is being used by another process\n");
		}
		perror ("/dev/audio");
		Sys_Printf ("Could not open /dev/audio\n");
		return (0);
	}

	if (ioctl (audio_fd, AUDIO_GETINFO, &info) < 0) {
		perror ("/dev/audio");
		Sys_Printf ("Could not communicate with audio device.\n");
		close (audio_fd);
		return 0;
	}
	// set to nonblock
	if (fcntl (audio_fd, F_SETFL, O_NONBLOCK) < 0) {
		perror ("/dev/audio");
		close (audio_fd);
		return 0;
	}

	AUDIO_INITINFO (&info);

	shm->speed = 11025;

	// try 16 bit stereo
	info.play.encoding = AUDIO_ENCODING_LINEAR;
	info.play.sample_rate = 11025;
	info.play.channels = 2;
	info.play.precision = 16;

	if (ioctl (audio_fd, AUDIO_SETINFO, &info) < 0) {
		info.play.encoding = AUDIO_ENCODING_LINEAR;
		info.play.sample_rate = 11025;
		info.play.channels = 1;
		info.play.precision = 16;
		if (ioctl (audio_fd, AUDIO_SETINFO, &info) < 0) {
			Sys_Printf ("Incapable sound hardware.\n");
			close (audio_fd);
			return 0;
		}
		Sys_Printf ("16 bit mono sound initialized\n");
		shm->samplebits = 16;
		shm->channels = 1;
	} else {							// 16 bit stereo
		Sys_Printf ("16 bit stereo sound initialized\n");
		shm->samplebits = 16;
		shm->channels = 2;
	}

	shm->soundalive = true;
	shm->samples = sizeof (dma_buffer) / (shm->samplebits / 8);
	shm->samplepos = 0;
	shm->submission_chunk = 1;
	shm->buffer = (unsigned char *) dma_buffer;

	snd_inited = 1;

	return 1;
}

static int
SNDDMA_GetDMAPos (void)
{
	if (!snd_inited)
		return (0);

	if (ioctl (audio_fd, AUDIO_GETINFO, &info) < 0) {
		perror ("/dev/audio");
		Sys_Printf ("Could not communicate with audio device.\n");
		close (audio_fd);
		snd_inited = 0;
		return (0);
	}

	return ((info.play.samples * shm->channels) % shm->samples);
}

static int
SNDDMA_GetSamples (void)
{
	if (!snd_inited)
		return (0);

	if (ioctl (audio_fd, AUDIO_GETINFO, &info) < 0) {
		perror ("/dev/audio");
		Sys_Printf ("Could not communicate with audio device.\n");
		close (audio_fd);
		snd_inited = 0;
		return (0);
	}

	return info.play.samples;
}

static void
SNDDMA_Shutdown (void)
{
	if (snd_inited) {
		close (audio_fd);
		snd_inited = 0;
	}
}

/*
	SNDDMA_Submit

	Send sound to device if buffer isn't really the dma buffer
*/
static void
SNDDMA_Submit (void)
{
	unsigned char *p;
	static unsigned char writebuf[1024];
	int			bsize, bytes, idx, b;
	int			stop = *plugin_info_sound_data.paintedtime;

	if (snd_blocked)
		return;

	if (*plugin_info_sound_data.paintedtime < wbufp)
		wbufp = 0;						// reset

	bsize = shm->channels * (shm->samplebits / 8);
	bytes = (*plugin_info_sound_data.paintedtime - wbufp) * bsize;

	if (!bytes)
		return;

	if (bytes > sizeof (writebuf)) {
		bytes = sizeof (writebuf);
		stop = wbufp + bytes / bsize;
	}

	p = writebuf;
	idx = (wbufp * bsize) & (BUFFER_SIZE - 1);

	for (b = bytes; b; b--) {
		*p++ = dma_buffer[idx];
		idx = (idx + 1) & (BUFFER_SIZE - 1);
	}

	wbufp = stop;

	if (write (audio_fd, writebuf, bytes) < bytes)
		Sys_Printf ("audio can't keep up!\n");

}

static void
SNDDMA_BlockSound (void)
{
	++snd_blocked;
}

static void
SNDDMA_UnblockSound (void)
{
	if (!snd_blocked)
		return;
	--snd_blocked;
}

QFPLUGIN plugin_t *
snd_output_sun_PluginInfo (void) {
	plugin_info.type = qfp_sound;
	plugin_info.api_version = QFPLUGIN_VERSION;
	plugin_info.plugin_version = "0.1";
	plugin_info.description = "SUN digital output";
	plugin_info.copyright = "Copyright (C) 1996-1997 id Software, Inc.\n"
		"Copyright (C) 1999,2000,2001  contributors of the QuakeForge "
		"project\n"
		"Please see the file \"AUTHORS\" for a list of contributors";
	plugin_info.functions = &plugin_info_funcs;
	plugin_info.data = &plugin_info_data;

	plugin_info_data.general = &plugin_info_general_data;
	plugin_info_data.input = NULL;
	plugin_info_data.sound = &plugin_info_sound_data;

	plugin_info_funcs.general = &plugin_info_general_funcs;
	plugin_info_funcs.input = NULL;
	plugin_info_funcs.sound = &plugin_info_sound_funcs;

	plugin_info_general_funcs.p_Init = NULL; //SNDDMA_Init_Cvars;
	plugin_info_general_funcs.p_Shutdown = NULL;

	plugin_info_sound_funcs.pS_O_Init = SNDDMA_Init;
	plugin_info_sound_funcs.pS_O_Shutdown = SNDDMA_Shutdown;
	plugin_info_sound_funcs.pS_O_GetDMAPos = SNDDMA_GetDMAPos;
	plugin_info_sound_funcs.pS_O_Submit = SNDDMA_Submit;
	plugin_info_sound_funcs.pS_O_BlockSound = SNDDMA_BlockSound;
	plugin_info_sound_funcs.pS_O_UnblockSound = SNDDMA_UnblockSound;

	return &plugin_info;
}
