/*
	vid_common_sw32.c

	general video driver functions

	Copyright (C) 1996-1997 Id Software, Inc.

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

#include "QF/cvar.h"
#include "QF/qargs.h"
#include "QF/sys.h"
#include "QF/va.h"
#include "QF/vid.h"

#include "compat.h"



unsigned short d_8to16table[256];
unsigned int d_8to24table[256];
unsigned char d_15to8table[65536];


/*
	lhfindcolor

	LordHavoc: finds nearest matching color in a palette
*/
int
lhfindcolor (byte *pal, int colors, int r, int g, int b)
{
	int i, dist, best, bestdist, rd, gd, bd;
	best = 0;
	bestdist = 1000000000;
	for (i = 0;i < colors;i++)
	{
		rd = pal[i*3+0] - r;
		gd = pal[i*3+1] - g;
		bd = pal[i*3+2] - b;
		dist = rd*rd+gd*gd+bd*bd;
		if (dist < bestdist)
		{
			if (!dist) // exact match
				return i;
			best = i;
			bestdist = dist;
		}
	}
	return best;
}

/*
	VID_MakeColormap32

	LordHavoc: makes a 32bit color*light table, RGBA order, no endian,
	           may need to be re-ordered to hardware at display time
*/
void
VID_MakeColormap32 (void *outcolormap, byte *pal)
{
	int c, l;
	byte *out;
	out = (byte *)&d_8to24table;
	for (c = 0; c < 256; c++) {
		*out++ = pal[c*3+0];
		*out++ = pal[c*3+1];
		*out++ = pal[c*3+2];
		*out++ = 255;
	}
	d_8to24table[255] = 0;				// 255 is transparent
	out = (byte *) outcolormap;
	for (l = 0;l < VID_GRADES;l++)
	{
		for (c = 0;c < vid.fullbright;c++)
		{
			out[(l*256+c)*4+0] = bound(0, (pal[c*3+0] * l) >> (VID_CBITS - 1),
									   255);
			out[(l*256+c)*4+1] = bound(0, (pal[c*3+1] * l) >> (VID_CBITS - 1),
									   255);
			out[(l*256+c)*4+2] = bound(0, (pal[c*3+2] * l) >> (VID_CBITS - 1),
									   255);
			out[(l*256+c)*4+3] = 255;
		}
		for (;c < 255;c++)
		{
			out[(l*256+c)*4+0] = pal[c*3+0];
			out[(l*256+c)*4+1] = pal[c*3+1];
			out[(l*256+c)*4+2] = pal[c*3+2];
			out[(l*256+c)*4+3] = 255;
		}
		out[(l*256+255)*4+0] = 0;
		out[(l*256+255)*4+1] = 0;
		out[(l*256+255)*4+2] = 0;
		out[(l*256+255)*4+3] = 0;
	}
}

unsigned short lh24to16bit (int red, int green, int blue)
{
	red = bound(0, red, 255);
	green = bound(0, green, 255);
	blue = bound(0, blue, 255);
	red >>= 3;
	green >>= 2;
	blue >>= 3;
	red <<= 11;
	green <<= 5;
	return (unsigned short) (red | green | blue);
}

/*
	VID_MakeColormap16

	LordHavoc: makes a 16bit color*light table, RGB order, native endian,
	           may need to be translated to hardware order at display time
*/
void
VID_MakeColormap16 (void *outcolormap, byte *pal)
{
	int c, l;
	unsigned short *out;
	out = (unsigned short *)&d_8to16table;
	for (c = 0; c < 256; c++)
		*out++ = lh24to16bit(pal[c*3+0], pal[c*3+1], pal[c*3+2]);
	d_8to16table[255] = 0;				// 255 is transparent
	out = (unsigned short *) outcolormap;
	for (l = 0;l < VID_GRADES;l++)
	{
		for (c = 0;c < vid.fullbright;c++)
			out[l*256+c] = lh24to16bit(
			(pal[c*3+0] * l) >> (VID_CBITS - 1), 
			(pal[c*3+1] * l) >> (VID_CBITS - 1), 
			(pal[c*3+2] * l) >> (VID_CBITS - 1));
		for (;c < 255;c++)
			out[l*256+c] = lh24to16bit(pal[c*3+0], pal[c*3+1], pal[c*3+2]);
		out[l*256+255] = 0;
	}
}

