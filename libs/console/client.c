/*
	console.c

	(description)

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

#include <stdarg.h>
#include <errno.h>

#include "QF/cbuf.h"
#include "QF/cmd.h"
#include "QF/console.h"
#include "QF/cvar.h"
#include "QF/draw.h"
#include "QF/dstring.h"
#include "QF/input.h"
#include "QF/keys.h"
#include "QF/plugin.h"
#include "QF/qargs.h"
#include "QF/quakefs.h"
#include "QF/screen.h"
#include "QF/sys.h"
#include "QF/va.h"
#include "QF/vid.h"
#include "QF/gib_builtin.h"

#include "compat.h"

// XXX check InputLine.h in ruamoko/include
typedef struct {
	int         x, y;
	int         cursor;
} il_data_t;

static general_data_t plugin_info_general_data;
console_data_t con_data;

static old_console_t   con_main;
static old_console_t   con_chat;
static old_console_t  *con;

static float       con_cursorspeed = 4;

static cvar_t *con_notifytime;				// seconds
static cvar_t *cl_chatmode;

#define	NUM_CON_TIMES 4
static float con_times[NUM_CON_TIMES];	// realtime time the line was generated
										// for transparent notify lines

static int  con_totallines;				// total lines in console scrollback
static int  con_vislines;
static int  con_notifylines;			// scan lines to clear for notify lines

static qboolean con_debuglog;
static qboolean chat_team;

#define		MAXCMDLINE	256
static inputline_t *input_line;
static inputline_t *say_line;
static inputline_t *say_team_line;

static qboolean con_initialized;


static void
ClearNotify (void)
{
	int			i;

	for (i = 0; i < NUM_CON_TIMES; i++)
		con_times[i] = 0;
}


static void
ToggleConsole_f (void)
{
	Con_ClearTyping (input_line, 0);

	if (key_dest == key_console && !con_data.force_commandline) {
		key_dest = key_game;
		game_target = IMT_0;
	} else {
		key_dest = key_console;
		game_target = IMT_CONSOLE;
	}

	ClearNotify ();
}

static void
ToggleChat_f (void)
{
	Con_ClearTyping (input_line, 0);

	if (key_dest == key_console && !con_data.force_commandline) {
		key_dest = key_game;
		game_target = IMT_0;
	} else {
		key_dest = key_console;
		game_target = IMT_CONSOLE;
	}

	ClearNotify ();
}

static void
Clear_f (void)
{
	con_main.numlines = 0;
	con_chat.numlines = 0;
	memset (con_main.text, ' ', CON_TEXTSIZE);
	memset (con_chat.text, ' ', CON_TEXTSIZE);
	con_main.display = con_main.current;
}

static void
MessageMode_f (void)
{
	if (con_data.force_commandline)
		return;
	chat_team = false;
	key_dest = key_message;
	game_target = IMT_CONSOLE;
}

static void
MessageMode2_f (void)
{
	if (con_data.force_commandline)
		return;
	chat_team = true;
	key_dest = key_message;
	game_target = IMT_CONSOLE;
}

static void
Resize (old_console_t *con)
{
	char		tbuf[CON_TEXTSIZE];
	int			width, oldwidth, oldtotallines, numlines, numchars, i, j;

	width = (vid.width >> 3) - 2;

	if (width == con_linewidth)
		return;

	if (width < 1) {					// video hasn't been initialized yet
		width = 38;
		con_linewidth = width;
		con_totallines = CON_TEXTSIZE / con_linewidth;
		memset (con->text, ' ', CON_TEXTSIZE);
	} else {
		oldwidth = con_linewidth;
		con_linewidth = width;
		oldtotallines = con_totallines;
		con_totallines = CON_TEXTSIZE / con_linewidth;
		numlines = oldtotallines;

		if (con_totallines < numlines)
			numlines = con_totallines;

		numchars = oldwidth;

		if (con_linewidth < numchars)
			numchars = con_linewidth;

		memcpy (tbuf, con->text, CON_TEXTSIZE);
		memset (con->text, ' ', CON_TEXTSIZE);

		for (i = 0; i < numlines; i++) {
			for (j = 0; j < numchars; j++) {
				con->text[(con_totallines - 1 - i) * con_linewidth + j] =
					tbuf[((con->current - i + oldtotallines) %
						  oldtotallines) * oldwidth + j];
			}
		}

		ClearNotify ();
	}
	say_team_line->width = con_linewidth - 9;
	say_line->width = con_linewidth - 4;
	input_line->width = con_linewidth;
							
	con->current = con_totallines - 1;
	con->display = con->current;
}

/*
	C_CheckResize

	If the line width has changed, reformat the buffer.
*/
static void
C_CheckResize (void)
{
	Resize (&con_main);
	Resize (&con_chat);
}

