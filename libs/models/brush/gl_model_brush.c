/*
	gl_model_brush.c

	gl support routines for model loading and caching

	Copyright (C) 1996-1997  Id Software, Inc.

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

// models are the only shared resource between a client and server running
// on the same machine.

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif

#include "QF/cvar.h"
#include "QF/model.h"
#include "QF/qendian.h"
#include "QF/quakefs.h"
#include "QF/sys.h"
#include "QF/texture.h"
#include "QF/tga.h"
#include "QF/va.h"
#include "QF/vid.h"
#include "QF/GL/qf_textures.h"

#include "compat.h"

int   mod_lightmap_bytes = 3;


void
Mod_ProcessTexture (miptex_t *mt, texture_t *tx)
{
	char		name[32];

	snprintf (name, sizeof (name), "fb_%s", mt->name);
	tx->gl_fb_texturenum =
		Mod_Fullbright ((byte *) (tx + 1), tx->width, tx->height, name);
	tx->gl_texturenum =
		GL_LoadTexture (mt->name, tx->width, tx->height, (byte *) (tx + 1),
						true, false, 1);
}

void
Mod_LoadExternalTextures (model_t *mod)
{
	char	   *filename;
	int			i;
	tex_t	   *targa;
	texture_t  *tx;
	QFile	   *f;

	for (i = 0; i < mod->numtextures; i++) {
		tx = mod->textures[i];
		if (!tx)
			continue;

		// replace special flag characters with underscores
		if (tx->name[0] == '*') { // FIXME: translate to # or _?
		 	filename = va ("maps/#%s.tga", tx->name + 1);
		} else {
		 	filename = va ("maps/%s.tga", tx->name);
		}
		COM_FOpenFile (filename, &f);

		if (!f) {
			if (tx->name[0] == '*') {
				filename = va ("textures/#%s.tga", tx->name + 1);
			} else {
				filename = va ("textures/%s.tga", tx->name);
			}
			COM_FOpenFile (filename, &f);
		}

		if (f) {
			targa = LoadTGA (f);
			Qclose (f);
			tx->gl_texturenum =
				GL_LoadTexture (tx->name, targa->width, targa->height,
								targa->data, true, false, 3);
		}
	}
}

void
Mod_LoadLighting (lump_t *l)
{
	byte        d;
	byte       *in, *out, *data;
	char        litfilename[1024];
	int         i;

	loadmodel->lightdata = NULL;
	if (mod_lightmap_bytes > 1) {
		// LordHavoc: check for a .lit file to load
		strcpy (litfilename, loadmodel->name);
		COM_StripExtension (litfilename, litfilename);
		strncat (litfilename, ".lit", sizeof (litfilename) -
				 strlen (litfilename));
		data = (byte *) COM_LoadHunkFile (litfilename);
		if (data) {
			if (data[0] == 'Q' && data[1] == 'L' && data[2] == 'I'
				&& data[3] == 'T') {
				i = LittleLong (((int *) data)[1]);
				if (i == 1) {
					Sys_DPrintf ("%s loaded", litfilename);
					loadmodel->lightdata = data + 8;
					return;
				} else
					Sys_Printf ("Unknown .lit file version (%d)\n", i);
			} else
				Sys_Printf ("Corrupt .lit file (old version?), ignoring\n");
		}
	}
	// LordHavoc: oh well, expand the white lighting data
	if (!l->filelen)
		return;
	loadmodel->lightdata = Hunk_AllocName (l->filelen * mod_lightmap_bytes,
										   litfilename);
	in = mod_base + l->fileofs;
	out = loadmodel->lightdata;

	if (mod_lightmap_bytes > 1)
		for (i = 0; i < l->filelen ; i++) {
			d = gammatable[*in++];
			*out++ = d;
			*out++ = d;
			*out++ = d;
		}
	else
		for (i = 0; i < l->filelen ; i++)
			*out++ = gammatable[*in++];
}

msurface_t *warpface;


static void
BoundPoly (int numverts, float *verts, vec3_t mins, vec3_t maxs)
{
	float      *v;
	int         i, j;

	mins[0] = mins[1] = mins[2] = 9999;
	maxs[0] = maxs[1] = maxs[2] = -9999;
	v = verts;
	for (i = 0; i < numverts; i++)
		for (j = 0; j < 3; j++, v++) {
			if (*v < mins[j])
				mins[j] = *v;
			if (*v > maxs[j])
				maxs[j] = *v;
		}
}

static void
SubdividePolygon (int numverts, float *verts)
{
	float       frac, m, s, t;
	float       dist[64];
	float      *v;
	int         b, f, i, j, k;
	glpoly_t   *poly;
	vec3_t      mins, maxs;
	vec3_t      front[64], back[64];

	if (numverts > 60)
		Sys_Error ("numverts = %i", numverts);

	BoundPoly (numverts, verts, mins, maxs);

	for (i = 0; i < 3; i++) {
		m = (mins[i] + maxs[i]) * 0.5;
		m = gl_subdivide_size->value * floor (m / gl_subdivide_size->value +
											  0.5);
		if (maxs[i] - m < 8)
			continue;
		if (m - mins[i] < 8)
			continue;

		// cut it
		v = verts + i;
		for (j = 0; j < numverts; j++, v += 3)
			dist[j] = *v - m;

		// wrap cases
		dist[j] = dist[0];
		v -= i;
		VectorCopy (verts, v);

		f = b = 0;
		v = verts;
		for (j = 0; j < numverts; j++, v += 3) {
			if (dist[j] >= 0) {
				VectorCopy (v, front[f]);
				f++;
			}
			if (dist[j] <= 0) {
				VectorCopy (v, back[b]);
				b++;
			}
			if (dist[j] == 0 || dist[j + 1] == 0)
				continue;
			if ((dist[j] > 0) != (dist[j + 1] > 0)) {
				// clip point
				frac = dist[j] / (dist[j] - dist[j + 1]);
				for (k = 0; k < 3; k++)
					front[f][k] = back[b][k] = v[k] + frac * (v[3 + k] - v[k]);
				f++;
				b++;
			}
		}

		SubdividePolygon (f, front[0]);
		SubdividePolygon (b, back[0]);
		return;
	}

	poly = Hunk_Alloc (sizeof (glpoly_t) + (numverts - 4) * VERTEXSIZE *
					   sizeof (float));
	poly->next = warpface->polys;
	warpface->polys = poly;
	poly->numverts = numverts;
	for (i = 0; i < numverts; i++, verts += 3) {
		VectorCopy (verts, poly->verts[i]);
		s = DotProduct (verts, warpface->texinfo->vecs[0]);
		t = DotProduct (verts, warpface->texinfo->vecs[1]);
		poly->verts[i][3] = s;
		poly->verts[i][4] = t;
	}
}

/*
	Mod_SubdivideSurface

	Breaks a polygon up along axial 64 unit
	boundaries so that turbulent and sky warps
	can be done reasonably.
*/
void
Mod_SubdivideSurface (msurface_t *fa)
{
	float      *vec;
	int         lindex, numverts, i;
	vec3_t      verts[64];

	warpface = fa;

	// convert edges back to a normal polygon
	numverts = 0;
	for (i = 0; i < fa->numedges; i++) {
		lindex = loadmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
			vec = loadmodel->vertexes[loadmodel->edges[lindex].v[0]].position;
		else
			vec = loadmodel->vertexes[loadmodel->edges[-lindex].v[1]].position;
		VectorCopy (vec, verts[numverts]);
		numverts++;
	}

	SubdividePolygon (numverts, verts[0]);
}