/*
	VID_MakeColormap8

	LordHavoc: makes a 8bit color*light table
*/
void
VID_MakeColormap8 (void *outcolormap, byte *pal)
{
	int c, l;
	byte *out;
	out = (byte *) outcolormap;
	for (l = 0;l < VID_GRADES;l++)
	{
		for (c = 0;c < vid.fullbright;c++)
			out[l*256+c] = lhfindcolor(pal, 256,
									   (pal[c*3+0] * l) >> (VID_CBITS - 1),
									   (pal[c*3+1] * l) >> (VID_CBITS - 1),
									   (pal[c*3+2] * l) >> (VID_CBITS - 1));
		for (;c < 256;c++)
			out[l*256+c] = c;
	}
}

/*
	VID_MakeColormaps

	LordHavoc: makes 8bit, 16bit, and 32bit colormaps and palettes
*/
void
VID_MakeColormaps (int fullbrights, byte *pal)
{
	vid.fullbright = fullbrights;
	vid.colormap8 = malloc (256*VID_GRADES * sizeof (byte));
	vid.colormap16 = malloc (256*VID_GRADES * sizeof (short));
	vid.colormap32 = malloc (256*VID_GRADES * sizeof (int));
	SYS_CHECKMEM (vid.colormap8 && vid.colormap16 && vid.colormap32);
	VID_MakeColormap8(vid.colormap8, pal);
	VID_MakeColormap16(vid.colormap16, pal);
	VID_MakeColormap32(vid.colormap32, pal);
}

void
VID_InitBuffers (void)
{
	int         buffersize, zbuffersize, cachesize = 1;

	// Calculate the sizes we want first
	buffersize = vid.rowbytes * vid.height;
	zbuffersize = vid.width * vid.height * sizeof (*vid.zbuffer);
	if (vid.surf_cache_size)
		cachesize = vid.surf_cache_size (vid.width, vid.height);

	// Free the old z-buffer
	if (vid.zbuffer) {
		free (vid.zbuffer);
		vid.zbuffer = NULL;
	}
	// Free the old surface cache
	if (vid.surfcache) {
		if (vid.flush_caches)
			vid.flush_caches ();
		free (vid.surfcache);
		vid.surfcache = NULL;
	}
	if (vid.do_screen_buffer) {
		vid.do_screen_buffer ();
	} else {
		// Free the old screen buffer
		if (vid.buffer) {
			free (vid.buffer);
			vid.conbuffer = vid.buffer = NULL;
		}
		// Allocate the new screen buffer
		vid.conbuffer = vid.buffer = calloc (buffersize, 1);
		if (!vid.conbuffer) {
			Sys_Error ("Not enough memory for video mode");
		}
	}
	// Allocate the new z-buffer
	vid.zbuffer = calloc (zbuffersize, 1);
	if (!vid.zbuffer) {
		free (vid.buffer);
		vid.conbuffer = vid.buffer = NULL;
		Sys_Error ("Not enough memory for video mode");
	}
	// Allocate the new surface cache; free the z-buffer if we fail
	vid.surfcache = calloc (cachesize, 1);
	if (!vid.surfcache) {
		free (vid.buffer);
		free (vid.zbuffer);
		vid.zbuffer = NULL;
		Sys_Error ("Not enough memory for video mode");
	}

	if (vid.init_caches)
		vid.init_caches (vid.surfcache, cachesize);
}

void
VID_ShiftPalette (unsigned char *p)
{
	VID_SetPalette (p);
}
