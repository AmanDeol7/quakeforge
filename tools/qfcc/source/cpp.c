/*
	cpp.c

	cpp preprocessing support

	Copyright (C) 2002 Bill Currie <bill@taniwha.org>

	Author: Bill Currie <bill@taniwha.org>
	Date: 2002/06/04

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

#ifdef HAVE_SYS_WAIT_H
# include <sys/wait.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_PROCESS_H
# include <process.h>
#endif
#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif
#include <stdlib.h>
#include <stdio.h>

#include "QF/dstring.h"

#include "cpp.h"
#include "options.h"

typedef struct cpp_arg_s {
	struct cpp_arg_s *next;
	const char *arg;
} cpp_arg_t;

cpp_arg_t  *cpp_arg_list;
cpp_arg_t **cpp_arg_tail = &cpp_arg_list;
cpp_arg_t  *cpp_def_list;
cpp_arg_t **cpp_def_tail = &cpp_def_list;
const char **cpp_argv;
const char *cpp_name = CPP_NAME;
static int  cpp_argc = 0;
dstring_t  *tempname;

static void
add_cpp_arg (const char *arg)
{
	cpp_arg_t  *cpp_arg = malloc (sizeof (cpp_arg_t));
	cpp_arg->next = 0;
	cpp_arg->arg = arg;
	*cpp_arg_tail = cpp_arg;
	cpp_arg_tail = &(*cpp_arg_tail)->next;
	cpp_argc++;
}

void
add_cpp_def (const char *arg)
{
	cpp_arg_t  *cpp_def = malloc (sizeof (cpp_arg_t));
	cpp_def->next = 0;
	cpp_def->arg = arg;
	*cpp_def_tail = cpp_def;
	cpp_def_tail = &(*cpp_def_tail)->next;
	cpp_argc++;
}

void
parse_cpp_name (void)
{
	char       *n;

	if (!cpp_name)
		return;
	n = strdup (cpp_name);
	while (*n) {
		while (*n && *n == ' ')
			n++;
		add_cpp_arg (n);
		while (*n && *n != ' ')
			n++;
		if (*n)
			*n++ = 0;
	}
}

static void
build_cpp_args (const char *in_name, const char *out_name)
{
	cpp_arg_t  *cpp_arg;
	cpp_arg_t  *cpp_def;
	const char **arg;

	if (cpp_argv)
		free (cpp_argv);
	cpp_argv = (const char **)malloc ((cpp_argc + 1) * sizeof (char**));
	for (arg = cpp_argv, cpp_arg = cpp_arg_list;
		 cpp_arg;
		 cpp_arg = cpp_arg->next) {
		if (!strcmp (cpp_arg->arg, "%d")) {
			for (cpp_def = cpp_def_list; cpp_def; cpp_def = cpp_def->next)
				*arg++ = cpp_def->arg;
		} else if (!strcmp (cpp_arg->arg, "%i")) {
			*arg++ = in_name;
		} else if (!strcmp (cpp_arg->arg, "%o")) {
			if (!options.preprocess_only) {
				*arg++ = out_name;
			}
		} else {
			if (!options.preprocess_only || strcmp (cpp_arg->arg, "-o") != 0)
				*arg++ = cpp_arg->arg;
		}
	}
	*arg = 0;
	//for (arg = cpp_argv; *arg; arg++)
	//	printf ("%s ", *arg);
	//puts ("");
}

//============================================================================

FILE *
preprocess_file (const char *filename)
{
#ifndef _WIN32
	pid_t       pid;
	int         tempfd = 0;
#endif
	const char *temp1;
	char       *temp2 = strrchr (this_program, PATH_SEPARATOR);

	if (cpp_name) {
		if (options.save_temps) {
			char	*basename = strdup (filename);
			char	*temp;
		
			temp = strrchr (basename, '.');
			if (temp)
				*temp = '\0';	// ignore the rest of the string

			temp = strrchr (basename, '/');
			if (!temp)
				temp = basename;
			else
				temp++;

			if (*sourcedir) {
				dsprintf (tempname, "%s%c%s", sourcedir,
						  PATH_SEPARATOR, temp);
			} else {
				dsprintf (tempname, "%s.p", temp);
			}
			free (basename);
		} else {
			temp1 = getenv ("TMPDIR");
			if ((!temp1) || (!temp1[0])) {
				temp1 = getenv ("TEMP");
				if ((!temp1) || (!temp1[0])) {
					temp1 = "/tmp";
				}
			}

			dsprintf (tempname, "%s%c%sXXXXXX", temp1,
					  PATH_SEPARATOR, temp2 ? temp2 + 1 : this_program);
		}
		build_cpp_args (filename, tempname->str);

#ifdef _WIN32
		if (!options.save_temps && !options.preprocess_only)
			mktemp (tempname->str);

		{
			FILE       *tmp = fopen (tempname->str, "wt");
			if (tmp == NULL) {
				fprintf (stderr, "%s: qfcc was unable to open\n",
						 tempname->str);
				exit(1);
			}
			fclose (tmp);
		}

		{
			int		status;

			if (options.verbosity > 1) {
				const char **a;
				for (a = cpp_argv; *a; a++)
					printf ("%s ", *a);
				puts("");
			}

			status = spawnvp (_P_WAIT, cpp_argv[0], (char **)cpp_argv);

			if (status) {
				fprintf (stderr, "%s: cpp returned error code %d\n",
						filename,
						status);
				exit (1);
			}
		}

		if (options.preprocess_only)
			return 0;
		return fopen (tempname->str, "rt");
#else
		if (!options.save_temps && !options.preprocess_only)
			tempfd = mkstemp (tempname->str);

		if ((pid = fork ()) == -1) {
			perror ("fork");
			exit (1);
		}
		if (!pid) {
			// we're a child, check for abuse
			if (options.verbosity > 1) {
				const char **a;
				for (a = cpp_argv; *a; a++)
					printf ("%s ", *a);
				puts("");
			}
			execvp (cpp_argv[0], (char **)cpp_argv);
			perror (cpp_argv[0]);
			exit (1);
		} else {
			// give parental guidance (or bury it in the back yard)
			int         status;
			pid_t       rc;

//			printf ("pid = %d\n", pid);
			if ((rc = waitpid (0, &status, 0 | WUNTRACED)) != pid) {
				if (rc == -1) {
					perror ("wait");
					exit (1);
				}
				fprintf (stderr, "%s: The wrong child (%ld) died. Don't ask me, I don't know either.\n",
						this_program,
						(long) rc);
				exit (1);
			}
			if (WIFEXITED (status)) {
				if (WEXITSTATUS (status)) {
					fprintf (stderr, "%s: cpp returned error code %d\n",
							filename,
							WEXITSTATUS (status));
					exit (1);
				}
			} else {
				fprintf (stderr, "%s: cpp returned prematurely.\n", filename);
				exit (1);
			}
		}
		if (options.preprocess_only)
			return 0;
		else if (options.save_temps)
			return fopen (tempname->str, "rt");
		else {
			return fdopen (tempfd, "r+t");
		}
#endif
	}
	return fopen (filename, "rt");
}
