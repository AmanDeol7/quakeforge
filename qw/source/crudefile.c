/*
	sv_crudefile.c

	(description)

	Copyright (C) 2001  Adam Olsen

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
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_CTYPE_H
# include <ctype.h>
#endif

#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "QF/console.h"
#include "QF/sys.h"
#include "QF/cvar.h"
#include "QF/quakefs.h"
#include "QF/zone.h"

#include "compat.h"
#include "crudefile.h"

int cf_maxsize; // max combined file size (eg quota)
int cf_cursize; // current combined file size

typedef struct cf_file_s {
	QFile *file;
	char *path;
	char *buf;
	int size;
	int writtento;
	char mode; // 'r' for read, 'w' for write
} cf_file_t;

cf_file_t *cf_filep;
cvar_t    *crudefile_quota;
int        cf_filepcount; // elements in array
int        cf_openfiles; // used elements

#define CF_DIR "cf/"
#define CF_MAXFILES 100
#define CF_BUFFERSIZE 256


/*
	CF_ValidDesc

	Returns 1 if the file descriptor is valid.
*/
static int
CF_ValidDesc (int desc)
{
	if (desc >= 0 && desc < cf_filepcount && cf_filep[desc].file)
		return 1;
	return 0;
}

/*
	CF_AlreadyOpen

	Returns 1 if mode == 'r' and the file is already open for
	writing, or if if mode == 'w' and the file's already open at all.
*/
static int
CF_AlreadyOpen (const char * path, char mode)
{
	int i;

	for (i = 0; i < cf_filepcount; i++) {
		if (!cf_filep[i].file)
			continue;
		if (mode == 'r' && cf_filep[i].mode == 'w' &&
			strequal (path, cf_filep[i].path))
			return 1;
		if (mode == 'w' && strequal (path, cf_filep[i].path))
			return 1;
	}
	return 0;
}

/*
	CF_GetFileSize

	Returns the size of the specified file
*/
static int
CF_GetFileSize (const char *path)
{
	struct stat buf;

	if (!stat (path, &buf))
		return 0;

	return buf.st_size;
}

/*
	CF_BuildQuota

	Calculates the currently used space
*/
static void
CF_BuildQuota (void)
{
	char *file, *path;
	struct dirent *i;
	DIR *dir;

	path = Hunk_TempAlloc (strlen (qfs_gamedir_path) + 1 + strlen (CF_DIR) + 256 +
						   1);
	if (!path)
		return;

	strcpy(path, qfs_gamedir_path);
	strcpy(path + strlen(path), "/");
	strcpy(path + strlen(path), CF_DIR);

	dir = opendir (path);
	if (!dir)
		return;

	file = path + strlen(path);

	cf_cursize = 0;

	while ((i = readdir(dir))) {
		strcpy (file, i->d_name);
		cf_cursize += CF_GetFileSize (path);
	}
	closedir (dir);
}

/*
	CF_Init

	Ye ol' Init function :)
*/
void
CF_Init (void)
{
	CF_BuildQuota();
	crudefile_quota = Cvar_Get ("crudefile_quota", "-1", CVAR_ROM, NULL,
								"Maximum space available to the Crude File "
								"system, -1 to totally disable file writing");
	cf_maxsize = crudefile_quota->int_val;
}

/*
	CF_CloseAllFiles

	Closes all open files, printing warnings if developer is on
*/
void
CF_CloseAllFiles ()
{
	int i;

	for (i = 0; i < cf_filepcount; i++)
		if (cf_filep[i].file) {
			Con_DPrintf ("Warning: closing Crude File %d left over from last "
						 "map\n", i);
			CF_Close(i);
		}
}

