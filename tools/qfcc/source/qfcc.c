/* 
	qfcc.c

	QuakeForge Code Compiler (main program)

	Copyright (C) 1996-1997 id Software, Inc.
	Copyright (C) 2001 Jeff Teunissen <deek@quakeforge.net>
	Copyright (C) 2001 Bill Currie <bill@taniwha.org>

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License as
	published by the Free Software Foundation; either version 2 of
	the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

	See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public
	License along with this program; if not, write to:

		Free Software Foundation, Inc.
		59 Temple Place - Suite 330
		Boston, MA  02111-1307, USA

*/
static const char rcsid[] =
	"$Id$";

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <QF/cbuf.h>
#include <QF/idparse.h>
#include <QF/crc.h>
#include <QF/dstring.h>
#include <QF/hash.h>
#include <QF/qendian.h>
#include <QF/sys.h>
#include <QF/va.h>

#include "qfcc.h"
#include "class.h"
#include "cmdlib.h"
#include "cpp.h"
#include "debug.h"
#include "def.h"
#include "emit.h"
#include "expr.h"
#include "function.h"
#include "idstuff.h"
#include "immediate.h"
#include "linker.h"
#include "method.h"
#include "obj_file.h"
#include "opcodes.h"
#include "options.h"
#include "reloc.h"
#include "strpool.h"
#include "struct.h"
#include "type.h"

options_t   options;

const char *sourcedir;
const char *progs_src;

char        debugfile[1024];

pr_info_t   pr;

ddef_t      *globals;
int         numglobaldefs;

int         num_localdefs;
const char *big_function = 0;

ddef_t      *fields;
int         numfielddefs;

hashtab_t  *saved_strings;

static const char *
ss_get_key (void *s, void *unused)
{
	return (const char *)s;
}

const char *
save_string (const char *str)
{
	char       *s;
	if (!saved_strings)
		saved_strings = Hash_NewTable (16381, ss_get_key, 0, 0);
	s = Hash_Find (saved_strings, str);
	if (s)
		return s;
	s = strdup (str);
	Hash_Add (saved_strings, s);
	return s;
}

void
InitData (void)
{
	int         i;

	if (pr.code) {
		codespace_delete (pr.code);
		strpool_delete (pr.strings);
	}
	memset (&pr, 0, sizeof (pr));
	pr.source_line = 1;
	pr.error_count = 0;
	pr.code = codespace_new ();
	memset (codespace_newstatement (pr.code), 0, sizeof (dstatement_t));
	pr.strings = strpool_new ();
	pr.num_functions = 1;

	pr.near_data = new_defspace ();
	pr.near_data->data = calloc (65536, sizeof (pr_type_t));
	pr.scope = new_scope (sc_global, pr.near_data, 0);
	current_scope = pr.scope;

	pr.entity_data = new_defspace ();

	numglobaldefs = 1;
	numfielddefs = 1;

	def_ret.ofs = OFS_RETURN;
	for (i = 0; i < MAX_PARMS; i++)
		def_parms[i].ofs = OFS_PARM0 + 3 * i;
}


