/*
	qfplist.c

	Property list management

	Copyright (C) 2000  Jeff Teunissen <deek@d2dc.net>

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

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "QF/hash.h"
#include "QF/qfplist.h"
#include "QF/qtypes.h"
#include "QF/sys.h"

static plitem_t *PL_ParsePropertyListItem (pldata_t *);
static qboolean PL_SkipSpace (pldata_t *);
static char *PL_ParseQuotedString (pldata_t *);
static char *PL_ParseUnquotedString (pldata_t *);

static const char *
dict_get_key (void *i, void *unused)
{
	dictkey_t	*item = (dictkey_t *) i;
	return item->key;
}

static void
dict_free (void *i, void *unused)
{
	dictkey_t	*item = (dictkey_t *) i;
	free (item->key);
	PL_FreeItem (item->value);	// Make descended stuff get freed
	free (item);
}

static plitem_t *
PL_NewItem (pltype_t type)
{
	plitem_t   *item = calloc (1, sizeof (plitem_t));
	item->type = type;
	return item;
}

plitem_t *
PL_NewDictionary (void)
{
	plitem_t   *item = PL_NewItem (QFDictionary);
	hashtab_t  *dict = Hash_NewTable (1021, dict_get_key, dict_free, NULL);
	item->data = dict;
	return item;
}

plitem_t *
PL_NewArray (void)
{
	plitem_t   *item = PL_NewItem (QFArray);
	plarray_t  *array = calloc (1, sizeof (plarray_t));
	item->data = array;
	return item;
}

plitem_t *
PL_NewData (void *data, int size)
{
	plitem_t   *item = PL_NewItem (QFBinary);
	plbinary_t *bin = malloc (sizeof (plbinary_t));
	item->data = bin;
	bin->data = data;
	bin->size = size;
	return item;
}

plitem_t *
PL_NewString (char *str)
{
	plitem_t   *item = PL_NewItem (QFString);
	item->data = str;
	return item;
}

void
PL_FreeItem (plitem_t *item)
{
	switch (item->type) {
		case QFDictionary:
			Hash_DelTable (item->data);
			break;

		case QFArray: {
				int 	i = ((plarray_t *) item->data)->numvals;

				while (i-- > 0) {
					PL_FreeItem (((plarray_t *) item->data)->values[i]);
				}
				free (item->data);
			}
			break;

		case QFBinary:
			free (((plbinary_t *) item->data)->data);
			free (item->data);
			break;

		case QFString:
			free (item->data);
			break;
	}
	free (item);
}

plitem_t *
PL_ObjectForKey (plitem_t *item, const char *key)
{
	hashtab_t  *table = (hashtab_t *) item->data;
	dictkey_t  *k;

	if (item->type != QFDictionary)
		return NULL;

	k = (dictkey_t *) Hash_Find (table, key);
	return k ? k->value : NULL;
}

plitem_t *
PL_ObjectAtIndex (plitem_t *item, int index)
{
	plarray_t  *array = (plarray_t *) item->data;

	if (item->type != QFArray)
		return NULL;

	return index >= 0 && index < array->numvals ? array->values[index] : NULL;
}

plitem_t *
PL_D_AddObject (plitem_t *dict, plitem_t *key, plitem_t *value)
{
	dictkey_t	*k;

	if (dict->type != QFDictionary)
		return NULL;

	if (key->type != QFString)
		return NULL;

	k = malloc (sizeof (dictkey_t));

	if (!k)
		return NULL;

	k->key = strdup ((char *) key->data);
	k->value = value;

	Hash_Add ((hashtab_t *)dict->data, k);
	return dict;
}

plitem_t *
PL_A_InsertObjectAtIndex (plitem_t *array_item, plitem_t *item, int index)
{
	plarray_t  *array;

	if (array_item->type != QFArray)
		return NULL;

	array = (plarray_t *)array_item->data;

	if (array->numvals == MAX_ARRAY_INDEX)
		return NULL;

	if (index == -1)
		index = array->numvals;

	if (index < 0 || index > array->numvals)
		return NULL;

	memmove (array->values + index + 1, array->values + index,
			 (array->numvals - index) * sizeof (plitem_t *));
	array->values[index] = item;
	array->numvals++;
	return array_item;
}

plitem_t *
PL_A_AddObject (plitem_t *array_item, plitem_t *item)
{
	return PL_A_InsertObjectAtIndex (array_item, item, -1);
}

static qboolean
PL_SkipSpace (pldata_t *pl)
{
	while (pl->pos < pl->end) {
		char	c = pl->ptr[pl->pos];

		if (!isspace ((byte) c)) {
			if (c == '/' && pl->pos < pl->end - 1) {	// check for comments
				if (pl->ptr[pl->pos + 1] == '/') {
					pl->pos += 2;

					while (pl->pos < pl->end) {
						c = pl->ptr[pl->pos];

						if (c == '\n')
							break;
						pl->pos++;
					}
					if (pl->pos >= pl->end) {
						pl->error = "Reached end of string in comment";
						return false;
					}
				} else if (pl->ptr[pl->pos + 1] == '*') {	// "/*" comments
					pl->pos += 2;

					while (pl->pos < pl->end) {
						c = pl->ptr[pl->pos];
						
						if (c == '\n') {
							pl->line++;
						} else if (c == '*' && pl->pos < pl->end - 1
									&& pl->ptr[pl->pos+1] == '/') {
							pl->pos++;
							break;
						}
						pl->pos++;
					}
					if (pl->pos >= pl->end) {
						pl->error = "Reached end of string in comment";
						return false;
					}
				} else {
					return true;
				}
			} else {
				return true;
			}
		}
		if (c == '\n') {
			pl->line++;
		}
		pl->pos++;
	}
	pl->error = "Reached end of string";
	return false;
}

static char *
PL_ParseQuotedString (pldata_t *pl)
{
	unsigned int	start = ++pl->pos;
	unsigned int	escaped = 0;
	unsigned int	shrink = 0;
	qboolean		hex = false;
	char			*str;

	while (pl->pos < pl->end) {

		char	c = pl->ptr[pl->pos];

		if (escaped) {
			if (escaped == 1 && c == '0') {
				escaped++;
				hex = false;
			} else if (escaped > 1) {
				if (escaped == 2 && c == 'x') {
					hex = true;
					shrink++;
					escaped++;
				} else if (hex && isxdigit ((byte) c)) {
					shrink++;
					escaped++;
				} else if (c >= '0' && c <= '7') {
					shrink++;
					escaped++;
				} else {
					pl->pos--;
					escaped = 0;
				}
			} else {
				escaped = 0;
			}
		} else {
			if (c == '\\') {
				escaped = 1;
				shrink++;
			} else if (c == '"') {
				break;
			}
		}

		if (c == '\n') {
			pl->line++;
		}

		pl->pos++;
	}

	if (pl->pos >= pl->end) {
		pl->error = "Reached end of string while parsing quoted string";
		return NULL;
	}

	if (pl->pos - start - shrink == 0) {
		str = strdup ("");
	} else {
		char			chars[pl->pos - start - shrink];
		unsigned int	j;
		unsigned int	k;

		escaped = 0;
		hex = false;
		
		for (j = start, k = 0; j < pl->pos; j++) {

			char	c = pl->ptr[j];

			if (escaped) {
				if (escaped == 1 && c == '0') {
					chars[k] = 0;
					hex = false;
					escaped++;
				} else if (escaped > 1) {
					if (escaped == 2 && c == 'x') {
						hex = true;
						escaped++;
					} else if (hex && isxdigit ((byte) c)) {
						chars[k] <<= 4;
						chars[k] |= char2num (c);
						escaped++;
					} else if (inrange (c, '0', '7')) {
						chars[k] <<= 3;
						chars[k] |= (c - '0');
						escaped++;
					} else {
						escaped = 0;
						j--;
						k++;
					}
				} else {
					escaped = 0;
					switch (c) {
						case 'a':
							chars[k] = '\a';
							break;
						case 'b':
							chars[k] = '\b';
							break;
						case 't':
							chars[k] = '\t';
							break;
						case 'r':
							chars[k] = '\r';
							break;
						case 'n':
							chars[k] = '\n';
							break;
						case 'v':
							chars[k] = '\v';
							break;
						case 'f':
							chars[k] = '\f';
							break;
						default:
							chars[k] = c;
							break;
					}
					k++;
				}
			} else {
				chars[k] = c;
				if (c == '\\') {
					escaped = 1;
				} else {
					k++;
				}
			}
		}
		str = strncat (calloc ((pl->pos - start - shrink) + 1, 1), chars,
					   pl->pos - start - shrink);
	}
	pl->pos++;
	return str;
}

static char *
PL_ParseUnquotedString (pldata_t *pl)
{
	unsigned int	start = pl->pos;
	char			*str;

	while (pl->pos < pl->end) {
		if (!isalnum ((byte) pl->ptr[pl->pos]) && pl->ptr[pl->pos] != '_')
			break;
		pl->pos++;
	}
	str = strncat (calloc ((pl->pos - start) + 1, 1), &pl->ptr[start],
				   pl->pos - start);
	return str;
}

static plitem_t *
PL_ParsePropertyListItem (pldata_t *pl)
{
	plitem_t	*item = NULL;

	if (!PL_SkipSpace (pl))
		return NULL;

	switch (pl->ptr[pl->pos]) {
	case '{':
	{
		hashtab_t *dict;

		item = PL_NewDictionary ();
		dict = (hashtab_t *) item->data;

		pl->pos++;

		while (PL_SkipSpace (pl) && pl->ptr[pl->pos] != '}') {
			plitem_t	*key;
			plitem_t	*value;

			if (!(key = PL_ParsePropertyListItem (pl))) {
				PL_FreeItem (item);
				return NULL;
			}

			if (!(PL_SkipSpace (pl))) {
				PL_FreeItem (key);
				PL_FreeItem (item);
				return NULL;
			}

			if (key->type != QFString) {
				pl->error = "Key is not a string";
				PL_FreeItem (key);
				PL_FreeItem (item);
				return NULL;
			}

			if (pl->ptr[pl->pos] != '=') {
				pl->error = "Unexpected character (expected '=')";
				PL_FreeItem (key);
				PL_FreeItem (item);
				return NULL;
			}
			pl->pos++;

			// If there is no value, lose the key				
			if (!(value = PL_ParsePropertyListItem (pl))) {
				PL_FreeItem (key);
				PL_FreeItem (item);
				return NULL;
			}

			if (!(PL_SkipSpace (pl))) {
				PL_FreeItem (key);
				PL_FreeItem (value);
				PL_FreeItem (item);
				return NULL;
			}

			if (pl->ptr[pl->pos] == ';') {
				pl->pos++;
			} else if (pl->ptr[pl->pos] != '}') {
				pl->error = "Unexpected character (wanted ';' or '}')";
				PL_FreeItem (key);
				PL_FreeItem (value);
				PL_FreeItem (item);
				return NULL;
			}

			// Add the key/value pair to the dictionary
			if (!PL_D_AddObject (item, key, value)) {
				PL_FreeItem (key);
				PL_FreeItem (value);
				PL_FreeItem (item);
				return NULL;
			}
			PL_FreeItem (key);
		}

		if (pl->pos >= pl->end) {	// Catch the error
			pl->error = "Unexpected end of string when parsing dictionary";
			PL_FreeItem (item);
			return NULL;
		}
		pl->pos++;

		return item;
	}

	case '(': {
		plarray_t *a;

		item = PL_NewArray ();
		a = (plarray_t *) item->data;

		pl->pos++;

		while (PL_SkipSpace (pl) && pl->ptr[pl->pos] != ')') {
			plitem_t	*value;

			if (!(value = PL_ParsePropertyListItem (pl))) {
				PL_FreeItem (item);
				return NULL;
			}

			if (!(PL_SkipSpace (pl))) {
				PL_FreeItem (value);
				PL_FreeItem (item);
				return NULL;
			}

			if (pl->ptr[pl->pos] == ',') {
				pl->pos++;
			} else if (pl->ptr[pl->pos] != ')') {
				pl->error = "Unexpected character (wanted ',' or ')')";
				PL_FreeItem (value);
				PL_FreeItem (item);
				return NULL;
			}

			if (!PL_A_AddObject (item, value)) {
				pl->error = "Unexpected character (too many items in array)";
				PL_FreeItem (value);
				PL_FreeItem (item);
				return NULL;
			}
		}
		pl->pos++;

		return item;
	}

	case '<':
		pl->error = "Unexpected character in string (binary data unsupported)";
		return NULL;

	case '"': {
		char *str = PL_ParseQuotedString (pl);

		if (!str) {
			return NULL;
		} else {
			return PL_NewString (str);
		}
	}

	default: {
		char *str = PL_ParseUnquotedString (pl);

		if (!str) {
			return NULL;
		} else {
			return PL_NewString (str);
		}
	}
	} // switch
}

plitem_t *
PL_GetPropertyList (const char *string)
{
	pldata_t	*pl = calloc (1, sizeof (pldata_t));
	plitem_t	*newpl = NULL;

	pl->ptr = string;
	pl->pos = 0;
	pl->end = strlen (string);
	pl->error = NULL;
	pl->line = 1;

	if ((newpl = PL_ParsePropertyListItem (pl))) {
		free (pl);
		return newpl;
	} else {
		if (pl && pl->error && pl->error[0])
			Sys_Error ("%d,%d: %s", pl->line, pl->pos, pl->error);
		free (pl);
		return NULL;
	}
}
