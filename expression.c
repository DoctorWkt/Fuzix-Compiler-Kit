#include <stddef.h>
#include "compiler.h"


/*
 *	Build an argument tree for right to left stacking
 */
struct node *call_args(void)
{
	struct node *n = expression_tree(0);
	if (match(T_COMMA))
		/* Switch around for calling order */
		return tree(T_COMMA, call_args(), n);
	require(T_RPAREN);
	return n;
}

/*
 *	Generate a function call tree - no type checking arg counts etc
 *	yet.
 */
struct node *function_call(struct node *n)
{
	unsigned type;
	if (!IS_FUNCTION(n->type))
		error("not a function");
	type = func_return(n->type);
	n = tree(T_FUNCCALL, call_args(), n);
	n->type = type;
	return n;
}

/*
 *	Postfixed array and structure dereferences (basically the same but
 *	one is named and re-typed), and function calls.
 */
static struct node *hier11(void)
{
	int direct;
	struct node *l, *r;
	unsigned ptr;
	struct symbol *sym;
	unsigned sname;
	unsigned scale;
	unsigned *tag;

	l = primary();

	ptr = PTR(l->type) || IS_ARRAY(l->type);
	if (token == T_LSQUARE || token == T_LPAREN || token == T_DOT
	    || token == T_POINTSTO) {
		for (;;) {
			if (match(T_LSQUARE)) {
				if (ptr == 0) {
					error("can't subscript");
					junk();
					require(T_RSQUARE);
					return (0);
				}
				r = expression_tree(1);
				require(T_RSQUARE);
				/* Need a proper method for this stuff */
				/* TODO arrays */
				scale = type_ptrscale(l->type);
				l = tree(T_PLUS, l,
					 tree(T_STAR, r,
					      make_constant(scale, UINT)));
				l->flags |= LVAL;
				l->type = type_deref(l->type);
			} else if (match(T_LPAREN)) {
				l = function_call(l);
			} else if ((direct = match(T_DOT))
				   || match(T_POINTSTO)) {
				if (direct == 0)
					l = tree(T_DEREF, NULL, l);
				if (PTR(l->type)
				    || !IS_STRUCT(l->type)) {
					error("can't take member");
					junk();
					return 0;
				}
				tag = struct_find_member(l->type, symname());
				if (tag == NULL) {
					error("unknown member");
					junk();
					return 0;
				}
				l = tree(T_PLUS, l,
					 make_constant(tag[2], UINT));
				l->flags |= LVAL;
				l->type = tag[1];
			} else
				return l;
		}
	}
	return l;
}

/*
 *	Unary operators
 */
static struct node *hier10(void)
{
	struct node *l, *r;
	unsigned op;
	op = token;
	if (token != T_PLUSPLUS
	    && token != T_MINUSMINUS
	    && token != T_MINUS
	    && token != T_TILDE
	    && token != T_BANG && token != T_STAR && token != T_AND) {
		/* Check for trailing forms */
		l = hier11();
		if (token == T_PLUSPLUS || token == T_MINUSMINUS) {
			next_token();
			if (!(l->flags & LVAL)) {
				needlval();
				return l;
			}
			if (token == T_PLUSPLUS)
				return tree(T_POSTINC, NULL, l);
			else
				return tree(T_POSTDEC, NULL, l);
		}
		return l;
	}

	next_token();
	switch (op) {
	case T_PLUSPLUS:
	case T_MINUSMINUS:
		r = hier10();
		if (!r->flags & LVAL) {
			needlval();
			return (0);
		}
		/* TODO:  Scaling... */
		return tree(op, NULL, r);
	case T_TILDE:
	case T_BANG:
		return tree(op, NULL, make_rval(hier10()));
		/* Disambiguate forms */
	case T_MINUS:
		return tree(T_NEGATE, NULL, make_rval(hier10()));
	case T_STAR:
		/* TODO *array */
		r = tree(T_DEREF, NULL, make_rval(hier10()));
		r->type = r->right->type - 1;
		return r;
	case T_AND:
		r = hier10();
		if (r->flags & LVAL) {
			error("illegal address of");
			return r;
		}
		r = tree(T_ADDROF, NULL, r);
		r->type = type_addrof(r->type);
		return r;
	}
	/* gcc */
	return NULL;
}