int
WriteData (int crc)
{
	def_t      *def;
	ddef_t     *dd;
	dprograms_t progs;
	pr_debug_header_t debug;
	QFile      *h;
	int         i;

	globals = calloc (pr.scope->num_defs + 1, sizeof (ddef_t));
	fields = calloc (pr.scope->num_defs + 1, sizeof (ddef_t));

	for (def = pr.scope->head; def; def = def->def_next) {
		if (def->local || !def->name)
			continue;
		if (def->type->type == ev_func) {
		} else if (def->type->type == ev_field) {
			dd = &fields[numfielddefs++];
			def_to_ddef (def, dd, 1);
			dd->ofs = G_INT (def->ofs);
		}

		dd = &globals[numglobaldefs++];
		def_to_ddef (def, dd, 0);
		if (!def->constant
			&& def->type->type != ev_func
			&& def->type->type != ev_field && def->global)
			dd->type |= DEF_SAVEGLOBAL;
	}

	pr.strings->size = (pr.strings->size + 3) & ~3;

	if (options.verbosity >= 0) {
		printf ("%6i strofs\n", pr.strings->size);
		printf ("%6i statements\n", pr.code->size);
		printf ("%6i functions\n", pr.num_functions);
		printf ("%6i global defs\n", numglobaldefs);
		printf ("%6i locals size (%s)\n", num_localdefs, big_function);
		printf ("%6i fielddefs\n", numfielddefs);
		printf ("%6i globals\n", pr.near_data->size);
		printf ("%6i entity fields\n", pr.entity_data->size);
	}

	if (!(h = Qopen (options.output_file, "wb")))
		Sys_Error ("%s: %s\n", options.output_file, strerror(errno));
	Qwrite (h, &progs, sizeof (progs));

	progs.ofs_strings = Qtell (h);
	progs.numstrings = pr.strings->size;
	Qwrite (h, pr.strings->strings, pr.strings->size);

	progs.ofs_statements = Qtell (h);
	progs.numstatements = pr.code->size;
	for (i = 0; i < pr.code->size; i++) {
		pr.code->code[i].op = LittleShort (pr.code->code[i].op);
		pr.code->code[i].a = LittleShort (pr.code->code[i].a);
		pr.code->code[i].b = LittleShort (pr.code->code[i].b);
		pr.code->code[i].c = LittleShort (pr.code->code[i].c);
	}
	Qwrite (h, pr.code->code, pr.code->size * sizeof (dstatement_t));

	{
		dfunction_t *df;

		progs.ofs_functions = Qtell (h);
		progs.numfunctions = pr.num_functions;
		for (i = 0, df = pr.functions + 1; i < pr.num_functions; i++, df++) {
			df->first_statement = LittleLong (df->first_statement);
			df->parm_start      = LittleLong (df->parm_start);
			df->s_name          = LittleLong (df->s_name);
			df->s_file          = LittleLong (df->s_file);
			df->numparms        = LittleLong (df->numparms);
			df->locals          = LittleLong (df->locals);
		}
		Qwrite (h, pr.functions, pr.num_functions * sizeof (dfunction_t));
	}

	progs.ofs_globaldefs = Qtell (h);
	progs.numglobaldefs = numglobaldefs;
	for (i = 0; i < numglobaldefs; i++) {
		globals[i].type = LittleShort (globals[i].type);
		globals[i].ofs = LittleShort (globals[i].ofs);
		globals[i].s_name = LittleLong (globals[i].s_name);
	}
	Qwrite (h, globals, numglobaldefs * sizeof (ddef_t));

	progs.ofs_fielddefs = Qtell (h);
	progs.numfielddefs = numfielddefs;
	for (i = 0; i < numfielddefs; i++) {
		fields[i].type = LittleShort (fields[i].type);
		fields[i].ofs = LittleShort (fields[i].ofs);
		fields[i].s_name = LittleLong (fields[i].s_name);
	}
	Qwrite (h, fields, numfielddefs * sizeof (ddef_t));

	progs.ofs_globals = Qtell (h);
	progs.numglobals = pr.near_data->size;
	for (i = 0; i < pr.near_data->size; i++)
		G_INT (i) = LittleLong (G_INT (i));
	Qwrite (h, pr.near_data->data, pr.near_data->size * 4);

	if (options.verbosity >= -1)
		printf ("%6i TOTAL SIZE\n", (int) Qtell (h));

	progs.entityfields = pr.entity_data->size;

	progs.version = options.code.progsversion;
	progs.crc = crc;

	// byte swap the header and write it out
	for (i = 0; i < sizeof (progs) / 4; i++)
		((int *) &progs)[i] = LittleLong (((int *) &progs)[i]);

	Qseek (h, 0, SEEK_SET);
	Qwrite (h, &progs, sizeof (progs));
	Qclose (h);

	if (!options.code.debug) {
		return 0;
	}

	if (!(h = Qopen (options.output_file, "rb")))
		Sys_Error ("%s: %s\n", options.output_file, strerror(errno));

	debug.version = LittleLong (PROG_DEBUG_VERSION);
	CRC_Init (&debug.crc);
	while ((i = Qgetc (h)) != EOF)
		CRC_ProcessByte (&debug.crc, i);
	Qclose (h);
	debug.crc = LittleShort (debug.crc);
	debug.you_tell_me_and_we_will_both_know = 0;

	if (!(h = Qopen (debugfile, "wb")))
		Sys_Error ("%s: %s\n", options.output_file, strerror(errno));
	Qwrite (h, &debug, sizeof (debug));

	debug.auxfunctions = LittleLong (Qtell (h));
	debug.num_auxfunctions = LittleLong (pr.num_auxfunctions);
	for (i = 0; i < pr.num_auxfunctions; i++) {
		pr.auxfunctions[i].function = LittleLong (pr.auxfunctions[i].function);
		pr.auxfunctions[i].source_line = LittleLong (pr.auxfunctions[i].source_line);
		pr.auxfunctions[i].line_info = LittleLong (pr.auxfunctions[i].line_info);
		pr.auxfunctions[i].local_defs = LittleLong (pr.auxfunctions[i].local_defs);
		pr.auxfunctions[i].num_locals = LittleLong (pr.auxfunctions[i].num_locals);
	}
	Qwrite (h, pr.auxfunctions,
			pr.num_auxfunctions * sizeof (pr_auxfunction_t));

	debug.linenos = LittleLong (Qtell (h));
	debug.num_linenos = LittleLong (pr.num_linenos);
	for (i = 0; i < pr.num_linenos; i++) {
		pr.linenos[i].fa.addr = LittleLong (pr.linenos[i].fa.addr);
		pr.linenos[i].line = LittleLong (pr.linenos[i].line);
	}
	Qwrite (h, pr.linenos, pr.num_linenos * sizeof (pr_lineno_t));

	debug.locals = LittleLong (Qtell (h));
	debug.num_locals = LittleLong (pr.num_locals);
	for (i = 0; i < pr.num_locals; i++) {
		pr.locals[i].type = LittleShort (pr.locals[i].type);
		pr.locals[i].ofs = LittleShort (pr.locals[i].ofs);
		pr.locals[i].s_name = LittleLong (pr.locals[i].s_name);
	}
	Qwrite (h, pr.locals, pr.num_locals * sizeof (ddef_t));

	Qseek (h, 0, SEEK_SET);
	Qwrite (h, &debug, sizeof (debug));
	Qclose (h);
	return 0;
}


