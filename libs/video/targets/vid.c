/*
	vid.c

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
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

static __attribute__ ((unused)) const char rcsid[] = 
	"$Id$";

#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif
#include <math.h>

#include "QF/console.h"
#include "QF/cvar.h"
#include "QF/qargs.h"
#include "QF/sys.h"
#include "QF/va.h"
#include "QF/vid.h"

#include "compat.h"
#include "view.h"

/* Software and hardware gamma support */
byte		gammatable[256];
cvar_t	   *vid_gamma;
cvar_t	   *vid_system_gamma;
cvar_t     *vid_conwidth;
qboolean	vid_gamma_avail;		// hardware gamma availability

unsigned int	d_8to24table[256];

/* Screen size */
int 		scr_width, scr_height;
cvar_t	   *vid_width;
cvar_t	   *vid_height;

cvar_t     *vid_fullscreen;


void
VID_GetWindowSize (int def_w, int def_h)
{
	int pnum;

	vid_width = Cvar_Get ("vid_width", va ("%d", def_w), CVAR_NONE, NULL,
			"screen width");
	vid_height = Cvar_Get ("vid_height", va ("%d", def_h), CVAR_NONE, NULL,
			"screen height");

	if ((pnum = COM_CheckParm ("-width"))) {
		if (pnum >= com_argc - 1)
			Sys_Error ("VID: -width <width>");

		Cvar_Set (vid_width, com_argv[pnum + 1]);

		if (!vid_width->int_val)
			Sys_Error ("VID: Bad window width");
	}

	if ((pnum = COM_CheckParm ("-height"))) {
		if (pnum >= com_argc - 1)
			Sys_Error ("VID: -height <height>");

		Cvar_Set (vid_height, com_argv[pnum + 1]);

		if (!vid_height->int_val)
			Sys_Error ("VID: Bad window height");
	}

	if ((pnum = COM_CheckParm ("-winsize"))) {
		if (pnum >= com_argc - 2)
			Sys_Error ("VID: -winsize <width> <height>");

		Cvar_Set (vid_width, com_argv[pnum + 1]);
		Cvar_Set (vid_height, com_argv[pnum + 2]);

		if (!vid_width->int_val || !vid_height->int_val)
			Sys_Error ("VID: Bad window width/height");
	}

	Cvar_SetFlags (vid_width, vid_width->flags | CVAR_ROM);
	Cvar_SetFlags (vid_height, vid_height->flags | CVAR_ROM);

	scr_width = vid.width = vid_width->int_val;
	scr_height = vid.height = vid_height->int_val;

	vid_conwidth = Cvar_Get ("vid_conwidth", va ("%d", def_w), CVAR_NONE, NULL,
			"console effective width (GL only)");
	if ((pnum = COM_CheckParm ("-conwidth"))) {
		if (pnum >= com_argc - 1)
			Sys_Error ("VID: -conwidth <width>");
		Cvar_Set (vid_conwidth, com_argv[pnum + 1]);
		if (!vid_height->int_val)
			Sys_Error ("VID: Bad console width");
	}
	Cvar_SetFlags (vid_conwidth, vid_conwidth->flags | CVAR_ROM);
	vid.conwidth = vid_conwidth->int_val;
}

/* GAMMA FUNCTIONS */

static void
VID_BuildGammaTable (double gamma)
{
	int 	i;

	if (gamma == 1.0) { // linear, don't bother with the math
		for (i = 0; i < 256; i++) {
			gammatable[i] = i;
		}
	} else {
		double	g = 1.0 / gamma;
		int 	v;

		for (i = 0; i < 256; i++) { // Build/update gamma lookup table
			v = (int) ((255.0 * pow ((double) i / 255.0, g)) + 0.5);
			gammatable[i] = bound (0, v, 255);
		}
	}
}

/*
	VID_UpdateGamma

	This is a callback to update the palette or system gamma whenever the
	vid_gamma Cvar is changed.
*/
void
VID_UpdateGamma (cvar_t *vid_gamma)
{
	double gamma = bound (0.1, vid_gamma->value, 9.9);
	
	vid.recalc_refdef = 1;				// force a surface cache flush

	if (vid_gamma_avail && vid_system_gamma->int_val) {	// Have system, use it
		Con_DPrintf ("Setting hardware gamma to %g\n", gamma);
		VID_BuildGammaTable (1.0);	// hardware gamma wants a linear palette
		VID_SetGamma (gamma);
		memcpy (vid.palette, vid.basepal, 256 * 3);
	} else {	// We have to hack the palette
		int i;
		Con_DPrintf ("Setting software gamma to %g\n", gamma);
		VID_BuildGammaTable (gamma);
		for (i = 0; i < 256 * 3; i++)
			vid.palette[i] = gammatable[vid.basepal[i]];
		VID_SetPalette (vid.palette); // update with the new palette
	}
}

/*
	VID_InitGamma

	Initialize the vid_gamma Cvar, and set up the palette
*/
void
VID_InitGamma (unsigned char *pal)
{
	int 	i;
	double	gamma = 1.45;

	vid.basepal = pal;
	vid.palette = malloc (256 * 3);
	if ((i = COM_CheckParm ("-gamma"))) {
		gamma = atof (com_argv[i + 1]);
	}
	gamma = bound (0.1, gamma, 9.9);

	vid_gamma = Cvar_Get ("vid_gamma", va ("%f", gamma), CVAR_ARCHIVE,
						  VID_UpdateGamma, "Gamma correction");

	VID_BuildGammaTable (vid_gamma->value);
}

void
VID_HandlePause (qboolean paused)
{
}
