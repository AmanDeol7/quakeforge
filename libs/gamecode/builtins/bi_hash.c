/*
	bi_hash.c

	QuakeC hash table api

	Copyright (C) 2003 Bill Currie

	Author: Bill Currie <bill@taniwha.org>
	Date: 2003/4/7

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
#include <stdlib.h>

#include "QF/csqc.h"
#include "QF/hash.h"
#include "QF/progs.h"

typedef struct bi_hashtab_s {
	struct bi_hashtab_s *next;
	struct bi_hashtab_s **prev;
	progs_t    *pr;
	hashtab_t  *tab;
	func_t      gk;
	func_t      gh;
	func_t      cmp;
	func_t      f;
	pointer_t   ud;
} bi_hashtab_t;

typedef struct {
	bi_hashtab_t *tabs;
} hash_resources_t;

static const char *
bi_get_key (void *key, void *_ht)
{
	bi_hashtab_t *ht = (bi_hashtab_t *)_ht;
	P_INT (ht->pr, 0) = (long) (key);
	P_INT (ht->pr, 1) = ht->ud;
	PR_ExecuteProgram (ht->pr, ht->gk);
	return PR_GetString (ht->pr, R_STRING (ht->pr));
}

static unsigned long
bi_get_hash (void *key, void *_ht)
{
	bi_hashtab_t *ht = (bi_hashtab_t *)_ht;
	P_INT (ht->pr, 0) = (long) (key);
	P_INT (ht->pr, 1) = ht->ud;
	PR_ExecuteProgram (ht->pr, ht->gh);
	return R_INT (ht->pr);
}

static int
bi_compare (void *key1, void *key2, void *_ht)
{
	bi_hashtab_t *ht = (bi_hashtab_t *)_ht;
	P_INT (ht->pr, 0) = (long) (key1);
	P_INT (ht->pr, 1) = (long) (key2);
	P_INT (ht->pr, 2) = ht->ud;
	PR_ExecuteProgram (ht->pr, ht->cmp);
	return R_INT (ht->pr);
}

static void
bi_free (void *key, void *_ht)
{
	bi_hashtab_t *ht = (bi_hashtab_t *)_ht;
	P_INT (ht->pr, 0) = (long) (key);
	P_INT (ht->pr, 1) = ht->ud;
	PR_ExecuteProgram (ht->pr, ht->f);
}

static void
bi_Hash_NewTable (progs_t *pr)
{
	hash_resources_t *res = PR_Resources_Find (pr, "Hash");
	int         tsize = P_INT (pr, 0);
	const char *(*gk)(void*,void*);
	void        (*f)(void*,void*);
	bi_hashtab_t *ht;

	ht = PR_Zone_Malloc (pr, sizeof (bi_hashtab_t));
	ht->pr = pr;
	ht->gk = P_FUNCTION (pr, 1);
	ht->f = P_FUNCTION (pr, 2);
	ht->ud = P_INT (pr, 3);		// don't convert pointer for speed reasons

	ht->next = res->tabs;
	ht->prev = &res->tabs;
	if (ht->next)
		ht->next->prev = &ht->next;

	gk = ht->gk ? bi_get_key : 0;
	f = ht->f ? bi_free : 0;
	ht->tab = Hash_NewTable (tsize, gk, f, ht);
	RETURN_POINTER (pr, ht);
}

static void
bi_Hash_SetHashCompare (progs_t *pr)
{
	bi_hashtab_t *ht = &P_STRUCT (pr, bi_hashtab_t, 0);
	unsigned long (*gh)(void*,void*);
	int         (*cmp)(void*,void*,void*);

	ht->gh = P_FUNCTION (pr, 1);
	ht->cmp = P_FUNCTION (pr, 2);
	gh = ht->gh ? bi_get_hash : 0;
	cmp = ht->cmp ? bi_compare : 0;
	Hash_SetHashCompare (ht->tab, gh, cmp);
}

static void
bi_Hash_DelTable (progs_t *pr)
{
	bi_hashtab_t *ht = &P_STRUCT (pr, bi_hashtab_t, 0);

	Hash_DelTable (ht->tab);
	*ht->prev = ht->next;
	PR_Zone_Free (pr, ht);
}

static void
bi_Hash_FlushTable (progs_t *pr)
{
	bi_hashtab_t *ht = &P_STRUCT (pr, bi_hashtab_t, 0);

	Hash_FlushTable (ht->tab);
}

static void
bi_Hash_Add (progs_t *pr)
{
	bi_hashtab_t *ht = &P_STRUCT (pr, bi_hashtab_t, 0);

	R_INT (pr) = Hash_Add (ht->tab, (void *) (long) P_INT (pr, 1));
}

static void
bi_Hash_AddElement (progs_t *pr)
{
	bi_hashtab_t *ht = &P_STRUCT (pr, bi_hashtab_t, 0);

	R_INT (pr) = Hash_AddElement (ht->tab, (void *) (long) P_INT (pr, 1));
}

static void
bi_Hash_Find (progs_t *pr)
{
	bi_hashtab_t *ht = &P_STRUCT (pr, bi_hashtab_t, 0);

	R_INT (pr) = (long) Hash_Find (ht->tab, P_GSTRING (pr, 1));
}

static void
bi_Hash_FindElement (progs_t *pr)
{
	bi_hashtab_t *ht = &P_STRUCT (pr, bi_hashtab_t, 0);

	R_INT (pr) = (long) Hash_FindElement (ht->tab,
										  (void *) (long) P_INT (pr, 1));
}

static void
bi_Hash_FindList (progs_t *pr)
{
	bi_hashtab_t *ht = &P_STRUCT (pr, bi_hashtab_t, 0);
	void      **list, **l;
	pr_type_t  *pr_list;
	int         count;

	list = Hash_FindList (ht->tab, P_GSTRING (pr, 1));
	for (count = 1, l = list; *l; l++)
		count++;
	pr_list = PR_Zone_Malloc (pr, count * sizeof (pr_type_t));
	for (count = 0, l = list; *l; l++)
		pr_list[count++].integer_var = (long) *l;
	free (list);
	RETURN_POINTER (pr, pr_list);
}

static void
bi_Hash_FindElementList (progs_t *pr)
{
	bi_hashtab_t *ht = &P_STRUCT (pr, bi_hashtab_t, 0);
	void      **list, **l;
	pr_type_t  *pr_list;
	int         count;

	list = Hash_FindElementList (ht->tab, (void *) (long) P_INT (pr, 1));
	for (count = 1, l = list; *l; l++)
		count++;
	pr_list = PR_Zone_Malloc (pr, count * sizeof (pr_type_t));
	for (count = 0, l = list; *l; l++)
		pr_list[count++].integer_var = (long) *l;
	free (list);
	RETURN_POINTER (pr, pr_list);
}

static void
bi_Hash_Del (progs_t *pr)
{
	bi_hashtab_t *ht = &P_STRUCT (pr, bi_hashtab_t, 0);

	R_INT (pr) = (long) Hash_Del (ht->tab, P_GSTRING (pr, 1));
}

static void
bi_Hash_DelElement (progs_t *pr)
{
	bi_hashtab_t *ht = &P_STRUCT (pr, bi_hashtab_t, 0);

	R_INT (pr) = (long) Hash_DelElement (ht->tab,
										 (void *) (long) P_INT (pr, 1));
}

static void
bi_Hash_Free (progs_t *pr)
{
	bi_hashtab_t *ht = &P_STRUCT (pr, bi_hashtab_t, 0);

	Hash_Free (ht->tab, (void *) (long) P_INT (pr, 1));
}

static void
bi_Hash_String (progs_t *pr)
{
	R_INT (pr) = Hash_String (P_GSTRING (pr, 0));
}

static void
bi_Hash_Buffer (progs_t *pr)
{
	R_INT (pr) = Hash_Buffer (P_GPOINTER (pr, 0), P_INT (pr, 1));
}

static void
bi_Hash_GetList (progs_t *pr)
{
	bi_hashtab_t *ht = &P_STRUCT (pr, bi_hashtab_t, 0);
	void      **list, **l;
	pr_type_t  *pr_list;
	int         count;

	list = Hash_GetList (ht->tab);
	for (count = 1, l = list; *l; l++)
		count++;
	pr_list = PR_Zone_Malloc (pr, count * sizeof (pr_type_t));
	for (count = 0, l = list; *l; l++)
		pr_list[count++].integer_var = (long) *l;
	free (list);
	RETURN_POINTER (pr, pr_list);
}

static void
bi_Hash_Stats (progs_t *pr)
{
	bi_hashtab_t *ht = &P_STRUCT (pr, bi_hashtab_t, 0);

	Hash_Stats (ht->tab);
}

static void
bi_hash_clear (progs_t *pr, void *data)
{
	hash_resources_t *res = (hash_resources_t *) data;
	bi_hashtab_t *ht;

	for (ht = res->tabs; ht; ht = ht->next)
		Hash_DelTable (ht->tab);
	res->tabs = 0;
}

void
Hash_Progs_Init (progs_t *pr)
{
	hash_resources_t *res = malloc (sizeof (hash_resources_t));
	res->tabs = 0;

	PR_Resources_Register (pr, "Hash", res, bi_hash_clear);
	PR_AddBuiltin (pr, "Hash_NewTable", bi_Hash_NewTable, -1);
	PR_AddBuiltin (pr, "Hash_SetHashCompare", bi_Hash_SetHashCompare, -1);
	PR_AddBuiltin (pr, "Hash_DelTable", bi_Hash_DelTable, -1);
	PR_AddBuiltin (pr, "Hash_FlushTable", bi_Hash_FlushTable, -1);
	PR_AddBuiltin (pr, "Hash_Add", bi_Hash_Add, -1);
	PR_AddBuiltin (pr, "Hash_AddElement", bi_Hash_AddElement, -1);
	PR_AddBuiltin (pr, "Hash_Find", bi_Hash_Find, -1);
	PR_AddBuiltin (pr, "Hash_FindElement", bi_Hash_FindElement, -1);
	PR_AddBuiltin (pr, "Hash_FindList", bi_Hash_FindList, -1);
	PR_AddBuiltin (pr, "Hash_FindElementList", bi_Hash_FindElementList, -1);
	PR_AddBuiltin (pr, "Hash_Del", bi_Hash_Del, -1);
	PR_AddBuiltin (pr, "Hash_DelElement", bi_Hash_DelElement, -1);
	PR_AddBuiltin (pr, "Hash_Free", bi_Hash_Free, -1);
	PR_AddBuiltin (pr, "Hash_String", bi_Hash_String, -1);
	PR_AddBuiltin (pr, "Hash_Buffer", bi_Hash_Buffer, -1);
	PR_AddBuiltin (pr, "Hash_GetList", bi_Hash_GetList, -1);
	PR_AddBuiltin (pr, "Hash_Stats", bi_Hash_Stats, -1);
}
