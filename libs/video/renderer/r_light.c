/*
	r_light.c

	common lightmap code.

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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif

#include <math.h>
#include <stdio.h>

#include "QF/cvar.h"
#include "QF/render.h"

#include "compat.h"
#include "r_cvar.h"
#include "r_local.h"
#include "r_shared.h"

dlight_t    *r_dlights;
lightstyle_t r_lightstyle[MAX_LIGHTSTYLES];

unsigned int r_maxdlights;


void
R_MaxDlightsCheck (cvar_t *var)
{
	r_maxdlights = max(var->int_val, 0);

	if (r_dlights)
		free (r_dlights);

	r_dlights=0;

	if (r_maxdlights)
		r_dlights = (dlight_t *) calloc (r_maxdlights, sizeof (dlight_t));

	R_ClearDlights();
}

void
R_AnimateLight (void)
{
	int         i, j, k;

	// light animations
	// 'm' is normal light, 'a' is no light, 'z' is double bright
	i = (int) (r_realtime * 10);
	for (j = 0; j < MAX_LIGHTSTYLES; j++) {
		if (!r_lightstyle[j].length) {
			d_lightstylevalue[j] = 256;
			continue;
		}
		k = i % r_lightstyle[j].length;
		k = r_lightstyle[j].map[k] - 'a';
		k = k * 22;
		d_lightstylevalue[j] = k;
	}
}

// LordHavoc: heavily modified, to eliminate unnecessary texture uploads,
//            and support bmodel lighting better
void
R_RecursiveMarkLights (const vec3_t lightorigin, dlight_t *light, int bit,
					   mnode_t *node)
{
	int         i;
	float       ndist, maxdist;
	mplane_t   *splitplane;
	msurface_t *surf;

	maxdist = light->radius * light->radius;

loc0:
	if (node->contents < 0)
		return;

	splitplane = node->plane;
	ndist = DotProduct (lightorigin, splitplane->normal) - splitplane->dist;

	if (ndist > light->radius) {
		// Save time by not pushing another stack frame.
		if (node->children[0]->contents >= 0) {
			node = node->children[0];
			goto loc0;
		}
		return;
	}
	if (ndist < -light->radius) {
		// Save time by not pushing another stack frame.
		if (node->children[1]->contents >= 0) {
			node = node->children[1];
			goto loc0;
		}
		return;
	}

	// mark the polygons
	surf = r_worldentity.model->surfaces + node->firstsurface;
	for (i = 0; i < node->numsurfaces; i++, surf++) {
		int s, t;
		float l, dist, dist2;
		vec3_t	impact;

		dist = ndist;

		dist2 = dist * dist;
		if (dist2 >= maxdist)
			continue;

		impact[0] = light->origin[0] - surf->plane->normal[0] * dist;
		impact[1] = light->origin[1] - surf->plane->normal[1] * dist;
		impact[2] = light->origin[2] - surf->plane->normal[2] * dist;

		l = DotProduct (impact, surf->texinfo->vecs[0]) +
			surf->texinfo->vecs[0][3] - surf->texturemins[0];
		s = l + 0.5;
		if (s < 0)
			s = 0;
		else if (s > surf->extents[0])
			s = surf->extents[0];
		s = l - s;
		l = DotProduct (impact, surf->texinfo->vecs[1]) +
			surf->texinfo->vecs[1][3] - surf->texturemins[1];
		t = l + 0.5;
		if (t < 0)
			t = 0;
		else if (t > surf->extents[1])
			t = surf->extents[1];
		t = l - t;

		if ((s * s + t * t + dist * dist) < maxdist) {
			if (surf->dlightframe != r_framecount) {
				surf->dlightframe = r_framecount;
				surf->dlightbits = bit;
			} else {
				surf->dlightbits |= bit;
			}
		}
	}

	if (node->children[0]->contents >= 0) {
		if (node->children[1]->contents >= 0)
			R_RecursiveMarkLights (lightorigin, light, bit, node->children[1]);
		node = node->children[0];
		goto loc0;
	} else if (node->children[1]->contents >= 0) {
		node = node->children[1];
		goto loc0;
	}
}

static inline void
mark_surfaces (msurface_t *surf, const vec3_t lightorigin, dlight_t *light,
			   int bit)
{
	float      dist;
	float      dist2, d;
	float      maxdist = light->radius * light->radius;
	vec3_t     impact;

	dist = PlaneDiff(lightorigin, surf->plane);
	if (surf->flags & SURF_PLANEBACK)
		dist = -dist;
	if ((dist < -0.25f && !(surf->flags & SURF_LIGHTBOTHSIDES))
		|| dist > light->radius)
		return;

	dist2 = dist * dist;
	dist = -dist;
	VectorMA (light->origin, dist, surf->plane->normal, impact);

	d = DotProduct (impact, surf->texinfo->vecs[0])
		+ surf->texinfo->vecs[0][3] - surf->texturemins[0];
	if (d < 0) {
		dist2 += d * d;
		if (dist2 >= maxdist)
			return;
	} else {
		d -= surf->extents[0] + 16;
		if (d > 0) {
			dist2 += d * d;
			if (dist2 >= maxdist)
				return;
		}
	}
	d = DotProduct (impact, surf->texinfo->vecs[1])
		+ surf->texinfo->vecs[1][3] - surf->texturemins[1];
	if (d < 0) {
		dist2 += d * d;
		if (dist2 >= maxdist)
			return;
	} else {
		d -= surf->extents[1] + 16;
		if (d > 0) {
			dist2 += d * d;
			if (dist2 >= maxdist)
				return;
		}
	}

	if (surf->dlightframe != r_framecount) {
		surf->dlightbits = 0;
		surf->dlightframe = r_framecount;
	}
	surf->dlightbits |= bit;
}

void
R_MarkLights (const vec3_t lightorigin, dlight_t *light, int bit,
			  model_t *model)
{
	mleaf_t    *pvsleaf = Mod_PointInLeaf (lightorigin, model);

	if (!pvsleaf->compressed_vis) {
		mnode_t *node = model->nodes + model->hulls[0].firstclipnode;
		R_RecursiveMarkLights (lightorigin, light, bit, node);
	} else {
		float       radius = light->radius;
		vec3_t      mins, maxs;
		int         leafnum = 0;
		byte       *in = pvsleaf->compressed_vis;
		byte        vis_bits;

		mins[0] = lightorigin[0] - radius;
		mins[1] = lightorigin[1] - radius;
		mins[2] = lightorigin[2] - radius;
		maxs[0] = lightorigin[0] + radius;
		maxs[1] = lightorigin[1] + radius;
		maxs[2] = lightorigin[2] + radius;
		while (leafnum < model->numleafs) {
			int         i;
			if (!(vis_bits = *in++)) {
				leafnum += (*in++) * 8;
				continue;
			}
			for (i = 0; i < 8 && leafnum < model->numleafs; i++, leafnum++) {
				int      m;
				mleaf_t *leaf  = &model->leafs[leafnum + 1];
				if (!(vis_bits & (1 << i)))
					continue;
				if (leaf->visframe != r_visframecount)
					continue;
				if (leaf->mins[0] > maxs[0] || leaf->maxs[0] < mins[0]
					|| leaf->mins[1] > maxs[1] || leaf->maxs[1] < mins[1]
					|| leaf->mins[2] > maxs[2] || leaf->maxs[2] < mins[2])
					continue;
				if (leaf->dlightframe != r_framecount) {
					leaf->dlightbits = 0;
					leaf->dlightframe = r_framecount;
				}
				leaf->dlightbits |= bit;
				for (m = 0; m < leaf->nummarksurfaces; m++) {
					msurface_t *surf = leaf->firstmarksurface[m];
					if (surf->visframe != r_visframecount)
						continue;
					mark_surfaces (surf, lightorigin, light, bit);
				}
			}
		}
	}
}

void
R_PushDlights (const vec3_t entorigin)
{
	int         i;
	dlight_t   *l;
	vec3_t      lightorigin;

	if (!r_dlight_lightmap->int_val)
		return;

	l = r_dlights;

	for (i = 0; i < r_maxdlights; i++, l++) {
		if (l->die < r_realtime || !l->radius)
			continue;
		VectorSubtract (l->origin, entorigin, lightorigin);
		R_MarkLights (lightorigin, l, 1 << i, r_worldentity.model);
	}
}

/* LIGHT SAMPLING */

