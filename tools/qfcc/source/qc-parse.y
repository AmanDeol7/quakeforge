%{
/*
	qc-parse.y

	parser for quakec

	Copyright (C) 2001 Bill Currie <bill@taniwha.org>

	Author: Bill Currie <bill@taniwha.org>
	Date: 2001/06/12

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

#include <QF/hash.h>
#include <QF/sys.h>

#include "class.h"
#include "debug.h"
#include "def.h"
#include "emit.h"
#include "expr.h"
#include "function.h"
#include "immediate.h"
#include "method.h"
#include "options.h"
#include "qfcc.h"
#include "reloc.h"
#include "struct.h"
#include "switch.h"
#include "type.h"

#define YYDEBUG 1
#define YYERROR_VERBOSE 1
#undef YYERROR_VERBOSE

extern char *yytext;

static void
yyerror (const char *s)
{
#ifdef YYERROR_VERBOSE
	error (0, "%s %s\n", yytext, s);
#else
	error (0, "%s before %s", s, yytext);
#endif
}

static void
parse_error (void)
{
	error (0, "parse error before %s", yytext);
}

#define PARSE_ERROR do { parse_error (); YYERROR; } while (0)

int yylex (void);

hashtab_t *save_local_inits (scope_t *scope);
hashtab_t *merge_local_inits (hashtab_t *dl_1, hashtab_t *dl_2);
void restore_local_inits (hashtab_t *def_list);
void free_local_inits (hashtab_t *def_list);

expr_t *argc_expr (void);
expr_t *argv_expr (void);

%}

%union {
	int			op;
	struct def_s *def;
	struct hashtab_s	*def_list;
	type_t		*type;
	expr_t		*expr;
	int			integer_val;
	float		float_val;
	const char *string_val;
	float		vector_val[3];
	float		quaternion_val[4];
	struct function_s *function;
	struct switch_block_s	*switch_block;
	struct param_s	*param;
	struct method_s	*method;
	struct class_s	*class;
	struct category_s *category;
	struct class_type_s	*class_type;
	struct protocol_s *protocol;
	struct keywordarg_s *keywordarg;
	struct methodlist_s *methodlist;
	struct struct_s *strct;
}

%right	<op> '=' ASX PAS /* pointer assign */
%right	'?' ':'
%left	OR AND
%left	EQ NE LE GE LT GT
%left	SHL SHR %left	'+' '-'
%left	'*' '/' '&' '|' '^' '%'
%right	<op> UNARY INCOP
%left	HYPERUNARY
%left	'.' '(' '['

%token	<string_val> CLASS_NAME NAME STRING_VAL
%token	<integer_val> INT_VAL
%token	<float_val> FLOAT_VAL
%token	<vector_val> VECTOR_VAL
%token	<quaternion_val> QUATERNION_VAL

%token	LOCAL RETURN WHILE DO IF ELSE FOR BREAK CONTINUE ELLIPSIS NIL
%token	IFBE IFB IFAE IFA
%token	SWITCH CASE DEFAULT STRUCT UNION ENUM TYPEDEF SUPER SELF THIS
%token	ARGS ARGC ARGV EXTERN STATIC SYSTEM SIZEOF
%token	ELE_START
%token	<type> TYPE
%token	CLASS DEFS ENCODE END IMPLEMENTATION INTERFACE PRIVATE PROTECTED
%token	PROTOCOL PUBLIC SELECTOR

%type	<type>	type non_field_type type_name def simple_def struct_def
%type	<type>	struct_def_item ivar_decl ivar_declarator def_item def_list
%type	<type>	struct_def_list ivars
%type	<param> function_decl
%type	<param>	param param_list
%type	<def>	def_name opt_initializer methoddef var_initializer
%type	<expr>	const opt_expr expr element_list element_list1 element
%type	<expr>	string_val opt_state_expr array_decl
%type	<expr>	statement statements statement_block
%type	<expr>	break_label continue_label enum_list enum
%type	<expr>	unary_expr primary cast_expr opt_arg_list arg_list
%type	<function> begin_function
%type	<def_list> save_inits
%type	<switch_block> switch_block

%type	<string_val> selector reserved_word maybe_class
%type	<param>	optparmlist unaryselector keyworddecl keywordselector
%type	<method> methodproto methoddecl
%type	<expr>	obj_expr identifier_list obj_messageexpr obj_string receiver
%type	<expr>	protocolrefs protocol_list
%type	<keywordarg> messageargs keywordarg keywordarglist selectorarg
%type	<keywordarg> keywordnamelist keywordname
%type	<class> class_name new_class_name class_with_super new_class_with_super
%type	<class> classdef
%type	<category> category_name new_category_name
%type	<protocol> protocol_name
%type	<methodlist> methodprotolist methodprotolist2
%type	<strct>	ivar_decl_list

%expect 4

%{

function_t *current_func;
param_t *current_params;
expr_t	*current_init;
class_type_t *current_class;
expr_t	*local_expr;
expr_t	*break_label;
expr_t	*continue_label;
switch_block_t *switch_block;
struct_t *current_struct;
visibility_type current_visibility;
struct_t *current_ivars;
scope_t *current_scope;
storage_class_t current_storage;

int      element_flag;

%}

%%

defs
	: /* empty */
	| defs {if (current_class) PARSE_ERROR;} def ';'
	| defs obj_def
	;

def
	: type { current_storage = st_global; $$ = $1; } def_list { }
	| storage_class type { $$ = $2; } def_list { }
	| storage_class '{' simple_defs '}' { }
	| STRUCT NAME
	  { current_struct = new_struct ($2); } '=' '{' struct_defs '}' { }
	| UNION NAME
	  { current_struct = new_union ($2); } '=' '{' struct_defs '}' { }
	| STRUCT NAME			{ decl_struct ($2); }
	| UNION NAME			{ decl_union ($2); }
	| ENUM '{' enum_list opt_comma '}'
	  { process_enum ($3); }
	| TYPEDEF type NAME
	  { new_typedef ($3, $2); }
	| TYPEDEF ENUM '{' enum_list opt_comma '}' NAME
		{
			process_enum ($4);
			new_typedef ($7, &type_integer);
		}
	;

simple_defs
	: /* empty */
	| simple_defs simple_def ';'
	;

simple_def
	: type { $$ = $1; } def_list { }
	;

storage_class
	: EXTERN		{ current_storage = st_extern; }
	| STATIC		{ current_storage = st_static; }
	| SYSTEM		{ current_storage = st_system; }
	;

struct_defs
	: /* empty */
	| struct_defs struct_def ';'
	| DEFS '(' NAME ')'
		{
			class_t    *class = get_class ($3, 0);

			if (class) {
				copy_struct_fields (current_struct, class->ivars);
			} else {
				error (0, "undefined symbol `%s'", $3);
			}
		}
	;

struct_def
	: type { $$ = $1; } struct_def_list { }
	;

enum_list
	: enum
	| enum_list ',' enum
		{
			if ($3) {
				$3->next = $1;
				$$ = $3;
			} else {
				$$ = $1;
			}
		}
	;

enum
	: NAME { $$ = new_name_expr ($1); }
	| NAME '=' expr
		{
			$$ = 0;
			if ($3->type == ex_def && $3->e.def->constant)
				$3 = constant_expr ($3);
			if ($3->type < ex_string) {
				error ($3, "non-constant initializer");
			} else if ($3->type != ex_integer) {
				error ($3, "invalid initializer type");
			} else {
				$$ = new_binary_expr ('=', new_name_expr ($1), $3);
			}
		}
	;

type
	: '.' type { $$ = field_type ($2); }
	| non_field_type { $$ = $1; }
	| non_field_type function_decl
		{
			current_params = $2;
			$$ = parse_params ($1, $2);
		}
	;

non_field_type
	: '(' type ')' { $$ = $2; }
	| non_field_type array_decl
		{
			if ($2)
				$$ = array_type ($1, $2->e.integer_val);
			else
				$$ = pointer_type ($1);
		}
	| type_name { $$ = $1; }
	;

type_name
	: TYPE { $$ = $1; }
	| CLASS_NAME
		{
			class_t    *class = get_class ($1, 0);
			if (!class) {
				error (0, "undefined symbol `%s'", $1);
				class = get_class (0, 1);
			}
			$$ = class->type;
		}
	| STRUCT NAME			{ $$ = decl_struct ($2)->type; }
	| UNION NAME			{ $$ = decl_union ($2)->type; }
	;

function_decl
	: '(' param_list ')'	{ $$ = reverse_params ($2); }
	| '(' param_list ',' ELLIPSIS ')'
		{
			$$ = new_param (0, 0, 0);
			$$->next = $2;
			$$ = reverse_params ($$);
		}
	| '(' ELLIPSIS ')'		{ $$ = new_param (0, 0, 0); }
	| '(' TYPE ')'
		{
			if ($2 != &type_void)
				PARSE_ERROR;
			$$ = 0;
		}
	| '(' ')'				{ $$ = 0; }
	;

param_list
	: param
	| param_list ',' param
		{
			$3->next = $1;
			$$ = $3;
		}
	;

param
	: type NAME { $$ = new_param (0, $1, $2); }
	;

array_decl
	: '[' const ']'
		{
			if ($2->type != ex_integer || $2->e.integer_val < 1) {
				error (0, "invalid array size");
				$$ = 0;
			} else {
				$$ = $2;
			}
		}
	| '[' ']' { $$ = 0; }
	;

struct_def_list
	: struct_def_list ',' { $$ = $<type>0; } struct_def_item
	| struct_def_item
	;

struct_def_item
	: NAME { new_struct_field (current_struct, $<type>0, $1, vis_public); }
	;

def_list
	: def_list ',' { $$ = $<type>0; } def_item
	| def_item
	;

def_item
	: def_name opt_initializer
		{
			if ($1 && !$1->local
				&& $1->type->type != ev_func)
				def_initialized ($1);
		}
	;

def_name
	: NAME
		{
			if (current_scope->type == sc_local
				&& current_scope->parent->type == sc_params) {
				def_t      *def = get_def (0, $1, current_scope, st_none);
				if (def) {
					if (def->scope->type == sc_params)
						warning (0, "local %s shadows param %s", $1, def->name);
				}
			}
			$$ = get_def ($<type>0, $1, current_scope, current_storage);
		}
	;

opt_initializer
	: /*empty*/ { }
	| { element_flag = $<def>0->type->type != ev_func; $$ = $<def>0; }
	  var_initializer { element_flag = 0; }
	;

var_initializer
	: '=' expr
		{
			if (current_scope->type == sc_local) {
				append_expr (local_expr,
							 assign_expr (new_def_expr ($<def>0), $2));
				def_initialized ($<def>0);
			} else {
				if ($2->type == ex_def && $2->e.def->constant)
					$2 = constant_expr ($2);
				if ($2->type >= ex_string) {
					if ($<def>0->constant) {
						error ($2, "%s re-initialized", $<def>0->name);
					} else {
						if ($<def>0->type->type == ev_func) {
							PARSE_ERROR;
						} else {
							ReuseConstant ($2,  $<def>0);
						}
					}
				} else {
					error ($2, "non-constant expression used for initializer");
				}
			}
		}
	| '=' ELE_START { current_init = new_block_expr (); } element_list '}'
		{
			init_elements ($<def>0, $4);
			current_init = 0;
		}
	| '=' '#' const
		{
			build_builtin_function ($<def>0, $3);
		}
	| '=' opt_state_expr { $$ = $<def>0; }
	  begin_function statement_block end_function
		{
			build_function ($4);
			if ($2) {
				$2->next = $5;
				emit_function ($4, $2);
			} else {
				emit_function ($4, $5);
			}
			finish_function ($4);
		}
	;

opt_state_expr
	: /* emtpy */
		{
			$$ = 0;
		}
	| '[' const ',' { $<type>$ = &type_function; } def_name ']'
		{
			if ($2->type == ex_integer)
				convert_int ($2);
			else if ($2->type == ex_uinteger)
				convert_uint ($2);
			if ($2->type != ex_float)
				error ($2, "invalid type for frame number");
			if ($5->type->type != ev_func)
				error ($2, "invalid type for think");

			$$ = new_binary_expr ('s', $2, new_def_expr ($5));
		}
	;

element_list
	: /* empty */
		{
			$$ = new_block_expr ();
		}
	| element_list1 opt_comma
		{
			$$ = current_init;
		}
	;

element_list1
	: element
		{
			append_expr (current_init, $1);
		}
	| element_list1 ',' element
		{
			append_expr (current_init, $3);
		}
	;

element
	: '{'
		{
			$$ = current_init;
			current_init = new_block_expr ();
		}
	  element_list
		{
			current_init = $<expr>2;
		}
	  '}'
		{
			$$ = $3;
		}
	| expr
		{
			$$ = $1;
		}
	;

opt_comma
	: /* empty */
	| ','
	;

begin_function
	: /*empty*/
		{
			$$ = current_func = new_function ($<def>0->name);
			$$->def = $<def>0;
			$$->refs = new_reloc ($$->def->ofs, rel_def_func);
			$$->code = pr.code->size;
			if (options.code.debug) {
				pr_lineno_t *lineno = new_lineno ();
				$$->aux->source_line = $$->def->line;
				$$->aux->line_info = lineno - pr.linenos;
				$$->aux->local_defs = pr.num_locals;

				lineno->fa.func = $$->aux - pr.auxfunctions;
			}
			build_scope ($$, $<def>0, current_params);
			current_scope = $$->scope;
		}
	;

end_function
	: /*empty*/
		{
			current_scope = current_scope->parent;
		}
	;

statement_block
	: '{' 
		{
			scope_t    *scope = new_scope (sc_local, current_scope->space,
										   current_scope);
			current_scope = scope;
		}
	  statements '}'
		{
			def_t      *defs = current_scope->head;
			int         num_defs = current_scope->num_defs;

			flush_scope (current_scope, 1);

			current_scope = current_scope->parent;
			current_scope->num_defs += num_defs;
			*current_scope->tail = defs;
			while (*current_scope->tail) {
				current_scope->tail = &(*current_scope->tail)->def_next;
			}
			$$ = $3;
		}
	;

statements
	: /*empty*/
		{
			$$ = new_block_expr ();
		}
	| statements statement
		{
			$$ = append_expr ($1, $2);
		}
	;

statement
	: ';' { $$ = 0; }
	| statement_block { $$ = $1; }
	| RETURN expr ';'
		{
			$$ = return_expr (current_func, $2);
		}
	| RETURN ';'
		{
			$$ = return_expr (current_func, 0);
		}
	| BREAK ';'
		{
			$$ = 0;
			if (break_label)
				$$ = new_unary_expr ('g', break_label);
			else
				error (0, "break outside of loop or switch");
		}
	| CONTINUE ';'
		{
			$$ = 0;
			if (continue_label)
				$$ = new_unary_expr ('g', continue_label);
			else
				error (0, "continue outside of loop");
		}
	| CASE expr ':'
		{
			$$ = case_label_expr (switch_block, $2);
		}
	| DEFAULT ':'
		{
			$$ = case_label_expr (switch_block, 0);
		}
	| SWITCH break_label switch_block '(' expr ')'
		{
			switch_block->test = $5;
		}
	  save_inits statement_block
		{
			restore_local_inits ($8);
			free_local_inits ($8);
			$$ = switch_expr (switch_block, break_label, $9);
			switch_block = $3;
			break_label = $2;
		}
	| WHILE break_label continue_label '(' expr ')' save_inits statement
		{
			expr_t *l1 = new_label_expr ();
			expr_t *l2 = break_label;
			expr_t *e;

			restore_local_inits ($7);
			free_local_inits ($7);

			$$ = new_block_expr ();

			e = new_binary_expr ('n', test_expr ($5, 1), l2);
			e->line = $5->line;
			e->file = $5->file;
			append_expr ($$, e);
			append_expr ($$, l1);
			append_expr ($$, $8);
			append_expr ($$, continue_label);
			e = new_binary_expr ('i', test_expr ($5, 1), l1);
			e->line = $5->line;
			e->file = $5->file;
			append_expr ($$, e);
			append_expr ($$, l2);
			break_label = $2;
			continue_label = $3;
		}
	| DO break_label continue_label statement WHILE '(' expr ')' ';'
		{
			expr_t *l1 = new_label_expr ();

			$$ = new_block_expr ();

			append_expr ($$, l1);
			append_expr ($$, $4);
			append_expr ($$, continue_label);
			append_expr ($$, new_binary_expr ('i', test_expr ($7, 1), l1));
			append_expr ($$, break_label);
			break_label = $2;
			continue_label = $3;
		}
	| LOCAL type
		{
			current_storage = st_local;
			$<type>$ = $2;
			local_expr = new_block_expr ();
		}
	  def_list ';'
		{
			$$ = local_expr;
			local_expr = 0;
		}
	| IF '(' expr ')' save_inits statement
		{
			expr_t *l1 = new_label_expr ();
			expr_t *e;

			$$ = new_block_expr ();

			restore_local_inits ($5);
			free_local_inits ($5);

			e = new_binary_expr ('n', test_expr ($3, 1), l1);
			e->line = $3->line;
			e->file = $3->file;
			append_expr ($$, e);
			append_expr ($$, $6);
			append_expr ($$, l1);
		}
	| IF '(' expr ')' save_inits statement ELSE
		{
			$<def_list>$ = save_local_inits (current_scope);
			restore_local_inits ($5);
		}
	  statement
		{
			expr_t     *l1 = new_label_expr ();
			expr_t     *l2 = new_label_expr ();
			expr_t     *e;
			hashtab_t  *merged;
			hashtab_t  *else_ini;

			$$ = new_block_expr ();

			else_ini = save_local_inits (current_scope);

			restore_local_inits ($5);
			free_local_inits ($5);

			e = new_binary_expr ('n', test_expr ($3, 1), l1);
			e->line = $3->line;
			e->file = $3->file;
			append_expr ($$, e);

			append_expr ($$, $6);

			e = new_unary_expr ('g', l2);
			append_expr ($$, e);

			append_expr ($$, l1);
			append_expr ($$, $9);
			append_expr ($$, l2);
			merged = merge_local_inits ($<def_list>8, else_ini);
			restore_local_inits (merged);
			free_local_inits (merged);
			free_local_inits (else_ini);
			free_local_inits ($<def_list>8);
		}
	| FOR break_label continue_label
			'(' opt_expr ';' opt_expr ';' opt_expr ')' save_inits statement
		{
			expr_t *l1 = new_label_expr ();
			expr_t *l2 = break_label;

			restore_local_inits ($11);
			free_local_inits ($11);

			$$ = new_block_expr ();

			append_expr ($$, $5);
			if ($7)
				append_expr ($$, new_binary_expr ('n', test_expr ($7, 1), l2));
			append_expr ($$, l1);
			append_expr ($$, $12);
			append_expr ($$, continue_label);
			append_expr ($$, $9);
			if ($5)
				append_expr ($$, new_binary_expr ('i', test_expr ($7, 1), l1));
			append_expr ($$, l2);
			break_label = $2;
			continue_label = $3;
		}
	| expr ';'
		{
			$$ = $1;
		}
	;

break_label
	: /* empty */
		{
			$$ = break_label;
			break_label = new_label_expr ();
		}
	;

continue_label
	: /* empty */
		{
			$$ = continue_label;
			continue_label = new_label_expr ();
		}
	;

switch_block
	: /* empty */
		{
			$$ = switch_block;
			switch_block = new_switch_block ();
		}
	;

save_inits
	: /* empty */
		{
			$$ = save_local_inits (current_scope);
		}
	;

opt_expr
	: expr
	| /* empty */
		{
			$$ = 0;
		}
	;

unary_expr
	: primary
	| '+' cast_expr %prec UNARY	{ $$ = $2; }
	| '-' cast_expr %prec UNARY	{ $$ = unary_expr ('-', $2); }
	| '!' cast_expr %prec UNARY	{ $$ = unary_expr ('!', $2); }
	| '~' cast_expr %prec UNARY	{ $$ = unary_expr ('~', $2); }
	| '&' cast_expr %prec UNARY	{ $$ = address_expr ($2, 0, 0); }
	| SIZEOF unary_expr	%prec UNARY 		{ $$ = sizeof_expr ($2, 0); }
	| SIZEOF '(' type ')'	 %prec HYPERUNARY	{ $$ = sizeof_expr (0, $3); }
	;

primary
	: NAME						{ $$ = new_name_expr ($1); }
	| ARGS						{ $$ = new_name_expr (".args"); }
	| ARGC						{ $$ = argc_expr (); }
	| ARGV						{ $$ = argv_expr (); }
	| SELF						{ $$ = new_self_expr (); }
	| THIS						{ $$ = new_this_expr (); }
	| const						{ $$ = $1; }
	| '(' expr ')'				{ $$ = $2; $$->paren = 1; }
	| primary '(' opt_arg_list ')' { $$ = function_expr ($1, $3); }
	| primary '[' expr ']'		{ $$ = array_expr ($1, $3); }
	| primary '.' primary		{ $$ = binary_expr ('.', $1, $3); }
	| INCOP primary				{ $$ = incop_expr ($1, $2, 0); }
	| primary INCOP				{ $$ = incop_expr ($2, $1, 1); }
	| obj_expr					{ $$ = $1; }
	;

cast_expr
	: unary_expr
	| '(' type ')' cast_expr %prec UNARY	{ $$ = cast_expr ($2, $4); }
	;

expr
	: cast_expr
	| expr '=' expr				{ $$ = assign_expr ($1, $3); }
	| expr ASX expr				{ $$ = asx_expr ($2, $1, $3); }
	| expr '?' expr ':' expr 	{ $$ = conditional_expr ($1, $3, $5); }
	| expr AND expr				{ $$ = binary_expr (AND, $1, $3); }
	| expr OR expr				{ $$ = binary_expr (OR,  $1, $3); }
	| expr EQ expr				{ $$ = binary_expr (EQ,  $1, $3); }
	| expr NE expr				{ $$ = binary_expr (NE,  $1, $3); }
	| expr LE expr				{ $$ = binary_expr (LE,  $1, $3); }
	| expr GE expr				{ $$ = binary_expr (GE,  $1, $3); }
	| expr LT expr				{ $$ = binary_expr (LT,  $1, $3); }
	| expr GT expr				{ $$ = binary_expr (GT,  $1, $3); }
	| expr SHL expr				{ $$ = binary_expr (SHL, $1, $3); }
	| expr SHR expr				{ $$ = binary_expr (SHR, $1, $3); }
	| expr '+' expr				{ $$ = binary_expr ('+', $1, $3); }
	| expr '-' expr				{ $$ = binary_expr ('-', $1, $3); }
	| expr '*' expr				{ $$ = binary_expr ('*', $1, $3); }
	| expr '/' expr				{ $$ = binary_expr ('/', $1, $3); }
	| expr '&' expr				{ $$ = binary_expr ('&', $1, $3); }
	| expr '|' expr				{ $$ = binary_expr ('|', $1, $3); }
	| expr '^' expr				{ $$ = binary_expr ('^', $1, $3); }
	| expr '%' expr				{ $$ = binary_expr ('%', $1, $3); }
	;

opt_arg_list
	: /* emtpy */				{ $$ = 0; }
	| arg_list					{ $$ = $1; }
	;

arg_list
	: expr
	| arg_list ',' expr
		{
			$3->next = $1;
			$$ = $3;
		}
	;

const
	: FLOAT_VAL					{ $$ = new_float_expr ($1); }
	| string_val				{ $$ = $1; }
	| VECTOR_VAL				{ $$ = new_vector_expr ($1); }
	| QUATERNION_VAL			{ $$ = new_quaternion_expr ($1); }
	| INT_VAL					{ $$ = new_integer_expr ($1); }
	| NIL						{ $$ = new_nil_expr (); }
	;

string_val
	: STRING_VAL				{ $$ = new_string_expr ($1); }
	| string_val STRING_VAL
		{
			$$ = binary_expr ('+', $1, new_string_expr ($2));
		}
	;

obj_def
	: classdef					{ }
	| classdecl
	| protocoldef
	| { if (!current_class) PARSE_ERROR; } methoddef
	| END
		{
			if (!current_class)
				PARSE_ERROR;
			else
				class_finish (current_class);
			current_class = 0;
		}
	;

identifier_list
	: NAME
		{
			$$ = new_block_expr ();
			append_expr ($$, new_name_expr ($1));
		}
	| identifier_list ',' NAME
		{
			append_expr ($1, new_name_expr ($3));
			$$ = $1;
		}
	;

classdecl
	: CLASS identifier_list ';'
		{
			expr_t     *e;
			for (e = $2->e.block.head; e; e = e->next)
				get_class (e->e.string_val, 1);
		}
	;

maybe_class
	: NAME
	| CLASS_NAME
	;

class_name
	: maybe_class
		{
			$$ = get_class ($1, 0);
			if (!$$) {
				error (0, "undefined symbol `%s'", $1);
				$$ = get_class (0, 1);
			}
		}
	;

new_class_name
	: maybe_class
		{
			$$ = get_class ($1, 1);
			if ($$->defined) {
				error (0, "redefinition of `%s'", $1);
				$$ = get_class (0, 1);
			}
		}
	;

class_with_super
	: class_name ':' class_name
		{
			if ($1->super_class != $3)
				error (0, "%s is not a super class of %s", $3->name, $1->name);
			$$ = $1;
		}
	;

new_class_with_super
	: new_class_name ':' class_name
		{
			$1->super_class = $3;
			$$ = $1;
		}
	;

category_name
	: maybe_class '(' maybe_class ')'
		{
			$$ = get_category ($1, $3, 0);
			if (!$$) {
				error (0, "undefined category `%s (%s)'", $1, $3);
				$$ = get_category (0, 0, 1);
			}
		}
	;

new_category_name
	: maybe_class '(' maybe_class ')'
		{
			$$ = get_category ($1, $3, 1);
			if ($$->defined) {
				error (0, "redefinition of category `%s (%s)'", $1, $3);
				$$ = get_category (0, 0, 1);
			}
		}
	;

protocol_name
	: maybe_class
		{
			$$ = get_protocol ($1, 0);
			if ($$) {
				error (0, "redefinition of %s", $1);
				$$ = get_protocol (0, 1);
			} else {
				$$ = get_protocol ($1, 1);
			}
		}
	;

classdef
	: INTERFACE new_class_name
	  protocolrefs						{ class_add_protocol_methods ($2, $3);}
	  '{'								{ $$ = $2; }
	  ivar_decl_list '}'				{ class_add_ivars ($2, $7); $$ = $2; }
	  methodprotolist					{ class_add_methods ($2, $10); }
	  END								{ current_class = 0; }
	| INTERFACE new_class_name
	  protocolrefs					{ class_add_protocol_methods ($2, $3); }
					{ class_add_ivars ($2, class_new_ivars ($2)); $$ = $2; }
	  methodprotolist					{ class_add_methods ($2, $6); }
	  END								{ current_class = 0; }
	| INTERFACE new_class_with_super
	  protocolrefs						{ class_add_protocol_methods ($2, $3);}
	  '{'								{ $$ = $2; }
	  ivar_decl_list '}'				{ class_add_ivars ($2, $7); $$ = $2; }
	  methodprotolist					{ class_add_methods ($2, $10); }
	  END								{ current_class = 0; }
	| INTERFACE new_class_with_super
	  protocolrefs					{ class_add_protocol_methods ($2, $3); }
					{ class_add_ivars ($2, class_new_ivars ($2)); $$ = $2; }
	  methodprotolist					{ class_add_methods ($2, $6); }
	  END								{ current_class = 0; }
	| INTERFACE new_category_name
	  protocolrefs	{ category_add_protocol_methods ($2, $3); $$ = $2->class;}
	  methodprotolist					{ category_add_methods ($2, $5); }
	  END								{ current_class = 0; }
	| IMPLEMENTATION class_name			{ class_begin (&$2->class_type); }
	  '{'								{ $$ = $2; }
	  ivar_decl_list '}'				{ class_check_ivars ($2, $6); }
	| IMPLEMENTATION class_name			{ class_begin (&$2->class_type); }
	| IMPLEMENTATION class_with_super	{ class_begin (&$2->class_type); }
	  '{'								{ $$ = $2; }
	  ivar_decl_list '}'				{ class_check_ivars ($2, $6); }
	| IMPLEMENTATION class_with_super	{ class_begin (&$2->class_type); }
	| IMPLEMENTATION category_name		{ class_begin (&$2->class_type); }
	;

protocoldef
	: PROTOCOL protocol_name 
	  protocolrefs	{ protocol_add_protocol_methods ($2, $3); $<class>$ = 0; }
	  methodprotolist			{ protocol_add_methods ($2, $5); }
	  END
	;

protocolrefs
	: /* emtpy */				{ $$ = 0; }
	| LT protocol_list GT		{ $$ = $2->e.block.head; }
	;

protocol_list
	: maybe_class
		{
			$$ = new_block_expr ();
			append_expr ($$, new_name_expr ($1));
		}
	| protocol_list ',' maybe_class
		{
			append_expr ($1, new_name_expr ($3));
			$$ = $1;
		}
	;

ivar_decl_list
	:	/* */
		{
			current_visibility = vis_protected;
			current_ivars = class_new_ivars ($<class>0);
		}
	  ivar_decl_list_2
		{
			$$ = current_ivars;
			current_ivars = 0;
		}
	;

ivar_decl_list_2
	: ivar_decl_list_2 visibility_spec ivar_decls
	| ivar_decls
	;

visibility_spec
	: PRIVATE					{ current_visibility = vis_private; }
	| PROTECTED					{ current_visibility = vis_protected; }
	| PUBLIC					{ current_visibility = vis_public; }
	;

ivar_decls
	: /* empty */
	| ivar_decls ivar_decl ';'
	;

ivar_decl
	: type { $$ = $1; } ivars { }
	;

ivars
	: ivar_declarator
	| ivars ',' { $$ = $<type>0; } ivar_declarator
	;

ivar_declarator
	: NAME
		{
			new_struct_field (current_ivars, $<type>0, $1, current_visibility);
		}
	;

methoddef
	: '+' methoddecl
		{
			$2->instance = 0;
			$2 = class_find_method (current_class, $2);
		}
	  opt_state_expr
		{
			$$ = $2->def = method_def (current_class, $2);
			current_params = $2->params;
		}
	  begin_function statement_block end_function
		{
			$2->func = $6;
			build_function ($6);
			if ($4) {
				$4->next = $7;
				emit_function ($6, $4);
			} else {
				emit_function ($6, $7);
			}
			finish_function ($6);
		}
	| '+' methoddecl '=' '#' const ';'
		{
			$2->instance = 0;
			$2 = class_find_method (current_class, $2);
			$2->def = method_def (current_class, $2);

			$2->func = build_builtin_function ($2->def, $5);
		}
	| '-' methoddecl
		{
			$2->instance = 1;
			$2 = class_find_method (current_class, $2);
		}
	  opt_state_expr
		{
			$$ = $2->def = method_def (current_class, $2);
			current_params = $2->params;
		}
	  begin_function statement_block end_function
		{
			$2->func = $6;
			build_function ($6);
			if ($4) {
				$4->next = $7;
				emit_function ($6, $4);
			} else {
				emit_function ($6, $7);
			}
			finish_function ($6);
		}
	| '-' methoddecl '=' '#' const ';'
		{
			$2->instance = 1;
			$2 = class_find_method (current_class, $2);
			$2->def = method_def (current_class, $2);

			$2->func = build_builtin_function ($2->def, $5);
		}
	;

methodprotolist
	: /* emtpy */				{ $$ = 0; }
	| methodprotolist2
	;

methodprotolist2
	: { } methodproto
		{
			$$ = new_methodlist ();
			add_method ($$, $2);
		}
	| methodprotolist2 methodproto
		{
			add_method ($1, $2);
			$$ = $1;
		}
	;

methodproto
	: '+' methoddecl ';'
		{
			$2->instance = 0;
			$2->params->type = &type_Class;
			$$ = $2;
		}
	| '-' methoddecl ';'
		{
			$2->instance = 1;
			if ($<class>-1)
				$2->params->type = $<class>-1->type;
			$$ = $2;
		}
	;

methoddecl
	: '(' type ')' unaryselector
		{ $$ = new_method ($2, $4, 0); }
	| unaryselector
		{ $$ = new_method (&type_id, $1, 0); }
	| '(' type ')' keywordselector optparmlist
		{ $$ = new_method ($2, $4, $5); }
	| keywordselector optparmlist
		{ $$ = new_method (&type_id, $1, $2); }
	;

optparmlist
	: /* empty */				{ $$ = 0; }
	| ',' ELLIPSIS				{ $$ = new_param (0, 0, 0); }
	| ',' param_list			{ $$ = $2; }
	| ',' param_list ',' ELLIPSIS
		{
			$$ = new_param (0, 0, 0);
			$$->next = $2;
		}
	;

unaryselector
	: selector					{ $$ = new_param ($1, 0, 0); }
	;

keywordselector
	: keyworddecl
	| keywordselector keyworddecl { $2->next = $1; $$ = $2; }
	;

selector
	: maybe_class
	| TYPE						{ $$ = save_string (yytext); }
	| reserved_word
	;

reserved_word
	: LOCAL						{ $$ = save_string (yytext); }
	| RETURN					{ $$ = save_string (yytext); }
	| WHILE						{ $$ = save_string (yytext); }
	| DO						{ $$ = save_string (yytext); }
	| IF						{ $$ = save_string (yytext); }
	| ELSE						{ $$ = save_string (yytext); }
	| FOR						{ $$ = save_string (yytext); }
	| BREAK						{ $$ = save_string (yytext); }
	| CONTINUE					{ $$ = save_string (yytext); }
	| SWITCH					{ $$ = save_string (yytext); }
	| CASE						{ $$ = save_string (yytext); }
	| DEFAULT					{ $$ = save_string (yytext); }
	| NIL						{ $$ = save_string (yytext); }
	| STRUCT					{ $$ = save_string (yytext); }
	| UNION						{ $$ = save_string (yytext); }
	| ENUM						{ $$ = save_string (yytext); }
	| TYPEDEF					{ $$ = save_string (yytext); }
	| SUPER						{ $$ = save_string (yytext); }
	;

keyworddecl
	: selector ':' '(' type ')' NAME
		{ $$ = new_param ($1, $4, $6); }
	| selector ':' NAME
		{ $$ = new_param ($1, &type_id, $3); }
	| ':' '(' type ')' NAME
		{ $$ = new_param ("", $3, $5); }
	| ':' NAME
		{ $$ = new_param ("", &type_id, $2); }
	;

obj_expr
	: obj_messageexpr
	| SELECTOR '(' selectorarg ')'	{ $$ = selector_expr ($3); }
	| PROTOCOL '(' maybe_class ')'	{ $$ = protocol_expr ($3); }
	| ENCODE '(' type ')'			{ $$ = encode_expr ($3); }
	| obj_string /* FIXME string object? */
	;

obj_messageexpr
	: '[' receiver messageargs ']'	{ $$ = message_expr ($2, $3); }
	;

receiver
	: expr
	| CLASS_NAME				{ $$ = new_name_expr ($1); }
	| SUPER						{ $$ = new_name_expr ("super"); }
	;

messageargs
	: selector					{ $$ = new_keywordarg ($1, 0); }
	| keywordarglist
	;

keywordarglist
	: keywordarg
	| keywordarglist keywordarg
		{
			$2->next = $1;
			$$ = $2;
		}
	;

keywordarg
	: selector ':' arg_list		{ $$ = new_keywordarg ($1, $3); }
	| ':' arg_list				{ $$ = new_keywordarg ("", $2); }
	;

selectorarg
	: selector					{ $$ = new_keywordarg ($1, 0); }
	| keywordnamelist
	;

keywordnamelist
	: keywordname
	| keywordnamelist keywordname
		{
			$2->next = $1;
			$$ = $2;
		}
	;

keywordname
	: selector ':'				{ $$ = new_keywordarg ($1, 0); }
	| ':'						{ $$ = new_keywordarg ("", 0); }
	;

obj_string
	: '@' STRING_VAL			{ $$ = new_string_expr ($2); }
	| obj_string '@' STRING_VAL
		{
			$$ = binary_expr ('+', $1, new_string_expr ($3));
		}
	;

%%

typedef struct def_state_s {
	struct def_state_s *next;
	def_t		*def;
	int			state;
} def_state_t;

static def_state_t *free_def_states;

static const char *
ds_get_key (void *_d, void *unused)
{
	return ((def_state_t *)_d)->def->name;
}

static void
ds_free_key (void *_d, void *unused)
{
	def_state_t *d = (def_state_t *)_d;
	d->next = free_def_states;
	free_def_states = d;
}

static void
scan_scope (hashtab_t *tab, scope_t *scope)
{
	def_t      *def;
	if (scope->type == sc_local)
		scan_scope (tab, scope->parent);
	for (def = scope->head; def; def = def->def_next) {
		if  (def->name && !def->removed) {
			def_state_t *ds;
			ALLOC (1024, def_state_t, def_states, ds);
			ds->def = def;
			ds->state = def->initialized;
			Hash_Add (tab, ds);
		}
	}
}

hashtab_t *
save_local_inits (scope_t *scope)
{
	hashtab_t  *tab = Hash_NewTable (61, ds_get_key, ds_free_key, 0);
	scan_scope (tab, scope);
	return tab;
}

hashtab_t *
merge_local_inits (hashtab_t *dl_1, hashtab_t *dl_2)
{
	hashtab_t  *tab = Hash_NewTable (61, ds_get_key, ds_free_key, 0);
	def_state_t **ds_list = (def_state_t **)Hash_GetList (dl_1);
	def_state_t **ds;
	def_state_t *d;
	def_state_t *nds;

	for (ds = ds_list; *ds; ds++) {
		d = Hash_Find (dl_2, (*ds)->def->name);
		(*ds)->def->initialized = (*ds)->state;

		ALLOC (1024, def_state_t, def_states, nds);
		nds->def = (*ds)->def;
		nds->state = (*ds)->state && d->state;
		Hash_Add (tab, nds);
	}
	free (ds_list);
	return tab;
}

void
restore_local_inits (hashtab_t *def_list)
{
	def_state_t **ds_list = (def_state_t **)Hash_GetList (def_list);
	def_state_t **ds;

	for (ds = ds_list; *ds; ds++)
		(*ds)->def->initialized = (*ds)->state;
	free (ds_list);
}

void
free_local_inits (hashtab_t *def_list)
{
	Hash_DelTable (def_list);
}

expr_t *
argc_expr (void)
{
	warning (0, "@argc deprecated: use @args.count");
	return binary_expr ('.', new_name_expr (".args"), new_name_expr ("count"));
}

expr_t *
argv_expr (void)
{
	warning (0, "@argv deprecated: use @args.list");
	return binary_expr ('.', new_name_expr (".args"), new_name_expr ("list"));
}