void
begin_compilation (void)
{
	pr.near_data->size = RESERVED_OFS;
	pr.func_tail = &pr.func_head;

	pr.error_count = 0;
}

qboolean
finish_compilation (void)
{
	def_t      *d;
	qboolean    errors = false;
	function_t *f;
	def_t      *def;
	expr_t      e;
	ex_label_t *l;
	dfunction_t *df;

	// check to make sure all functions prototyped have code
	if (options.warnings.undefined_function)
		for (d = pr.scope->head; d; d = d->def_next) {
			if (d->type->type == ev_func && d->global) {
				// function args ok
				if (d->used) {
					if (!d->initialized) {
						warning (0, "function %s was called but not defined\n",
								 d->name);
					}
				}
			}
		}

	for (d = pr.scope->head; d; d = d->def_next) {
		if (d->external && d->refs) {
			errors = true;
			error (0, "undefined global %s", d->name);
		}
	}
	if (errors)
		return !errors;

	if (options.code.debug) {
		e.type = ex_string;
		e.e.string_val = debugfile;
		ReuseConstant (&e, get_def (&type_string, ".debug_file", pr.scope,
					   st_global));
	}

	for (def = pr.scope->head; def; def = def->def_next) {
		if (def->global || def->absolute)
			continue;
		relocate_refs (def->refs, def->ofs);
	}

	pr.functions = calloc (pr.num_functions + 1, sizeof (dfunction_t));
	for (df = pr.functions + 1, f = pr.func_head; f; df++, f = f->next) {
		df->s_name = f->s_name;
		df->s_file = f->s_file;
		df->numparms = function_parms (f, df->parm_size);
		if (f->scope)
			df->locals = f->scope->space->size;
		if (f->builtin) {
			df->first_statement = -f->builtin;
			continue;
		}
		if (!f->code)
			continue;
		df->first_statement = f->code;
		if (f->scope->space->size > num_localdefs) {
			num_localdefs = f->scope->space->size;
			big_function = f->def->name;
		}
		df->parm_start = pr.near_data->size;
		for (def = f->scope->head; def; def = def->def_next) {
			if (def->absolute)
				continue;
			def->ofs += pr.near_data->size;
			relocate_refs (def->refs, def->ofs);
		}
	}
	pr.near_data->size += num_localdefs;

	for (l = pr.labels; l; l = l->next)
		relocate_refs (l->refs, l->ofs);

	return !errors;
}