mplane_t   *lightplane;
vec3_t      lightspot;

int
RecursiveLightPoint (mnode_t *node, const vec3_t start, const vec3_t end)
{
	int			 i, r, s, t, ds, dt, maps, side;
	unsigned int scale;
	byte		*lightmap;
	float		 front, back, frac;
	mplane_t	*plane;
	msurface_t	*surf;
	mtexinfo_t	*tex;
	vec3_t		 mid;
loop:
	if (node->contents < 0)
		return -1;						// didn't hit anything

	// calculate mid point
	plane = node->plane;
	front = DotProduct (start, plane->normal) - plane->dist;
	back = DotProduct (end, plane->normal) - plane->dist;
	side = front < 0;

	if ((back < 0) == side) {
		node = node->children[side];
		goto loop;
	}

	frac = front / (front - back);
	mid[0] = start[0] + (end[0] - start[0]) * frac;
	mid[1] = start[1] + (end[1] - start[1]) * frac;
	mid[2] = start[2] + (end[2] - start[2]) * frac;

	// go down front side   
	r = RecursiveLightPoint (node->children[side], start, mid);
	if (r >= 0)
		return r;						// hit something

	if ((back < 0) == side)
		return -1;						// didn't hit anything

	// check for impact on this node
	VectorCopy (mid, lightspot);
	lightplane = plane;

	surf = r_worldentity.model->surfaces + node->firstsurface;
	for (i = 0; i < node->numsurfaces; i++, surf++) {
		if (surf->flags & SURF_DRAWTILED)
			continue;					// no lightmaps

		tex = surf->texinfo;

		s = DotProduct (mid, tex->vecs[0]) + tex->vecs[0][3];
		t = DotProduct (mid, tex->vecs[1]) + tex->vecs[1][3];;

		if (s < surf->texturemins[0] || t < surf->texturemins[1])
			continue;

		ds = s - surf->texturemins[0];
		dt = t - surf->texturemins[1];

		if (ds > surf->extents[0] || dt > surf->extents[1])
			continue;

		if (!surf->samples)
			return 0;

		ds >>= 4;
		dt >>= 4;

		lightmap = surf->samples;
		r = 0;
		if (lightmap) {
			lightmap += dt * ((surf->extents[0] >> 4) + 1) + ds;

			for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255;
				 maps++) {
				scale = d_lightstylevalue[surf->styles[maps]];
				r += *lightmap * scale;
				lightmap += ((surf->extents[0] >> 4) + 1) *
					((surf->extents[1] >> 4) + 1);
			}

			r >>= 8;
		}

		return r;
	}

	// go down back side
	return RecursiveLightPoint (node->children[!side], mid, end);
}

