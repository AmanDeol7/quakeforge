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

#ifndef __function_h
#define __function_h

#include "QF/pr_comp.h"

typedef struct function_s {
	struct function_s  *next;
	pr_auxfunction_t   *aux;		// debug info;
	int                 builtin;	// if non 0, call an internal function
	int                 code;		// first statement
	int                 function_num;
	string_t            s_file;		// source file with definition
	string_t            s_name;
	int                 file_line;
	struct def_s       *def;
	struct scope_s     *scope;
	struct reloc_s     *refs;
} function_t;

extern function_t *current_func;

typedef struct param_s {
	// the first two fields match the first two fiels of keywordarg_t in
	// method.h
	struct param_s *next;
	const char *selector;
	struct type_s *type;
	const char *name;
} param_t;

struct expr_s;

param_t *new_param (const char *selector, struct type_s *type,
					const char *name);
param_t *_reverse_params (param_t *params, param_t *next);
param_t *reverse_params (param_t *params);
struct type_s *parse_params (struct type_s *type, param_t *params);
void build_scope (function_t *f, struct def_s *func, param_t *params);
function_t *new_function (const char *name);
function_t *build_builtin_function (struct def_s *def, struct expr_s *bi_val);
void build_function (function_t *f);
void finish_function (function_t *f);
void emit_function (function_t *f, struct expr_s *e);
int function_parms (function_t *f, byte *parm_size);

#endif//__function_h