static void
Condump_f (void)
{
	int         line = con->current - con->numlines;
	const char *start, *end;
	QFile      *file;
	char        name[MAX_OSPATH];

	if (Cmd_Argc () != 2) {
		Con_Printf ("usage: condump <filename>\n");
		return;
	}

	if (strchr (Cmd_Argv (1), '/') || strchr (Cmd_Argv (1), '\\')) {
		Con_Printf ("invalid character in filename\n");
		return;
	}
	snprintf (name, sizeof (name), "%s/%s.txt", com_gamedir, Cmd_Argv (1));

	if (!(file = Qopen (name, "wt"))) {
		Con_Printf ("could not open %s for writing: %s\n", name,
					strerror (errno));
		return;
	}

	while (line < con->current) {
		start = &con->text[(line % con_totallines) * con_linewidth];
		end = start + con_linewidth;
		while (end > start && end[-1] != ' ')
			end--;
		Qprintf (file, "%.*s\n", (int)(end - start), start);
		line++;
	}

	Qclose (file);
}

qboolean
CheckForCommand (const char *line)
{
	char        command[128];
	const char *cmd;
	int         i;

	for (i = 0; i < 127; i++)

		if (line[i] <= ' ')
			break;
		else
			command[i] = line[i];
	command[i] = 0;
	
	cmd = Cmd_CompleteCommand (command);
	if (!cmd || strcmp (cmd, command))
		cmd = Cvar_CompleteVariable (command);
	if (!cmd || strcmp (cmd, command))
		return false;					// just a chat message
	return true;
}

static void
C_ExecLine (const char *line)
{
	if (line[0] == '/' && line [1] == '/')
		goto no_lf;
	else if (line[0] == '|')
		Cbuf_AddText (con_data.cbuf, line);
	else if (line[0] == '\\' || line[0] == '/')
		Cbuf_AddText (con_data.cbuf, line + 1);
	else if (cl_chatmode->int_val != 1 && CheckForCommand (line))
		Cbuf_AddText (con_data.cbuf, line);
	else if (cl_chatmode->int_val) {
		Cbuf_AddText (con_data.cbuf, "say ");
		Cbuf_AddText (con_data.cbuf, line);
	} else
		Cbuf_AddText (con_data.cbuf, line);
	Cbuf_AddText (con_data.cbuf, "\n");
  no_lf:
	Con_Printf ("%s\n", line);
}

static void
C_Say (const char *line)
{
	if (!*line)
		return;

	Cbuf_AddText (con_data.cbuf, "say \"");
	Cbuf_AddText (con_data.cbuf, line);
	Cbuf_AddText (con_data.cbuf, "\"\n");
	key_dest = key_game;
	game_target = IMT_0;
}

static void
C_SayTeam (const char *line)
{
	if (!*line)
		return;

	Cbuf_AddText (con_data.cbuf, "say_team \"");
	Cbuf_AddText (con_data.cbuf, line);
	Cbuf_AddText (con_data.cbuf, "\"\n");
	key_dest = key_game;
	game_target = IMT_0;
}


void
C_GIB_Print_Center_f (void)
{
	if (GIB_Argc () != 2) {
		Cbuf_Error ("syntax",
		  "print::center: invalid syntax\n"
		  "usage: print::center text");
		return;
	}
	SCR_CenterPrint (GIB_Argv(1));
}