int
R_LightPoint (const vec3_t p)
{
	vec3_t      end;
	int         r;

	if (!r_worldentity.model->lightdata)
		return 255;

	end[0] = p[0];
	end[1] = p[1];
	end[2] = p[2] - 2048;

	r = RecursiveLightPoint (r_worldentity.model->nodes, p, end);

	if (r == -1)
		r = 0;

	return r;
}

dlight_t *
R_AllocDlight (int key)
{
	int         i;
	dlight_t   *dl;
	static dlight_t dummy;

	if (!r_maxdlights) {
		memset (&dummy, 0, sizeof (dummy));
		return &dummy;
	}

	// first look for an exact key match
	if (key) {
		dl = r_dlights;
		for (i = 0; i < r_maxdlights; i++, dl++) {
			if (dl->key == key) {
				memset (dl, 0, sizeof (*dl));
				dl->key = key;
				dl->color[0] = dl->color[1] = dl->color[2] = 1;
				return dl;
			}
		}
	}
	// then look for anything else
	dl = r_dlights;
	for (i = 0; i < r_maxdlights; i++, dl++) {
		if (dl->die < r_realtime) {
			memset (dl, 0, sizeof (*dl));
			dl->key = key;
			dl->color[0] = dl->color[1] = dl->color[2] = 1;
			return dl;
		}
	}

	dl = &r_dlights[0];
	memset (dl, 0, sizeof (*dl));
	dl->key = key;
	return dl;
}

void
R_DecayLights (double frametime)
{
	int         i;
	dlight_t   *dl;

	dl = r_dlights;
	for (i = 0; i < r_maxdlights; i++, dl++) {
		if (dl->die < r_realtime || !dl->radius)
			continue;

		dl->radius -= frametime * dl->decay;
		if (dl->radius < 0)
			dl->radius = 0;
	}
}

void
R_ClearDlights (void)
{
	if (r_maxdlights)
		memset (r_dlights, 0, r_maxdlights * sizeof (dlight_t));
}
