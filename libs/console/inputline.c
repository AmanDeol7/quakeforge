/*
	sv_console.c

	ncurses console for the server

	Copyright (C) 2001       Bill Currie <bill@taniwha.org>

	Author: Bill Currie <bill@taniwha.org>
	Date: 2001/7/10

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
#include <stdlib.h>
#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif

#include "QF/cmd.h"
#include "QF/console.h"
#include "QF/keys.h"

#include "compat.h"

struct inputline_s *
Con_CreateInputLine (int lines, int width, char prompt)
{
	char		   *l, **p;
	int				size;
	inputline_t	   *inputline;
	int				i;

	size = sizeof (inputline_t);	// space for the header
	size += sizeof (char *[lines]);	// space for the line pointers
	size += lines * width;			// space for the lines themselves

	inputline = calloc (1, size);
	p = (char**)(inputline + 1);
	l = (char*)&p[lines];

	inputline->lines = p;
	inputline->num_lines = lines;
	inputline->line_size = width;
	while (lines--) {
		*p++ = l;
		l += width;
	}
	inputline->prompt_char = prompt;

	for (i = 0; i < inputline->num_lines; i++)
		inputline->lines[i][0] = prompt;
	inputline->linepos = 1;
	return inputline;
}

void
Con_DestroyInputLine (inputline_t *inputline)
{
	free (inputline);
}

void
Con_ProcessInputLine (inputline_t *il, int ch)
{
	int			i;
	char       *text;

	switch (ch) {
		case K_RETURN:
			if (il->enter)
				il->enter (il->lines[il->edit_line] + 1);
			il->edit_line = (il->edit_line + 1) % il->num_lines;
			il->history_line = il->edit_line;
			il->lines[il->edit_line][0] = il->prompt_char;
			il->lines[il->edit_line][1] = 0;
			il->linepos = 1;
			break;
		case K_TAB:
			if (il->complete)
				il->complete (il);
			break;
		case K_BACKSPACE:
			if (il->linepos > 1) {
				strcpy (il->lines[il->edit_line] + il->linepos - 1,
						il->lines[il->edit_line] + il->linepos);
				il->linepos--;
			}
			break;
		case K_DELETE:
			if (il->linepos < strlen (il->lines[il->edit_line]))
				strcpy (il->lines[il->edit_line] + il->linepos,
						il->lines[il->edit_line] + il->linepos + 1);
			break;
		case K_RIGHT:
			if (il->linepos < strlen (il->lines[il->edit_line]))
				il->linepos++;
			break;
		case K_LEFT:
			if (il->linepos > 1)
				il->linepos--;
			break;
		case K_UP:
			{
				int j = (il->history_line + il->num_lines - 1) % il->num_lines;
				if (j == il->edit_line || !il->lines[j][1])
					break; // don't let it wrap
				il->history_line = j;
			}
			strcpy (il->lines[il->edit_line], il->lines[il->history_line]);
			il->linepos = strlen (il->lines[il->edit_line]);
			break;
		case K_DOWN:
			if (il->history_line == il->edit_line)
				break; // don't let it wrap
			il->history_line = (il->history_line + 1) % il->num_lines;
			if (il->history_line == il->edit_line) {
				il->lines[il->edit_line][0] = ']';
				il->lines[il->edit_line][1] = 0;
				il->linepos = 1;
			} else {
				strcpy (il->lines[il->edit_line], il->lines[il->history_line]);
				il->linepos = strlen (il->lines[il->edit_line]);
			}
			break;
		case K_HOME:
			il->linepos = 1;
			break;
		case K_END:
			il->linepos = strlen (il->lines[il->edit_line]);
			break;
		default:
			if (ch >= ' ' && ch < 256 && ch != 127) {
				i = strlen (il->lines[il->edit_line]);
				if (i >= il->line_size - 1)
					break;
				// This also moves the ending \0
				memmove (il->lines[il->edit_line] + il->linepos + 1,
						il->lines[il->edit_line] + il->linepos,
						i - il->linepos + 1);
				il->lines[il->edit_line][il->linepos] = ch;
				il->linepos++;
			}
			break;
	}
	i = il->linepos - 1;
	if (il->scroll > i)
		il->scroll = i;
	if (il->scroll < i - (il->width - 2) + 1)
		il->scroll = i - (il->width - 2) + 1;
	text = il->lines[il->edit_line] + il->scroll;
	if ((int)strlen (text + 1) < il->width - 2) {
		text = il->lines[il->edit_line];
		il->scroll = strlen (text + 1) - (il->width - 2);
		il->scroll = max (il->scroll, 0);
	}
	if (il->draw)
		il->draw (il);
}