const char *
strip_path (const char *filename)
{
	const char *p = filename;
	int         i = options.strip_path;

	while (i-- > 0) {
		while (*p && *p != '/')
			p++;
		if (!*p)
			break;
		filename = ++p;
	}
	return filename;
}

static void
setup_sym_file (const char *output_file)
{
	if (options.code.debug) {
		char       *s;

		strcpy (debugfile, output_file);

		s = debugfile + strlen (debugfile);
		while (s-- > debugfile) {
			if (*s == '.')
				break;
			if (*s == '/' || *s == '\\') {
				s = debugfile + strlen (debugfile);
				break;
			}
		}
		strcpy (s, ".sym");
		if (options.verbosity >= 1)
			printf ("debug file: %s\n", debugfile);
	}
}

static int
compile_to_obj (const char *file, const char *obj)
{
	int         err;

	yyin = preprocess_file (file);
	if (!yyin)
		return !options.preprocess_only;

	InitData ();
	clear_frame_macros ();
	clear_classes ();
	clear_defs ();
	clear_immediates ();
	clear_selectors ();
	clear_structs ();
	clear_enums ();
	clear_typedefs ();
	chain_initial_types ();
	begin_compilation ();
	pr.source_file = ReuseString (strip_path (file));
	err = yyparse () || pr.error_count;
	fclose (yyin);
	if (cpp_name && !options.save_temps) {
		if (unlink (tempname->str)) {
			perror ("unlink");
			exit (1);
		}
	}
	if (!err) {
		qfo_t      *qfo;

		class_finish_module ();
		qfo = qfo_from_progs (&pr);
		err = qfo_write (qfo, obj);
		qfo_delete (qfo);
	}
	return err;
}

static int
separate_compile (void)
{
	const char **file;
	const char **temp_files;
	dstring_t  *output_file = dstring_newstr ();
	dstring_t  *extension = dstring_newstr ();
	char       *f;
	int         err = 0;
	int         i;

	if (options.compile && options.output_file && source_files[1]) {
		fprintf (stderr,
				 "%s: cannot use -c and -o together with multiple files\n",
				 this_program);
		return 1;
	}

	for (file = source_files, i = 0; *file; file++)
		i++;
	temp_files = calloc (i + 1, sizeof (const char*));

	for (file = source_files, i = 0; *file; file++) {
		dstring_clearstr (extension);
		dstring_clearstr (output_file);

		dstring_appendstr (output_file, *file);
		f = output_file->str + strlen (output_file->str);
		while (f >= output_file->str && *f != '.' && *f != '/')
			f--;
		if (*f == '.') {
			output_file->size -= strlen (f);
			dstring_appendstr (extension, f);
			*f = 0;
		}
		if (options.compile && options.output_file) {
			dstring_clearstr (output_file);
			dstring_appendstr (output_file, options.output_file);
		} else {
			dstring_appendstr (output_file, ".qfo");
		}
		if (strncmp (*file, "-l", 2)
			&& (!strcmp (extension->str, ".r")
				|| !strcmp (extension->str, ".qc"))) {
			if (options.verbosity >= 2)
				printf ("%s %s\n", *file, output_file->str);
			temp_files[i++] = save_string (output_file->str);
			err = compile_to_obj (*file, output_file->str) || err;

			free ((char *)*file);
			*file = strdup (output_file->str);
		} else {
			if (options.compile)
				fprintf (stderr, "%s: %s: ignoring object file since linking "
						 "not done\n", this_program, *file);
		}
	}
	if (!err && !options.compile) {
		qfo_t      *qfo;
		linker_begin ();
		for (file = source_files; *file; file++) {
			if (strncmp (*file, "-l", 2))
				err = linker_add_object_file (*file);
			else
				err = linker_add_lib (*file + 2);
			if (err)
				return err;
		}
		qfo = linker_finish ();
		if (qfo) {
			if (!options.output_file)
				options.output_file = "progs.dat";
			if (options.partial_link) {
				qfo_write (qfo, options.output_file);
			} else {
				int         crc = 0;

				qfo_to_progs (qfo, &pr);
				setup_sym_file (options.output_file);
				finish_compilation ();

				// write progdefs.h
				if (options.code.progsversion == PROG_ID_VERSION)
					crc = WriteProgdefs ("progdefs.h");

				WriteData (crc);
			}
		} else {
			err = 1;
		}
		if (!options.save_temps)
			for (file = temp_files; *file; file++)
				unlink (*file);
	}
	return err;
}

