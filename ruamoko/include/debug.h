/*
	debug.h

	Debugging function definitions

	Copyright (C) 2002 Bill Currie <taniwha@quakeforge.net>
	Copyright (C) 2002 Jeff Teunissen <deek@quakeforge.net>

	This file is part of the Ruamoko Standard Library.

	This library is free software; you can redistribute it and/or modify it
	under the terms of the GNU Lesser General Public License as published by
	the Free Software Foundation; either version 2.1 of the License, or (at
	your option) any later version.

	This library is distributed in the hope that it will be useful, but
	WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

	See the GNU Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public
	License along with this program; if not, write to:

		Free Software Foundation, Inc.
		59 Temple Place - Suite 330
		Boston, MA  02111-1307, USA

	$Id$
*/
#ifndef __ruamoko_debug_h
#define __ruamoko_debug_h

/*
	abort (in QuakeC, this was break)

	Tell the engine to abort (stop) code processing.
*/
@extern void () abort;

/*
	error

	Abort (crash) the server. "e" is the message the server crashes with.
*/
@extern void (string e) error;

/*
	objerror

	Prints info on the "self" ENTITY (not object), and error message "e".
	The entity is freed.
*/
@extern void (string e) objerror;

/*
	dprint

	Print string "e" if the developer Cvar is set to a nonzero value
*/
@extern void (string s) dprint;

/*
	coredump

	Tell the engine to print all edicts (entities)
*/
@extern void () coredump;

/*
	traceon

	Enable instruction trace in the interpreter
*/
@extern void () traceon;

/*
	traceoff

	Disable instruction trace in the interpreter
*/
@extern void () traceoff;

/*
	eprint

	Print all information on an entity to the server console
*/
@extern void (entity e) eprint;

#endif //__ruamoko_debug_h