/*
 *	Multiplication, division and remainder
 */
static struct node *hier9(void)
{
	struct node *l;
	unsigned op;
	l = hier10();
	if (token != T_STAR && token != T_PERCENT && token != T_SLASH)
		return l;
	op = token;
	next_token();
	if (op == T_PERCENT)
		return intarith_tree(op, l, hier9());
	else
		return arith_tree(op, l, hier9());
}

/*
 *	Addition and subtraction. Messy because of the pointer scaling
 *	rules.
 */
static struct node *hier8(void)
{
	struct node *l, *r;
	unsigned op;
	unsigned scale = 1;
	unsigned scalediv;
	l = hier9();
	if (token != T_PLUS && token != T_MINUS)
		return l;
	op = token;
	next_token();
	l = make_rval(l);
	r = make_rval(hier8());
	/* This can currently do silly things but the typechecks when added
	   will stop them from mattering */
	if (op == T_PLUS) {
		/* if left is pointer and right is int, scale right */
		scale =
		    type_ptrscale_binop(op, l->type, r->type, &scalediv);
	} else if (op == T_MINUS) {
		/* if dbl, can only be: pointer - int, or
		   pointer - pointer, thus,
		   in first case, int is scaled up,
		   in second, result is scaled down. */
		scale =
		    type_ptrscale_binop(op, l->type, r->type, &scalediv);
	}
	if (scale == 1)
		return tree(op, l, r);
	if (scalediv)
		return tree(T_SLASH, tree(op, l, r), make_constant(scale, UINT));
	if (PTR(l->type))
		return tree(op, l, tree(T_STAR, r, make_constant(scale, UINT)));
	else
		return tree(op, tree(T_STAR, l, make_constant(scale, UINT)), r);
}


/*
 *	Shifts
 */
static struct node *hier7(void)
{
	struct node *l;
	unsigned op;
	l = hier8();
	if (token != T_GTGT && token != T_LTLT)
		return l;
	op = token;
	next_token();
	/* The tree code knows about the shift rule being different for types */
	return intarith_tree(op, make_rval(l), make_rval(hier7()));
}

/*
 *	Ordered comparison operators
 */
static struct node *hier6(void)
{
	struct node *l;
	unsigned op;
	l = hier7();
	if (token != T_LT && token != T_GT
	    && token != T_LTEQ && token != T_GTEQ)
		return (l);
	op = token;
	next_token();
	return ordercomp_tree(op, make_rval(l), make_rval(hier6()));
}

/*
 *	Equality and not equal operators
 */
static struct node *hier5(void)
{
	struct node *l;
	unsigned op;
	l = hier6();
	if (token != T_EQEQ && token != T_BANGEQ)
		return (l);
	op = token;
	next_token();
	return ordercomp_tree(op, make_rval(l), make_rval(hier5()));
}

/*
 *	Bitwise and
 */
static struct node *hier4(void)
{
	struct node *l;
	l = hier5();
	if (!match(T_AND))
		return (l);
	return tree(T_AND, make_rval(l), make_rval(hier4()));
}

/*
 *	Bitwise xor
 */
static struct node *hier3(void)
{
	struct node *l;
	l = hier4();
	if (!match(T_HAT))
		return (l);
	return intarith_tree(T_HAT, make_rval(l), make_rval(hier3()));
}

/*
 *	Bitwise or
 */
static struct node *hier2(void)
{
	struct node *l;
	l = hier3();
	if (!match(T_OR))
		return (l);
	return intarith_tree(T_OR, make_rval(l), make_rval(hier2()));
}

/**
 * processes logical and &&
 * @param lval
 * @return 0 or 1, fetch or no fetch
 */
static struct node *hier1c(void)
{
	struct node *l;
	l = hier2();
	if (!match(T_ANDAND))
		return (l);
	return logic_tree(T_ANDAND, make_rval(l), make_rval(hier1c()));
}

