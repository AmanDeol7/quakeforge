/*
	Copyright (C) 1996-1997  Id Software, Inc.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

	See file, 'COPYING', for details.
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
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#include "QF/dstring.h"
#include "QF/qendian.h"
#include "QF/sys.h"
#include "QF/va.h"

#include "bsp5.h"
#include "options.h"

int         headclipnode;
int         firstface;


/*
	FindFinalPlane

	Used to find plane index numbers for clip nodes read from child processes
*/
int
FindFinalPlane (dplane_t *p)
{
	dplane_t   *dplane;
	int         i;

	for (i = 0, dplane = bsp->planes; i < bsp->numplanes; i++, dplane++) {
		if (p->type != dplane->type)
			continue;
		if (p->dist != dplane->dist)
			continue;
		if (p->normal[0] != dplane->normal[0])
			continue;
		if (p->normal[1] != dplane->normal[1])
			continue;
		if (p->normal[2] != dplane->normal[2])
			continue;
		return i;
	}

	// new plane
	if (bsp->numplanes == MAX_MAP_PLANES)
		Sys_Error ("numplanes == MAX_MAP_PLANES");
	BSP_AddPlane (bsp, p);

	return bsp->numplanes - 1;
}

int         planemapping[MAX_MAP_PLANES];

static void
WriteNodePlanes_r (node_t *node)
{
	dplane_t    dplane;
	plane_t    *plane;

	if (node->planenum == -1)
		return;
	if (planemapping[node->planenum] == -1) {	// a new plane
		planemapping[node->planenum] = bsp->numplanes;

		plane = &planes[node->planenum];

		dplane.normal[0] = plane->normal[0];
		dplane.normal[1] = plane->normal[1];
		dplane.normal[2] = plane->normal[2];
		dplane.dist = plane->dist;
		dplane.type = plane->type;
		BSP_AddPlane (bsp, &dplane);
	}

	node->outputplanenum = planemapping[node->planenum];

	WriteNodePlanes_r (node->children[0]);
	WriteNodePlanes_r (node->children[1]);
}

void
WriteNodePlanes (node_t *nodes)
{
	memset (planemapping, -1, sizeof (planemapping));
	WriteNodePlanes_r (nodes);
}

static int
WriteClipNodes_r (node_t *node)
{
	dclipnode_t cn;
	int         num, c, i;

	// FIXME: free more stuff?  
	if (node->planenum == -1) {
		num = node->contents;
		free (node);
		return num;
	}
	// emit a clipnode
	c = bsp->numclipnodes;
	BSP_AddClipnode (bsp, &cn);
	cn.planenum = node->outputplanenum;
	for (i = 0; i < 2; i++)
		cn.children[i] = WriteClipNodes_r (node->children[i]);
	bsp->clipnodes[c] = cn;

	free (node);
	return c;
}

/*
	WriteClipNodes

	Called after the clipping hull is completed.  Generates a disk format
	representation and frees the original memory.
*/
void
WriteClipNodes (node_t *nodes)
{
	headclipnode = bsp->numclipnodes;
	WriteClipNodes_r (nodes);
}

static void
WriteLeaf (node_t *node)
{
	dleaf_t     leaf_p;
	face_t    **fp, *f;

	// emit a leaf
	leaf_p.contents = node->contents;

	// write bounding box info
	VectorCopy (node->mins, leaf_p.mins);
	VectorCopy (node->maxs, leaf_p.maxs);

	leaf_p.visofs = -1;					// no vis info yet

	// write the marksurfaces
	leaf_p.firstmarksurface = bsp->nummarksurfaces;

	for (fp = node->markfaces; *fp; fp++) {
		// emit a marksurface
		if (bsp->nummarksurfaces == MAX_MAP_MARKSURFACES)
			Sys_Error ("nummarksurfaces == MAX_MAP_MARKSURFACES");
		f = *fp;
		if (f->texturenum < 0)
			continue;
		do {
			BSP_AddMarkSurface (bsp, f->outputnumber);
			f = f->original;			// grab tjunction split faces
		} while (f);
	}

	leaf_p.nummarksurfaces = bsp->nummarksurfaces - leaf_p.firstmarksurface;
	BSP_AddLeaf (bsp, &leaf_p);
}