static void
C_Init (void)
{
	Menu_Init ();

	con_notifytime = Cvar_Get ("con_notifytime", "3", CVAR_NONE, NULL,
							   "How long in seconds messages are displayed "
							   "on screen");
	cl_chatmode = Cvar_Get ("cl_chatmode", "2", CVAR_NONE, NULL,
							"Controls when console text will be treated as a "
							"chat message: 0 - never, 1 - always, 2 - smart");

	con_debuglog = COM_CheckParm ("-condebug");

	con = &con_main;
	con_linewidth = -1;

	input_line = Con_CreateInputLine (32, MAXCMDLINE, ']');
	input_line->complete = Con_BasicCompleteCommandLine;
	input_line->enter = C_ExecLine;
	input_line->width = con_linewidth;
	input_line->user_data = 0;
	input_line->draw = 0;

	say_line = Con_CreateInputLine (32, MAXCMDLINE, ' ');
	say_line->complete = 0;
	say_line->enter = C_Say;
	say_line->width = con_linewidth - 5;
	say_line->user_data = 0;
	say_line->draw = 0;

	say_team_line = Con_CreateInputLine (32, MAXCMDLINE, ' ');
	say_team_line->complete = 0;
	say_team_line->enter = C_SayTeam;
	say_team_line->width = con_linewidth - 10;
	say_team_line->user_data = 0;
	say_team_line->draw = 0;

	C_CheckResize ();

	Con_Printf ("Console initialized.\n");

	// register our commands
	Cmd_AddCommand ("toggleconsole", ToggleConsole_f,
					"Toggle the console up and down");
	Cmd_AddCommand ("togglechat", ToggleChat_f,
					"Toggle the console up and down");
	Cmd_AddCommand ("messagemode", MessageMode_f,
					"Prompt to send a message to everyone");
	Cmd_AddCommand ("messagemode2", MessageMode2_f,
					"Prompt to send a message to only people on your team");
	Cmd_AddCommand ("clear", Clear_f, "Clear the console");
	Cmd_AddCommand ("condump", Condump_f, "dump the console text to a "
					"file");
					
	// register GIB builtins
	GIB_Builtin_Add ("print::center", C_GIB_Print_Center_f, GIB_BUILTIN_NORMAL);
	
	con_initialized = true;
}

static void
C_Shutdown (void)
{
}

static void
Linefeed (void)
{
	con->x = 0;
	if (con->display == con->current)
		con->display++;
	con->current++;
	if (con->numlines < con_totallines)
		con->numlines++;
	memset (&con->text[(con->current % con_totallines) * con_linewidth],
			' ', con_linewidth);
}

/*
	C_Print

	Handles cursor positioning, line wrapping, etc
	All console printing must go through this in order to be logged to disk
	If no console is visible, the notify window will pop up.
*/
void
C_Print (const char *fmt, va_list args)
{
	char       *s;
	static dstring_t *buffer;
	int         mask, c, l, y;
	static int  cr;

	if (!buffer)
		buffer = dstring_new ();

	dvsprintf (buffer, fmt, args);

	// log all messages to file
	if (con_debuglog)
		Sys_DebugLog (va ("%s/qconsole.log", com_gamedir), "%s", buffer->str);

	if (!con_initialized)
		return;

	s = buffer->str;

	if (s[0] == 1 || s[0] == 2) {
		mask = 128;						// go to colored text
		s++;
	} else
		mask = 0;

	while ((c = (byte)*s)) {
		// count word length
		for (l = 0; l < con_linewidth; l++)
			if (s[l] <= ' ')
				break;

		// word wrap
		if (l != con_linewidth && (con->x + l > con_linewidth))
			con->x = 0;

		*s++ = sys_char_map[c];

		if (cr) {
			con->current--;
			cr = false;
		}

		if (!con->x) {
			Linefeed ();
			// mark time for transparent overlay
			if (con->current >= 0)
				con_times[con->current % NUM_CON_TIMES] = *con_data.realtime;
		}

		switch (c) {
			case '\n':
				con->x = 0;
				break;

			case '\r':
				con->x = 0;
				cr = 1;
				break;

			default:					// display character and advance
				y = con->current % con_totallines;
				con->text[y * con_linewidth + con->x] = c | mask | con_data.ormask;
				con->x++;
				if (con->x >= con_linewidth)
					con->x = 0;
				break;
		}

	}

	// echo to debugging console
	if ((byte)buffer->str[0] > 2)
		fputs (buffer->str, stdout);
	else if ((byte)buffer->str[0])
		fputs (buffer->str + 1, stdout);
}