/*
	CF_Open

	cfopen opens a file, either for reading or writing (not both).
	returns a file descriptor >= 0 on success, < 0 on failure.
	mode is either r or w.
*/
int
CF_Open (const char *path, const char *mode)
{
	char *fullpath, *j;
	int desc, oldsize, i;
	QFile *file;

	if (cf_openfiles >= CF_MAXFILES) {
		return -1;
	}

	// check for paths with ..
	if (strequal(path, "..")
		|| !strncmp(path, "../", 3)
		|| strstr(path, "/../")
		|| (strlen(path) >= 3
			&& strequal(path + strlen(path) - 3, "/.."))) {
		return -1;
	}

	if (!(strequal(mode, "w") || strequal(mode, "r"))) {
		return -1;
	}

	if (mode[0] == 'w' && cf_maxsize < 0) { // can't even delete if quota < 0
		return -1;
	}

	fullpath = malloc(strlen(qfs_gamedir_path) + 1 + strlen(CF_DIR)
					  + strlen(path) + 1);
	if (!fullpath) {
		return -1;
	}

	strcpy(fullpath, qfs_gamedir_path);
	strcpy(fullpath + strlen(fullpath), "/");
	strcpy(fullpath + strlen(fullpath), CF_DIR);
	j = fullpath + strlen(fullpath);
	for (i = 0; path[i]; i++, j++) // strcpy, but force lowercase
		*j = tolower(path[i]);
	*j = '\0';

	if (CF_AlreadyOpen(fullpath, mode[0])) {
		free (fullpath);
		return -1;
	}

	if (mode[0] == 'w')
		oldsize = CF_GetFileSize (fullpath);
	else
		oldsize = 0;

	file = Qopen (fullpath, mode);
	if (file) {
		if (cf_openfiles >= cf_filepcount) {
			cf_filepcount++;
			cf_filep = realloc(cf_filep, sizeof(cf_file_t) * cf_filepcount);
			if (!cf_filep) {
				Sys_Error ("CF_Open: memory allocation error!");
			}
			cf_filep[cf_filepcount - 1].file = 0;
		}

		for (desc = 0; cf_filep[desc].file; desc++)
			;
		cf_filep[desc].path = fullpath;
		cf_filep[desc].file = file;
		cf_filep[desc].buf = 0;
		cf_filep[desc].size = 0;
		cf_filep[desc].writtento = 0;
		cf_filep[desc].mode = mode[0];

		cf_cursize -= oldsize;
		cf_openfiles++;

		return desc;
	}
	return -1;
}

/*
	CF_Close

	cfclose closes a file descriptor.  returns nothing.  to prevent
	leakage, all open files are closed on map load, and warnings are
	printed if developer is set to 1.
*/
void
CF_Close (int desc)
{
	char *path;

	if (!CF_ValidDesc(desc))
		return;

	if (cf_filep[desc].mode == 'w' && !cf_filep[desc].writtento)
		unlink(cf_filep[desc].path);

	path = cf_filep[desc].path;

	Qclose (cf_filep[desc].file);
	cf_filep[desc].file = 0;
	free(cf_filep[desc].buf);
	cf_openfiles--;

	cf_cursize -= CF_GetFileSize (path);
	free(path);
}

/*
	CF_Read

	cfread returns a single string read in from the file.  an empty
	string either means an empty string or eof, use cfeof to check.
*/
const char *
CF_Read (int desc)
{
	int len = 0;

	if (!CF_ValidDesc(desc) || cf_filep[desc].mode != 'r') {
		return "";
	}

	do {
		int foo;
		if (cf_filep[desc].size <= len) {
			char *t = realloc (cf_filep[desc].buf, cf_filep[desc].size +
							   CF_BUFFERSIZE);
			if (!t) {
				Sys_Error ("CF_Read: memory allocation error!");
			}
			cf_filep[desc].buf = t;
			cf_filep[desc].size += CF_BUFFERSIZE;
		}
		foo = Qgetc(cf_filep[desc].file);
		if (foo == EOF)
			foo = 0;
		cf_filep[desc].buf[len] = (char) foo;
		len++;
	} while (cf_filep[desc].buf[len - 1]);

	return cf_filep[desc].buf;
}

/*
	CF_Write

	cfwrite writes a string to the file, including a trailing nul,
	returning the number of characters written.  It returns 0 if
	there was an error in writing, such as if the quota would have
	been exceeded.
*/
int
CF_Write (int desc, const char *buf) // should be const char *, but Qwrite isn't...
{
	int len;

	if (!CF_ValidDesc(desc) || cf_filep[desc].mode != 'w' || cf_cursize >=
		cf_maxsize) {
		return 0;
	}

	len = strlen(buf) + 1;
	if (len > cf_maxsize - cf_cursize) {
		return 0;
	}

	len = Qwrite(cf_filep[desc].file, buf, len);
	if (len < 0)
		len = 0;

	cf_cursize += len;
	cf_filep[desc].writtento = 1;

	return len;
}

/*
	CF_EOF

	cfeof returns 1 if you're at the end of the file, 0 if not, and
	-1 on a bad descriptor.
*/
int
CF_EOF (int desc)
{
	if (!CF_ValidDesc(desc)) {
		return -1;
	}

	return Qeof (cf_filep[desc].file);
}

/*
	CF_Quota

	cfquota returns the number of characters left in the quota, or
	<= 0 if it's met or (somehow) exceeded.
*/
int
CF_Quota()
{
	return cf_maxsize - cf_cursize;
}