static void
WriteDrawNodes_r (node_t *node)
{
	static dnode_t dummy;
	dnode_t    *n;
	int         i;
	int         nodenum = bsp->numnodes;

	// emit a node  
	if (bsp->numnodes == MAX_MAP_NODES)
		Sys_Error ("numnodes == MAX_MAP_NODES");
	BSP_AddNode (bsp, &dummy);
	n = &bsp->nodes[nodenum];

	VectorCopy (node->mins, n->mins);
	VectorCopy (node->maxs, n->maxs);

	n->planenum = node->outputplanenum;
	n->firstface = node->firstface;
	n->numfaces = node->numfaces;

	// recursively output the other nodes
	for (i = 0; i < 2; i++) {
		n = &bsp->nodes[nodenum];
		if (node->children[i]->planenum == -1) {
			if (node->children[i]->contents == CONTENTS_SOLID)
				n->children[i] = -1;
			else {
				n->children[i] = -(bsp->numleafs + 1);
				WriteLeaf (node->children[i]);
			}
		} else {
			n->children[i] = bsp->numnodes;
			WriteDrawNodes_r (node->children[i]);
		}
	}
}

void
WriteDrawNodes (node_t *headnode)
{
	dmodel_t    bm;
	int         start, i;

#if 0
	if (headnode->contents < 0)
		Sys_Error ("FinishBSPModel: empty model");
#endif

	// emit a model
	if (bsp->nummodels == MAX_MAP_MODELS)
		Sys_Error ("nummodels == MAX_MAP_MODELS");

	bm.headnode[0] = bsp->numnodes;
	bm.firstface = firstface;
	bm.numfaces = bsp->numfaces - firstface;
	firstface = bsp->numfaces;

	start = bsp->numleafs;

	if (headnode->contents < 0)
		WriteLeaf (headnode);
	else
		WriteDrawNodes_r (headnode);
	bm.visleafs = bsp->numleafs - start;

	for (i = 0; i < 3; i++) {
		bm.mins[i] = headnode->mins[i] + SIDESPACE + 1;   // remove the padding
		bm.maxs[i] = headnode->maxs[i] - SIDESPACE - 1;
	}
	BSP_AddModel (bsp, &bm);
	// FIXME: are all the children decendant of padded nodes?
}

/*
	BumpModel

	Used by the clipping hull processes that only need to store headclipnode
*/
void
BumpModel (int hullnum)
{
	static dmodel_t bm;

	// emit a model
	bm.headnode[hullnum] = headclipnode;
	BSP_AddModel (bsp, &bm);
}

typedef struct {
	char        identification[4];		// should be WAD2
	int         numlumps;
	int         infotableofs;
} wadinfo_t;

typedef struct {
	int         filepos;
	int         disksize;
	int         size;					// uncompressed
	char        type;
	char        compression;
	char        pad1, pad2;
	char        name[16];				// must be null terminated
} lumpinfo_t;

typedef struct wadlist_s {
	struct wadlist_s *next;
	wadinfo_t   wadinfo;
	lumpinfo_t *lumpinfo;
} wadlist_t;

QFile      *texfile;
wadlist_t  *wadlist;

static void
CleanupName (char *in, char *out)
{
	int         i;

	for (i = 0; i < 16; i++) {
		if (!in[i])
			break;

		out[i] = toupper (in[i]);
	}

	for (; i < 16; i++)
		out[i] = 0;
}

static int
TEX_InitFromWad (char *path)
{
	int         i;
	wadlist_t  *wl;

	texfile = Qopen (path, "rbz");
#ifdef HAVE_ZLIB
	if (!texfile)
		texfile = Qopen (va ("%s.gz", path), "rbz");
#endif
	if (!texfile)
		return -1;
	printf ("wadfile: %s\n", path);

	wl = calloc (1, sizeof (wadlist_t));

	Qread (texfile, &wl->wadinfo, sizeof (wadinfo_t));
	if (strncmp (wl->wadinfo.identification, "WAD2", 4))
		Sys_Error ("TEX_InitFromWad: %s isn't a wadfile", path);
	wl->wadinfo.numlumps = LittleLong (wl->wadinfo.numlumps);
	wl->wadinfo.infotableofs = LittleLong (wl->wadinfo.infotableofs);
	Qseek (texfile, wl->wadinfo.infotableofs, SEEK_SET);
	wl->lumpinfo = malloc (wl->wadinfo.numlumps * sizeof (lumpinfo_t));
	Qread (texfile, wl->lumpinfo, wl->wadinfo.numlumps * sizeof (lumpinfo_t));

	for (i = 0; i < wl->wadinfo.numlumps; i++) {
		CleanupName (wl->lumpinfo[i].name, wl->lumpinfo[i].name);
		wl->lumpinfo[i].filepos = LittleLong (wl->lumpinfo[i].filepos);
		wl->lumpinfo[i].disksize = LittleLong (wl->lumpinfo[i].disksize);
	}
	wl->next = wadlist;
	wadlist = wl;
	return 0;
}