static void
C_KeyEvent (knum_t key, short unicode, qboolean down)
{
	inputline_t *il;

	if (!down)
		return;

	if (down && (key == QFK_ESCAPE || unicode == '\x1b')) {
		switch (key_dest) {
			case key_menu:
				Menu_Leave ();
				return;
			case key_message:
				if (chat_team) {
					Con_ClearTyping (say_team_line, 1);
				} else {
					Con_ClearTyping (say_line, 1);
				}
				key_dest = key_game;
				game_target = IMT_0;
				return;
			case key_console:
				if (!con_data.force_commandline) {
					Cbuf_AddText (con_data.cbuf, "toggleconsole\n");
					return;
				}
			case key_game:
				Menu_Enter ();
				return;
			default:
				Sys_Error ("Bad key_dest");
		}
	}

	if (key_dest == key_menu) {
		Menu_KeyEvent (key, unicode, down);
		return;
	}

	if (key_dest == key_message) {
		if (chat_team) {
			il = say_team_line;
		} else {
			il = say_line;
		}
	} else {
		switch (key) {
			case QFK_PAGEUP:
				if (keydown[QFK_RCTRL] || keydown[QFK_LCTRL])
					con->display = 0;
				else
					con->display -= 10;
				if (con->display < con->current - con->numlines)
					con->display = con->current - con->numlines;
				return;
			case QFK_PAGEDOWN:
				if (keydown[QFK_RCTRL] || keydown[QFK_LCTRL])
					con->display = con->current;
				else
					con->display += 10;
				if (con->display > con->current)
					con->display = con->current;
				return;
			case QFM_WHEEL_UP:
				con->display -= 3;
				if (con->display < con->current - con->numlines)
					con->display = con->current - con->numlines;
				return;
			case QFM_WHEEL_DOWN:
				con->display += 3;
				if (con->display > con->current)
					con->display = con->current;
				return;
			default:
				break;
		}
		il = input_line;
	}
	Con_ProcessInputLine (il, key >= 256 ? key : unicode);
}

/* DRAWING */

static void
DrawInputLine (int x, int y, int cursor, inputline_t *il)
{
	const char *s = il->lines[il->edit_line] + il->scroll;

	if (il->scroll) {
		Draw_Character (x, y, '<' | 0x80);
		Draw_nString (x + 8, y, s + 1, il->width - 2);
	} else {
		Draw_nString (x, y, s, il->width - 1);
	}
	if (cursor) {
		float       t = *con_data.realtime * con_cursorspeed;
		int         ch = 10 + ((int) (t) & 1);
		Draw_Character (x + ((il->linepos - il->scroll) << 3), y, ch);
	}
	if (strlen (s) >= il->width)
		Draw_Character (x + ((il->width - 1) << 3), y, '>' | 0x80);
}

void
C_DrawInputLine (inputline_t *il)
{
	il_data_t   *data = il->user_data;
	DrawInputLine (data->x, data->y, data->cursor, il);
}

static void
DrawInput (void)
{
	if (key_dest != key_console)// && !con_data.force_commandline)
		return;				// don't draw anything (always draw if not active)

	DrawInputLine (8, con_vislines - 22, 1, input_line);
}

/*
	DrawNotify

	Draws the last few lines of output transparently over the game top
*/
static void
DrawNotify (void)
{
	int			i, v;
	char	   *text;
	float		time;

	v = 0;
	for (i = con->current - NUM_CON_TIMES + 1; i <= con->current; i++) {
		if (i < 0)
			continue;
		time = con_times[i % NUM_CON_TIMES];
		if (time == 0)
			continue;
		time = *con_data.realtime - time;
		if (time > con_notifytime->value)
			continue;
		text = con->text + (i % con_totallines) * con_linewidth;

		clearnotify = 0;
		scr_copytop = 1;

		Draw_nString (8, v, text, con_linewidth);
		v += 8;
	}

	if (key_dest == key_message) {
		clearnotify = 0;
		scr_copytop = 1;

		if (chat_team) {
			Draw_String (8, v, "say_team:");
			DrawInputLine (80, v, 1, say_team_line);
		} else {
			Draw_String (8, v, "say:");
			DrawInputLine (40, v, 1, say_line);
		}
		v += 8;
	}

	if (v > con_notifylines)
		con_notifylines = v;
}