static int
progs_src_compile (void)
{
	dstring_t  *filename = dstring_newstr ();
	const char *src;
	int         crc = 0;

	if (options.verbosity >= 1 && strcmp (sourcedir, "")) {
		printf ("Source directory: %s\n", sourcedir);
	}
	if (options.verbosity >= 1 && strcmp (progs_src, "progs.src")) {
		printf ("progs.src: %s\n", progs_src);
	}

	if (*sourcedir)
		dsprintf (filename, "%s/%s", sourcedir, progs_src);
	else
		dsprintf (filename, "%s", progs_src);
	LoadFile (filename->str, (void *) &src);

	if (!(src = COM_Parse (src))) {
		fprintf (stderr, "No destination filename.  qfcc --help for info.\n");
		return 1;
	}

	if (!options.output_file)
		options.output_file = strdup (com_token);
	if (options.verbosity >= 1) {
		printf ("output file: %s\n", options.output_file);
	}
	setup_sym_file (options.output_file);

	InitData ();
	chain_initial_types ();

	begin_compilation ();

	// compile all the files
	while ((src = COM_Parse (src))) {
		int         err;

		if (*sourcedir)
			dsprintf (filename, "%s%c%s", sourcedir, PATH_SEPARATOR,
					  com_token);
		else
			dsprintf (filename, "%s", com_token);
		if (options.verbosity >= 2)
			printf ("compiling %s\n", filename->str);

		yyin = preprocess_file (filename->str);
		if (!yyin)
			return !options.preprocess_only;

		pr.source_file = ReuseString (strip_path (filename->str));
		pr.source_line = 1;
		clear_frame_macros ();
		err = yyparse () || pr.error_count;
		fclose (yyin);
		if (cpp_name && (!options.save_temps)) {
			if (unlink (tempname->str)) {
				perror ("unlink");
				exit (1);
			}
		}
		if (err)
			return 1;
	}

	if (options.compile) {
		qfo_t      *qfo = qfo_from_progs (&pr);
		qfo_write (qfo, options.output_file);
		qfo_delete (qfo);
	} else {
		class_finish_module ();
		if (!finish_compilation ()) {
			fprintf (stderr, "compilation errors\n");
			return 1;
		}

		// write progdefs.h
		if (options.code.progsversion == PROG_ID_VERSION)
			crc = WriteProgdefs ("progdefs.h");

		// write data file
		if (WriteData (crc))
			return 1;

		// write files.dat
		if (options.files_dat)
			if (WriteFiles (sourcedir))
				return 1;
	}

	return 0;
}

/*
	main

	The nerve center of our little operation
*/
int
main (int argc, char **argv)
{
	double      start, stop;
	int         res;

	start = Sys_DoubleTime ();

	this_program = argv[0];

	DecodeArgs (argc, argv);

	tempname = dstring_new ();
	parse_cpp_name ();

	opcode_init ();
	init_types ();

	if (source_files) {
		res = separate_compile ();
	} else {
		res = progs_src_compile ();
	}
	if (res)
		return res;
	stop = Sys_DoubleTime ();
	if (options.verbosity >= 0)
		printf ("Compilation time: %0.3g seconds.\n", (stop - start));
	return 0;
}