static int
LoadLump (char *name, dstring_t *dest)
{
	char        cname[16];		//FIXME: overflow
	int         i;
	int         ofs = dest->size;
	wadlist_t  *wl;

	CleanupName (name, cname);

	for (wl = wadlist; wl; wl = wl->next) {
		for (i = 0; i < wl->wadinfo.numlumps; i++) {
			if (!strcmp (cname, wl->lumpinfo[i].name)) {
				dest->size += wl->lumpinfo[i].disksize;
				dstring_adjust (dest);
				Qseek (texfile, wl->lumpinfo[i].filepos, SEEK_SET);
				Qread (texfile, dest->str + ofs, wl->lumpinfo[i].disksize);
				return wl->lumpinfo[i].disksize;
			}
		}
	}

	printf ("WARNING: texture %s not found\n", name);
	return 0;
}

static void
AddAnimatingTextures (void)
{
	int         base, i, j, k;
	char        name[32];		//FIXME: overflow
	wadlist_t  *wl;

	base = nummiptex;

	for (i = 0; i < base; i++) {
		if (miptex[i][0] != '+')
			continue;
		strcpy (name, miptex[i]);

		for (j = 0; j < 20; j++) {
			if (j < 10)
				name[1] = '0' + j;
			else
				name[1] = 'A' + j - 10;	// alternate animation

			// see if this name exists in the wadfile
			for (wl = wadlist; wl; wl = wl->next) {
				for (k = 0; k < wl->wadinfo.numlumps; k++) {
					if (!strcmp (name, wl->lumpinfo[k].name)) {
						FindMiptex (name);	// add to the miptex list
						break;
					}
				}
			}
		}
	}

	printf ("added %i texture frames\n", nummiptex - base);
}

static void
WriteMiptex (void)
{
	dstring_t  *data;
	char       *wad_list, *wad, *w;
	char       *path_list, *path, *p;
	dstring_t  *fullpath;
	dmiptexlump_t *l;
	int         i, len, res = -1;

	(const char *)wad_list = ValueForKey (&entities[0], "_wad");
	if (!wad_list || !wad_list[0]) {
		(const char *)wad_list = ValueForKey (&entities[0], "wad");
		if (!wad_list || !wad_list[0]) {
			printf ("WARNING: no wadfile specified\n");
			bsp->texdatasize = 0;
			return;
		}
	}
	fullpath = dstring_new ();
	wad = wad_list = strdup (wad_list);
	w = strchr (wad, ';');
	if (w)
		*w++ = 0;
	while (1) {
		path = path_list = strdup (options.wadpath);
		p = strchr (path, ';');
		if (p)
			*p++ = 0;
		while (1) {
			dsprintf (fullpath, "%s%s%s", path, path[0] ? "/" : "", wad);
			res = TEX_InitFromWad (fullpath->str);
			if (!res)
				break;
			path = p;
			if (!path || !*path)
				break;
			p = strchr (path, ';');
			if (p)
				*p++ = 0;
		}
		free (path_list);
		if (res == -1)
			Sys_Error ("couldn't open %s[.gz]", wad);

		AddAnimatingTextures ();

		wad = w;
		if (!wad || !*wad)
			break;
		w = strchr (wad, ';');
		if (w)
			*w++ = 0;
	}
	free (wad_list);
	dstring_delete (fullpath);

	data = dstring_new ();
	data->size = sizeof (dmiptexlump_t);
	dstring_adjust (data);

	l = (dmiptexlump_t *) data->str;
	l->nummiptex = nummiptex;
	data->size = (char *) &l->dataofs[nummiptex] - data->str;
	dstring_adjust (data);

	for (i = 0; i < nummiptex; i++) {
		l = (dmiptexlump_t *) data->str;
		l->dataofs[i] = data->size;
		len = LoadLump (miptex[i], data);
		l = (dmiptexlump_t *) data->str;
		if (!len)
			l->dataofs[i] = -1;			// didn't find the texture
	}
	BSP_AddTextures (bsp, data->str, data->size);
}

void
BeginBSPFile (void)
{
	static dedge_t edge;
	static dleaf_t leaf;
	// edge 0 is not used, because 0 can't be negated
	BSP_AddEdge (bsp, &edge);

	// leaf 0 is common solid with no faces
	leaf.contents = CONTENTS_SOLID;
	BSP_AddLeaf (bsp, &leaf);

	firstface = 0;
}

void
FinishBSPFile (void)
{
	QFile      *f;

	printf ("--- FinishBSPFile ---\n");

	WriteMiptex ();

// XXX	PrintBSPFileSizes ();
	printf ("WriteBSPFile: %s\n", options.bspfile);
	f = Qopen (options.bspfile, "wb");
	if (!f)
		Sys_Error ("couldn't open %s. %s", options.bspfile, strerror(errno));
	WriteBSPFile (bsp, f);
	Qclose (f);
}
