/*
	#FILENAME#

	#DESCRIPTION#

	Copyright (C) 2001 #AUTHOR#

	Author: #AUTHOR#
	Date: #DATE#

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

	$Id$
*/

#ifndef __options_h
#define __options_h

#include "QF/qtypes.h"

typedef struct {
	qboolean	cow;				// Turn constants into variables if written to
	qboolean	debug;				// Generate debug info for the engine
	int			progsversion;		// Progs version to generate code for
} code_options_t;

typedef struct {
	qboolean	promote;			// Promote warnings to errors
	qboolean	cow;				// Warn on copy-on-write detection
	qboolean	undefined_function;	// Warn on undefined function use
	qboolean	uninited_variable;	// Warn on use of uninitialized vars
	qboolean	vararg_integer;		// Warn on passing an integer to vararg func
	qboolean	integer_divide;		// Warn on integer constant division
} warn_options_t;

typedef struct {
	qboolean	promote;			// Promote notices to warnings
	qboolean	silent;				// don't even bother (overrides promote)
} notice_options_t;

typedef struct {
	code_options_t	code;			// Code generation options
	warn_options_t	warnings;		// Warning options
	notice_options_t notices;		// Notice options

	int				verbosity;		// 0=silent, goes up to 2 currently
	qboolean		save_temps;		// save temporary files
	qboolean		files_dat;		// generate files.dat
	qboolean		traditional;	// behave more like qcc
	qboolean		compile;		// serparate compilation mode
	int				strip_path;		// number of leading path elements to strip
									// from source file names
} options_t;

extern options_t options;
int DecodeArgs (int argc, char **argv);
extern const char *progs_src;

extern const char *this_program;
extern const char *sourcedir;

#endif//__options_h
