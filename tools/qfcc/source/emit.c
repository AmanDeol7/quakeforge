/*
	emit.c

	statement emittion

	Copyright (C) 2001 Bill Currie <bill@taniwha.org>

	Author: Bill Currie <bill@taniwha.org>
	Date: 2001/07/26

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

#include <QF/mathlib.h>
#include <QF/va.h>

#include "def.h"
#include "debug.h"
#include "emit.h"
#include "expr.h"
#include "immediate.h"
#include "opcodes.h"
#include "options.h"
#include "qfcc.h"
#include "reloc.h"
#include "type.h"
#include "qc-parse.h"

static expr_t zero;

codespace_t *
codespace_new (void)
{
	return calloc (1, sizeof (codespace_t));
}

void
codespace_delete (codespace_t *codespace)
{
	free (codespace->code);
	free (codespace);
}

void
codespace_addcode (codespace_t *codespace, dstatement_t *code, int size)
{
	if (codespace->size + size > codespace->max_size) {
		codespace->max_size = (codespace->size + size + 16383) & ~16383;
		codespace->code = realloc (codespace->code,
								   codespace->max_size * sizeof (dstatement_t));
	}
	memcpy (codespace->code + codespace->size, code,
			size * sizeof (dstatement_t));
	codespace->size += size;
}

dstatement_t *
codespace_newstatement (codespace_t *codespace)
{
	if (codespace->size >= codespace->max_size) {
		codespace->max_size += 16384;
		codespace->code = realloc (codespace->code,
								   codespace->max_size * sizeof (dstatement_t));
	}
	return codespace->code + codespace->size++;
}

static void
add_statement_ref (def_t *def, dstatement_t *st, int field)
{
	if (def) {
		int         st_ofs = st - pr.code->code;

		def->users--;
		def->used = 1;

		if (def->alias) {
			def = def->alias;
			def->used = 1;
			reloc_op_def_ofs (def, st_ofs, field);
		} else
			reloc_op_def (def, st_ofs, field);
	}
}

def_t *
emit_statement (expr_t *e, opcode_t *op, def_t *var_a, def_t *var_b,
				def_t *var_c)
{
	dstatement_t *statement;
	def_t      *ret;

	if (!op) {
		error (e, "ice ice baby");
		abort ();
	}
	if (options.code.debug) {
		unsigned int line = (e ? e->line : pr.source_line) - lineno_base;

		if (line != pr.linenos[pr.num_linenos - 1].line) {
			pr_lineno_t *lineno = new_lineno ();

			lineno->line = line;
			lineno->fa.addr = pr.code->size;
		}
	}
	statement = codespace_newstatement (pr.code);
	statement->op = op->opcode;
	statement->a = var_a ? var_a->ofs : 0;
	statement->b = var_b ? var_b->ofs : 0;
	if (op->type_c == ev_void || op->right_associative) {
		// ifs, gotos, and assignments don't need vars allocated
		if (op->type_c == ev_void) {
			var_c = NULL;
			statement->c = 0;
		} else {
			statement->c = var_c->ofs;
		}
		ret = var_a;
	} else {							// allocate result space
		if (!var_c) {
			var_c = get_tempdef (ev_types[op->type_c], current_scope);
			var_c->users += 2;
		}
		statement->c = var_c->ofs;
		ret = var_c;
	}
#if 0
	printf ("%s %s(%d) %s(%d) %s(%d)\n", op->opname,
			var_a ? var_a->name : "", statement->a,
			var_b ? var_b->name : "", statement->b,
			var_c ? var_c->name : "", statement->c);
#endif

	add_statement_ref (var_a, statement, 0);
	add_statement_ref (var_b, statement, 1);
	add_statement_ref (var_c, statement, 2);

	if (op->right_associative)
		return var_a;
	return var_c;
}

static void
emit_branch (expr_t *_e, opcode_t *op, expr_t *e, expr_t *l)
{
	dstatement_t *st;
	reloc_t    *ref;
	def_t      *def = 0;
	int         ofs;

	if (e)
		def = emit_sub_expr (e, 0);
	ofs = pr.code->size;
	emit_statement (_e, op, def, 0, 0);
	st = &pr.code->code[ofs];
	if (l->e.label.ofs) {
		if (op == op_goto)
			st->a = l->e.label.ofs - ofs;
		else
			st->b = l->e.label.ofs - ofs;
	} else {
		ref = new_reloc (ofs, op == op_goto ? rel_op_a_op : rel_op_b_op);
		ref->next = l->e.label.refs;
		l->e.label.refs = ref;
	}
}

static def_t *
emit_function_call (expr_t *e, def_t *dest)
{
	def_t      *func = emit_sub_expr (e->e.expr.e1, 0);
	def_t      *parm;
	def_t      *ret;
	def_t      *arg;
	expr_t     *earg;
	opcode_t   *op;
	int         count = 0, ind;

	for (earg = e->e.expr.e2; earg; earg = earg->next)
		count++;
	ind = count;
	for (earg = e->e.expr.e2; earg; earg = earg->next) {
		ind--;
		parm = emit_sub_expr (new_param_expr (get_type (earg), ind), 0);
		if (parm->type->type == ev_struct) {
			expr_t     *a = assign_expr (new_def_expr (parm), earg);
			a->line = e->line;
			a->file = e->file;
			emit_expr (a);
		} else {
			arg = emit_sub_expr (earg, parm);
			if (arg != parm) {
				op = opcode_find ("=", arg->type, arg->type, &type_void);
				emit_statement (e, op, arg, parm, 0);
			}
		}
	}
	op = opcode_find (va ("<CALL%d>", count), &type_function, &type_void,
						 &type_void);
	emit_statement (e, op, func, 0, 0);

	ret = emit_sub_expr (new_ret_expr (func->type->aux_type), 0);
	if (dest) {
		op = opcode_find ("=", dest->type, ret->type, &type_void);
		emit_statement (e, op, ret, dest, 0);
		return dest;
	} else {
		return ret;
	}
}

static def_t *
emit_assign_expr (int oper, expr_t *e)
{
	def_t      *def_a, *def_b, *def_c;
	opcode_t   *op;
	expr_t     *e1 = e->e.expr.e1;
	expr_t     *e2 = e->e.expr.e2;
	const char *operator = get_op_string (oper);

	if (e1->type == ex_temp && e1->e.temp.users < 2) {
		e1->e.temp.users--;
		return 0;
	}
	if (oper == '=') {
		def_a = emit_sub_expr (e1, 0);
		if (def_a->constant) {
			if (options.code.cow) {
				int         size = type_size (def_a->type);
				int         ofs = new_location (def_a->type, pr.near_data);

				memcpy (G_POINTER (void, ofs), G_POINTER (void, def_a->ofs),
						size);
				def_a->ofs = ofs;
				def_a->constant = 0;
				def_a->nosave = 1;
				if (options.warnings.cow)
					warning (e1, "assignment to constant %s (Moooooooo!)",
							 def_a->name);
			} else {
				if (options.traditional)
					warning (e1, "assignment to constant %s", def_a->name);
				else
					error (e1, "assignment to constant %s", def_a->name);
			}
		}
		def_b = emit_sub_expr (e2, def_a);
		if (def_b != def_a) {
			op = opcode_find (operator, def_b->type, def_a->type, &type_void);
			emit_statement (e, op, def_b, def_a, 0);
		}
		return def_a;
	} else {
		def_b = emit_sub_expr (e2, 0);
		if (e->rvalue && def_b->managed)
			def_b->users++;
		if (e1->type == ex_expr && extract_type (e1->e.expr.e1) == ev_pointer
			&& e1->e.expr.e1->type < ex_string) {
			def_a = emit_sub_expr (e1->e.expr.e1, 0);
			def_c = emit_sub_expr (e1->e.expr.e2, 0);
			op = opcode_find (operator, def_b->type, def_a->type, def_c->type);
		} else {
			def_a = emit_sub_expr (e1, 0);
			def_c = 0;
			op = opcode_find (operator, def_b->type, def_a->type, &type_void);
		}
		emit_statement (e, op, def_b, def_a, def_c);
		return def_b;
	}
}

static def_t *
emit_bind_expr (expr_t *e1, expr_t *e2)
{
	type_t     *t1 = get_type (e1);
	type_t     *t2 = get_type (e2);
	def_t      *def;

	if (!e2 || e2->type != ex_temp) {
		error (e1, "internal error");
		abort ();
	}
	def = emit_sub_expr (e1, e2->e.temp.def);
	if (t1 != t2) {
		def_t      *tmp = new_def (t2, 0, def->scope);

		tmp->ofs = 0;
		tmp->alias = def;
		tmp->users = e2->e.temp.users;
		tmp->freed = 1;				// don't free this offset when freeing def
		def = tmp;
	}
	e2->e.temp.def = def;
	return e2->e.temp.def;
}

static def_t *
emit_move_expr (expr_t *e)
{
	expr_t     *e1 = e->e.expr.e1;
	expr_t     *e2 = e->e.expr.e2;
	expr_t     *size_expr;
	def_t      *size, *src, *dst;
	type_t     *src_type, *dst_type;
	opcode_t   *op;

	dst_type = get_type (e1);
	src_type = get_type (e2);
	src = emit_sub_expr (e2, 0);
	dst = emit_sub_expr (e1, 0);

	if (dst_type->type == ev_struct && src_type->type == ev_struct) {
		size_expr = new_short_expr (type_size (dst->type));
	} else if (dst_type->type == ev_struct) {
		if (dst->alias)
			dst = dst->alias;
		dst = emit_sub_expr (address_expr (new_def_expr (dst), 0, 0), 0);
		size_expr = new_integer_expr (type_size (dst_type));
	} else if (src_type->type == ev_struct) {
		if (src->alias)
			src = src->alias;
		src = emit_sub_expr (address_expr (new_def_expr (src), 0, 0), 0);
		size_expr = new_integer_expr (type_size (dst_type->aux_type));
	} else {
		size_expr = new_integer_expr (type_size (dst_type->aux_type));
	}
	size = emit_sub_expr (size_expr, 0);

	op = opcode_find ("<MOVE>", src->type, size->type, dst->type);
	return emit_statement (e, op, src, size, dst);
}

static def_t *
emit_address_expr (expr_t *e)
{
	def_t      *def_a, *def_b, *d;
	opcode_t   *op;

	def_a = emit_sub_expr (e->e.expr.e1, 0);
	def_b = emit_sub_expr (e->e.expr.e2, 0);
	op = opcode_find ("&", def_a->type, def_b->type, 0);
	d = emit_statement (e, op, def_a, def_b, 0);
	return d;
}

static def_t *
emit_deref_expr (expr_t *e, def_t *dest)
{
	def_t      *d;
	type_t     *type = e->e.expr.type;
	def_t      *z;
	opcode_t   *op;

	e = e->e.expr.e1;
	if (e->type == ex_pointer) {
		if (e->e.pointer.def) {
			d = new_def (e->e.pointer.type, 0, e->e.pointer.def->scope);
			d->local = e->e.pointer.def->local;
			d->ofs = e->e.pointer.val;
			d->alias = e->e.pointer.def;
		} else if (e->e.pointer.val >= 0 && e->e.pointer.val < 65536) {
			d = new_def (e->e.pointer.type, 0, current_scope);
			d->ofs = e->e.pointer.val;
		} else {
			d = ReuseConstant (e, 0);
			zero.type = ex_short;
			z = emit_sub_expr (&zero, 0);
			op = opcode_find (".", d->type, z->type, dest->type);
			d = emit_statement (e, op, d, z, dest);
		}
		return d;
	}
	if (e->type == ex_uexpr && e->e.expr.op == '&'
		&& e->e.expr.e1->type == ex_def) {
		d = new_def (e->e.expr.type->aux_type, 0, current_scope);
		d->alias = e->e.expr.e1->e.def;
		d->local = d->alias->local;
		d->ofs = d->alias->ofs;
		return d;
	}
	if (!dest) {
		dest = get_tempdef (type, current_scope);
		dest->users += 2;
	}

	if (e->type == ex_expr
		&& e->e.expr.op == '&'
		&& e->e.expr.e1->type < ex_string)
		e->e.expr.op = '.';
	d = emit_sub_expr (e, dest);

	if (dest && d != dest) {
		zero.type = ex_short;
		z = emit_sub_expr (&zero, 0);
		op = opcode_find (".", d->type, z->type, dest->type);
		d = emit_statement (e, op, d, z, dest);
	} else {
		if (!d->name)
			d->type = type;
	}
	return d;
}

def_t *
emit_sub_expr (expr_t *e, def_t *dest)
{
	opcode_t   *op;
	const char *operator;
	def_t      *def_a, *def_b, *d = 0;
	def_t      *tmp = 0;

	switch (e->type) {
		case ex_block:
			if (e->e.block.result) {
				expr_t     *res = e->e.block.result;
				for (e = e->e.block.head; e; e = e->next)
					emit_expr (e);
				d = emit_sub_expr (res, dest);
				break;
			}
		case ex_name:
		case ex_nil:
		case ex_label:
		case ex_error:
			error (e, "internal error");
			abort ();
		case ex_expr:
			if (e->e.expr.op == 'M') {
				d = emit_move_expr (e);
				break;
			}
			if (e->e.expr.op == 'b') {
				d = emit_bind_expr (e->e.expr.e1, e->e.expr.e2);
				break;
			}
			if (e->e.expr.op == 'c') {
				d = emit_function_call (e, dest);
				break;
			}
			if (e->e.expr.op == '=' || e->e.expr.op == PAS) {
				d = emit_assign_expr (e->e.expr.op, e);
				if (!d->managed)
					d->users++;
				break;
			}
			if (e->e.expr.op == '&' && e->e.expr.type->type == ev_pointer) {
				d = emit_address_expr (e);
				break;
			}
			if (e->e.expr.e1->type == ex_block
				&& e->e.expr.e1->e.block.is_call) {
				def_b = emit_sub_expr (e->e.expr.e2, 0);
				def_a = emit_sub_expr (e->e.expr.e1, 0);
			} else {
				def_a = emit_sub_expr (e->e.expr.e1, 0);
				def_b = emit_sub_expr (e->e.expr.e2, 0);
			}
			operator = get_op_string (e->e.expr.op);
			if (!dest) {
				dest = get_tempdef (e->e.expr.type, current_scope);
				dest->users += 2;
			}
			op = opcode_find (operator, def_a->type, def_b->type,
							  dest->type);
			d = emit_statement (e, op, def_a, def_b, dest);
			break;
		case ex_uexpr:
			switch (e->e.expr.op) {
				case '!':
					operator = "!";
					def_a = emit_sub_expr (e->e.expr.e1, 0);
					def_b = &def_void;
					break;
				case '~':
					operator = "~";
					def_a = emit_sub_expr (e->e.expr.e1, 0);
					def_b = &def_void;
					break;
				case '-':
					zero.type = expr_types[extract_type (e->e.expr.e1)];

					operator = "-";
					def_a = ReuseConstant (&zero, 0);
					def_b = emit_sub_expr (e->e.expr.e1, 0);
					if (!dest) {
						dest = get_tempdef (e->e.expr.type, current_scope);
						dest->users += 2;
					}
					break;
				case '&':
					zero.type = ex_short;

					operator = "&";
					if (e->e.expr.e1->type == ex_expr
						&& e->e.expr.e1->e.expr.op == '.') {
						tmp = get_tempdef (e->e.expr.type, current_scope);
						tmp->users += 2;
						def_b = emit_sub_expr (&zero, 0);
					} else {
						def_b = &def_void;
					}
					def_a = emit_sub_expr (e->e.expr.e1, tmp);
					if (!dest) {
						dest = get_tempdef (e->e.expr.type, current_scope);
						dest->users += 2;
					}
					break;
				case '.':
					return emit_deref_expr (e, dest);
				case 'C':
					def_a = emit_sub_expr (e->e.expr.e1, 0);
					if ((def_a->type->type == ev_pointer
						 && e->e.expr.type->type == ev_pointer)
						|| (def_a->type->type == ev_func
							&& e->e.expr.type->type == ev_func)) {
						return def_a;
					}
					if ((def_a->type->type == ev_pointer
						 && (e->e.expr.type->type == ev_integer
							 || e->e.expr.type->type == ev_uinteger))
						|| ((def_a->type->type == ev_integer
							 || def_a->type->type == ev_uinteger)
							&& e->e.expr.type->type == ev_pointer)) {
						def_t      *tmp;
						tmp = new_def (e->e.expr.type, 0, def_a->scope);
						tmp->ofs = 0;
						tmp->alias = def_a;
						tmp->users = def_a->users--;
						tmp->freed = 1;
						return tmp;
					}
					def_b = &def_void;
					if (!dest) {
						dest = get_tempdef (e->e.expr.type, current_scope);
						dest->users = 2;
					}
					operator = "=";
					break;
				default:
					abort ();
			}
			op = opcode_find (operator, def_a->type, def_b->type,
							  dest ? dest->type : 0);
			d = emit_statement (e, op, def_a, def_b, dest);
			break;
		case ex_def:
			d = e->e.def;
			break;
		case ex_temp:
			if (!e->e.temp.def) {
				if (dest)
					e->e.temp.def = dest;
				else
					e->e.temp.def = get_tempdef (e->e.temp.type, current_scope);
				e->e.temp.def->users = e->e.temp.users;
				e->e.temp.def->expr = e;
				e->e.temp.def->managed = 1;
			}
			d = e->e.temp.def;
			break;
		case ex_pointer:
		case ex_string:
		case ex_float:
		case ex_vector:
		case ex_entity:
		case ex_field:
		case ex_func:
		case ex_quaternion:
		case ex_integer:
		case ex_uinteger:
			d = ReuseConstant (e, 0);
			break;
		case ex_short:
			d = new_def (&type_short, 0, 0);
			d->ofs = e->e.short_val;
			d->absolute = 1;
			d->users = 1;
			break;
	}
	free_tempdefs ();
	return d;
}

void
emit_expr (expr_t *e)
{
	def_t      *def;
	def_t      *def_a;
	def_t      *def_b;
	ex_label_t *label;

	//printf ("%d ", e->line);
	//print_expr (e);
	//puts ("");
	switch (e->type) {
		case ex_error:
			break;
		case ex_label:
			label = &e->e.label;
			label->ofs = pr.code->size;
			relocate_refs (label->refs, label->ofs);
			break;
		case ex_block:
			for (e = e->e.block.head; e; e = e->next)
				emit_expr (e);
			break;
		case ex_expr:
			switch (e->e.expr.op) {
				case 'M':
					emit_move_expr (e);
					break;
				case PAS:
				case '=':
					emit_assign_expr (e->e.expr.op, e);
					break;
				case 'n':
					emit_branch (e, op_ifnot, e->e.expr.e1, e->e.expr.e2);
					break;
				case 'i':
					emit_branch (e, op_if, e->e.expr.e1, e->e.expr.e2);
					break;
				case IFBE:
					emit_branch (e, op_ifbe, e->e.expr.e1, e->e.expr.e2);
					break;
				case IFB:
					emit_branch (e, op_ifb, e->e.expr.e1, e->e.expr.e2);
					break;
				case IFAE:
					emit_branch (e, op_ifae, e->e.expr.e1, e->e.expr.e2);
					break;
				case IFA:
					emit_branch (e, op_ifa, e->e.expr.e1, e->e.expr.e2);
					break;
				case 'c':
					emit_function_call (e, 0);
					break;
				case 's':
					def_a = emit_sub_expr (e->e.expr.e1, 0);
					def_b = emit_sub_expr (e->e.expr.e2, 0);
					emit_statement (e, op_state, def_a, def_b, 0);
					break;
				case 'b':
					emit_bind_expr (e->e.expr.e1, e->e.expr.e2);
					break;
				case 'g':
					def_a = emit_sub_expr (e->e.expr.e1, 0);
					def_b = emit_sub_expr (e->e.expr.e2, 0);
					emit_statement (e, op_jumpb, def_a, def_b, 0);
					break;
				default:
					warning (e, "Non-executable statement; "
							 "executing programmer instead.");
					break;
			}
			break;
		case ex_uexpr:
			switch (e->e.expr.op) {
				case 'r':
					def = 0;
					if (e->e.expr.e1)
						def = emit_sub_expr (e->e.expr.e1, 0);
					emit_statement (e, op_return, def, 0, 0);
					break;
				case 'g':
					emit_branch (e, op_goto, 0, e->e.expr.e1);
					break;
				default:
					warning (e, "Non-executable statement; "
							 "executing programmer instead.");
					emit_expr (e->e.expr.e1);
					break;
			}
			break;
		case ex_def:
		case ex_temp:
		case ex_string:
		case ex_float:
		case ex_vector:
		case ex_entity:
		case ex_field:
		case ex_func:
		case ex_pointer:
		case ex_quaternion:
		case ex_integer:
		case ex_uinteger:
		case ex_short:
		case ex_name:
		case ex_nil:
			warning (e, "Non-executable statement; "
					 "executing programmer instead.");
			break;
	}
	free_tempdefs ();
}