/*
	DrawConsole

	Draws the console with the solid background
*/
static void
DrawConsole (int lines)
{
	char	   *text;
	int			row, rows, i, x, y;

	if (lines <= 0)
		return;

	// draw the background
	Draw_ConsoleBackground (lines);

	// draw the text
	con_vislines = lines;

	// changed to line things up better
	rows = (lines - 22) >> 3;			// rows of text to draw

	y = lines - 30;

	// draw from the bottom up
	if (con->display != con->current) {
		// draw arrows to show the buffer is backscrolled
		for (x = 0; x < con_linewidth; x += 4)
			Draw_Character ((x + 1) << 3, y, '^');

		y -= 8;
		rows--;
	}
	
	row = con->display;
	for (i = 0; i < rows; i++, y -= 8, row--) {
		if (row < 0)
			break;
		if (con->current - row >= con_totallines)
			break;						// past scrollback wrap point

		text = con->text + (row % con_totallines) * con_linewidth;

		Draw_nString(8, y, text, con_linewidth);
	}

	// draw the input prompt, user text, and cursor if desired
	DrawInput ();
}

static void
DrawDownload (int lines)
{
	char		dlbar[1024];
	const char *text;
	int			i, j, x, y, n;

	if (!con_data.dl_name || !*con_data.dl_name)
		return;

	text = COM_SkipPath(con_data.dl_name);

	x = con_linewidth - ((con_linewidth * 7) / 40);
	y = x - strlen (text) - 8;
	i = con_linewidth / 3;
	if (strlen (text) > i) {
		y = x - i - 11;
		strncpy (dlbar, text, i);
		dlbar[i] = 0;
		strncat (dlbar, "...", sizeof (dlbar) - strlen (dlbar));
	} else
		strncpy (dlbar, text, sizeof (dlbar));
	strncat (dlbar, ": ", sizeof (dlbar) - strlen (dlbar));
	i = strlen (dlbar);
	dlbar[i++] = '\x80';
	// where's the dot go?
	if (con_data.dl_percent == 0)
		n = 0;
	else
		n = y * *con_data.dl_percent / 100;
	for (j = 0; j < y; j++)
		if (j == n)
			dlbar[i++] = '\x83';
		else
			dlbar[i++] = '\x81';
	dlbar[i++] = '\x82';
	dlbar[i] = 0;

	snprintf (dlbar + strlen (dlbar), sizeof (dlbar) - strlen (dlbar),
			  " %02d%%", *con_data.dl_percent);

	// draw it
	y = lines - 22 + 8;
	for (i = 0; i < strlen (dlbar); i++)
		Draw_Character ((i + 1) << 3, y, dlbar[i]);
}

static void
C_DrawConsole (int lines)
{
	if (lines) {
		DrawConsole (lines);
		DrawDownload (lines);
	} else {
		if (key_dest == key_game || key_dest == key_message)
			DrawNotify ();				// only draw notify in game
	}
	if (key_dest == key_menu)
		Menu_Draw ();
}

static void
C_ProcessInput (void)
{
}

static void
C_NewMap (void)
{
	static char old_gamedir[MAX_OSPATH];

	if (!strequal (old_gamedir, com_gamedir))
		Menu_Load ();
	strcpy (old_gamedir, com_gamedir);
}

static general_funcs_t plugin_info_general_funcs = {
	C_Init,
	C_Shutdown,
};

static console_funcs_t plugin_info_console_funcs = {
	C_Print,
	C_ProcessInput,
	C_KeyEvent,
	C_DrawConsole,
	C_CheckResize,
	C_NewMap,
};

static plugin_funcs_t plugin_info_funcs = {
	&plugin_info_general_funcs,
	0,
	0,
	&plugin_info_console_funcs,
};

static plugin_data_t plugin_info_data = {
	&plugin_info_general_data,
	0,
	0,
	&con_data,
};

static plugin_t plugin_info = {
	qfp_console,
	0,
	QFPLUGIN_VERSION,
	"0.1",
	"client console driver",
	"Copyright (C) 1996-1997 id Software, Inc.\n"
		"Copyright (C) 1999,2000,2001  contributors of the QuakeForge "
		"project\n"
		"Please see the file \"AUTHORS\" for a list of contributors",
	&plugin_info_funcs,
	&plugin_info_data,
};

QFPLUGIN plugin_t *
PLUGIN_INFO(console, client) (void)
{
	return &plugin_info;
}