/**
 * processes logical or ||
 * @param lval
 * @return 0 or 1, fetch or no fetch
 */
static struct node *hier1b(void)
{
	struct node *l;
	l = hier1c();
	if (!match(T_OROR))
		return (l);
	return logic_tree(T_OROR, make_rval(l), make_rval(hier1b()));
}

/*
 *	The ?: operator. This needs thought.
 */
static struct node *hier1a(void)
{
	struct node *l;
	struct node *a1, *a2;
	l = hier1b();
	if (!match(T_QUESTION))
		return (l);
	l = make_rval(l);
	/* Now do the left of the colon */
	a1 = make_rval(hier1a());
	if (!match(T_COLON)) {
		error("missing colon");
		return l;
	}
	a2 = make_rval(hier1b());
	return tree(T_QUESTION, l, tree(T_COLON, a1, a2));
}


/*
 *	Assignment between an lval on the left and an rval on the right
 *
 *	Handle pointer scaling on += and -= by emitting the maths into the
 *	tree.
 */
struct node *hier1(void)
{
	struct node *l, *r;
	unsigned fc;
	unsigned scale = 1;
	l = hier1a();
	if (match(T_EQ)) {
		if ((l->flags & LVAL) == 0)
			needlval();
		r = make_rval(hier1());
		return ordercomp_tree(T_EQ, l, r);	/* Assignment */
	} else {
		fc = token;
		if (match(T_MINUSEQ) ||
		    match(T_PLUSEQ) ||
		    match(T_STAREQ) ||
		    match(T_SLASHEQ) ||
		    match(T_PERCENTEQ) ||
		    match(T_SHREQ) ||
		    match(T_SHLEQ) ||
		    match(T_ANDEQ) || match(T_HATEQ) || match(T_OREQ)) {
			if ((l->flags & LVAL) == 0) {
				needlval();
				return (0);
			}
			r = make_rval(hier1());
			switch (fc) {
			case T_MINUSEQ:
			case T_PLUSEQ:
				scale = type_ptrscale(l->type);
				break;
			}
			if (scale)
				return tree(fc, l,
					    tree(T_STAR, r,
						 make_constant(scale, UINT)));
			return tree(fc, l, r);
		} else
			return l;
	}
	/* gcc */
	return NULL;
}

/*
 *	Top level of the expression tree. Make the tree an rval in case
 *	we need the result. Allow for both the expr,expr,expr format and
 *	the cases where C doesnt allow it (expr, expr in function calls
 *	or initializers is not the same
 */
struct node *expression_tree(unsigned comma)
{
	struct node *n;
	/* Build a tree of comma operations */
	n = make_rval(hier1());
	if (!comma || !match(T_COMMA))
		return n;
	n = tree(T_COMMA, n, expression_tree(comma));
	return n;
}


/* Generate an expression and write it the output */
void expression(unsigned comma, unsigned mkbool, unsigned noret)
{
	struct node *n, *c;
	if (token == T_SEMICOLON)
		return;
	n = expression_tree(comma);
	if (mkbool)
		n = tree(T_BOOL, NULL, n);
	if (noret)
		n->flags |= NORETURN;
	/* Constify it if we can to reduce the amount of generated code */
	c = constify(n);
	if (c)
		n = c;
	write_tree(n);
}

/* We need a another version of this for initializers that allows global or
   static names (and string labels) too */

unsigned const_expression(void)
{
	unsigned v = 1;
	struct node *e = expression_tree(0);
	struct node *n = constify(e);

	if (n && n->op >= T_INTVAL && n->op <= T_ULONGVAL)
		v = n->value;
	else
		error("not constant");
	if (n)
		free_tree(n);
	else
		free_tree(e);
	return v;
}

void bracketed_expression(unsigned mkbool)
{
	require(T_LPAREN);
	expression(1, mkbool, 0);
	require(T_RPAREN);
}

void expression_or_null(unsigned mkbool, unsigned noret)
{
	if (token == T_SEMICOLON || token == T_RPAREN) {
		write_tree(tree(T_NULL, NULL, NULL));
		/* null */
	} else
		expression(1, mkbool, noret);
}
