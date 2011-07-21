/* Translation of CLAST (CLooG AST) to Gimple.
   Copyright (C) 2009, 2010 Free Software Foundation, Inc.
   Contributed by Sebastian Pop <sebastian.pop@amd.com>.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "diagnostic-core.h"
#include "tree-flow.h"
#include "tree-dump.h"
#include "cfgloop.h"
#include "tree-chrec.h"
#include "tree-data-ref.h"
#include "tree-scalar-evolution.h"
#include "sese.h"

#ifdef HAVE_cloog
#include "cloog/cloog.h"
#include "ppl_c.h"
#include "graphite-cloog-util.h"
#include "graphite-ppl.h"
#include "graphite-poly.h"
#include "graphite-clast-to-gimple.h"
#include "graphite-dependences.h"
#include "graphite-cloog-compat.h"

/* This flag is set when an error occurred during the translation of
   CLAST to Gimple.  */
static bool gloog_error;

/* Verifies properties that GRAPHITE should maintain during translation.  */

static inline void
graphite_verify (void)
{
#ifdef ENABLE_CHECKING
  verify_loop_structure ();
  verify_dominators (CDI_DOMINATORS);
  verify_loop_closed_ssa (true);
#endif
}

/* Stores the INDEX in a vector and the loop nesting LEVEL for a given
   clast NAME.  */

typedef struct clast_name_index {
  int index;
  int level;
  const char *name;
} *clast_name_index_p;

/* Returns a pointer to a new element of type clast_name_index_p built
   from NAME, LEVEL, and INDEX.  */

static inline clast_name_index_p
new_clast_name_index (const char *name, int index, int level)
{
  clast_name_index_p res = XNEW (struct clast_name_index);

  res->name = name;
  res->level = level;
  res->index = index;
  return res;
}

/* For a given clast NAME, returns -1 if NAME is not in the
   INDEX_TABLE, otherwise returns the loop level for the induction
   variable NAME, or if it is a parameter, the parameter number in the
   vector of parameters.  */

static inline int
clast_name_to_level (clast_name_p name, htab_t index_table)
{
  struct clast_name_index tmp;
  PTR *slot;

#ifdef CLOOG_ORG
  gcc_assert (name->type == clast_expr_name);
  tmp.name = ((const struct clast_name *) name)->name;
#else
  tmp.name = name;
#endif

  slot = htab_find_slot (index_table, &tmp, NO_INSERT);

  if (slot && *slot)
    return ((struct clast_name_index *) *slot)->level;

  return -1;
}

/* For a given clast NAME, returns -1 if it does not correspond to any
   parameter, or otherwise, returns the index in the PARAMS or
   SCATTERING_DIMENSIONS vector.  */

static inline int
clast_name_to_index (clast_name_p name, htab_t index_table)
{
  struct clast_name_index tmp;
  PTR *slot;

#ifdef CLOOG_ORG
  gcc_assert (name->type == clast_expr_name);
  tmp.name = ((const struct clast_name *) name)->name;
#else
  tmp.name = name;
#endif

  slot = htab_find_slot (index_table, &tmp, NO_INSERT);

  if (slot && *slot)
    return ((struct clast_name_index *) *slot)->index;

  return -1;
}

/* Records in INDEX_TABLE the INDEX and LEVEL for NAME.  */

static inline void
save_clast_name_index (htab_t index_table, const char *name,
		       int index, int level)
{
  struct clast_name_index tmp;
  PTR *slot;

  tmp.name = name;
  slot = htab_find_slot (index_table, &tmp, INSERT);

  if (slot)
    {
      free (*slot);

      *slot = new_clast_name_index (name, index, level);
    }
}

/* Computes a hash function for database element ELT.  */

static inline hashval_t
clast_name_index_elt_info (const void *elt)
{
  return htab_hash_pointer (((const struct clast_name_index *) elt)->name);
}

/* Compares database elements E1 and E2.  */

static inline int
eq_clast_name_indexes (const void *e1, const void *e2)
{
  const struct clast_name_index *elt1 = (const struct clast_name_index *) e1;
  const struct clast_name_index *elt2 = (const struct clast_name_index *) e2;

  return (elt1->name == elt2->name);
}



/* NEWIVS_INDEX binds CLooG's scattering name to the index of the tree
   induction variable in NEWIVS.

   PARAMS_INDEX binds CLooG's parameter name to the index of the tree
   parameter in PARAMS.  */

typedef struct ivs_params {
  VEC (tree, heap) *params, **newivs;
  htab_t newivs_index, params_index;
  sese region;
} *ivs_params_p;

/* Returns the tree variable from the name NAME that was given in
   Cloog representation.  */

static tree
clast_name_to_gcc (clast_name_p name, ivs_params_p ip)
{
  int index;

  if (ip->params && ip->params_index)
    {
      index = clast_name_to_index (name, ip->params_index);

      if (index >= 0)
	return VEC_index (tree, ip->params, index);
    }

  gcc_assert (*(ip->newivs) && ip->newivs_index);
  index = clast_name_to_index (name, ip->newivs_index);
  gcc_assert (index >= 0);

  return VEC_index (tree, *(ip->newivs), index);
}

/* Returns the signed maximal precision type for expressions TYPE1 and TYPE2.  */

static tree
max_signed_precision_type (tree type1, tree type2)
{
  int p1 = TYPE_PRECISION (type1);
  int p2 = TYPE_PRECISION (type2);
  int precision;
  tree type;
  enum machine_mode mode;

  if (p1 > p2)
    precision = TYPE_UNSIGNED (type1) ? p1 * 2 : p1;
  else
    precision = TYPE_UNSIGNED (type2) ? p2 * 2 : p2;

  if (precision > BITS_PER_WORD)
    {
      gloog_error = true;
      return integer_type_node;
    }

  mode = smallest_mode_for_size (precision, MODE_INT);
  precision = GET_MODE_PRECISION (mode);
  type = build_nonstandard_integer_type (precision, false);

  if (!type)
    {
      gloog_error = true;
      return integer_type_node;
    }

  return type;
}

/* Returns the maximal precision type for expressions TYPE1 and TYPE2.  */

static tree
max_precision_type (tree type1, tree type2)
{
  if (POINTER_TYPE_P (type1))
    return type1;

  if (POINTER_TYPE_P (type2))
    return type2;

  if (!TYPE_UNSIGNED (type1)
      || !TYPE_UNSIGNED (type2))
    return max_signed_precision_type (type1, type2);

  return TYPE_PRECISION (type1) > TYPE_PRECISION (type2) ? type1 : type2;
}

static tree
clast_to_gcc_expression (tree, struct clast_expr *, ivs_params_p);

/* Converts a Cloog reduction expression R with reduction operation OP
   to a GCC expression tree of type TYPE.  */

static tree
clast_to_gcc_expression_red (tree type, enum tree_code op,
			     struct clast_reduction *r, ivs_params_p ip)
{
  int i;
  tree res = clast_to_gcc_expression (type, r->elts[0], ip);
  tree operand_type = (op == POINTER_PLUS_EXPR) ? sizetype : type;

  for (i = 1; i < r->n; i++)
    {
      tree t = clast_to_gcc_expression (operand_type, r->elts[i], ip);
      res = fold_build2 (op, type, res, t);
    }

  return res;
}

/* Converts a Cloog AST expression E back to a GCC expression tree of
   type TYPE.  */

static tree
clast_to_gcc_expression (tree type, struct clast_expr *e, ivs_params_p ip)
{
  switch (e->type)
    {
    case clast_expr_term:
      {
	struct clast_term *t = (struct clast_term *) e;

	if (t->var)
	  {
	    if (mpz_cmp_si (t->val, 1) == 0)
	      {
		tree name = clast_name_to_gcc (t->var, ip);

		if (POINTER_TYPE_P (TREE_TYPE (name)) != POINTER_TYPE_P (type))
		  name = fold_convert (sizetype, name);

		name = fold_convert (type, name);
		return name;
	      }

	    else if (mpz_cmp_si (t->val, -1) == 0)
	      {
		tree name = clast_name_to_gcc (t->var, ip);

		if (POINTER_TYPE_P (TREE_TYPE (name)) != POINTER_TYPE_P (type))
		  name = fold_convert (sizetype, name);

		name = fold_convert (type, name);

		return fold_build1 (NEGATE_EXPR, type, name);
	      }
	    else
	      {
		tree name = clast_name_to_gcc (t->var, ip);
		tree cst = gmp_cst_to_tree (type, t->val);

		if (POINTER_TYPE_P (TREE_TYPE (name)) != POINTER_TYPE_P (type))
		  name = fold_convert (sizetype, name);

		name = fold_convert (type, name);

		if (!POINTER_TYPE_P (type))
		  return fold_build2 (MULT_EXPR, type, cst, name);

		gloog_error = true;
		return cst;
	      }
	  }
	else
	  return gmp_cst_to_tree (type, t->val);
      }

    case clast_expr_red:
      {
        struct clast_reduction *r = (struct clast_reduction *) e;

        switch (r->type)
          {
	  case clast_red_sum:
	    return clast_to_gcc_expression_red
	      (type, POINTER_TYPE_P (type) ? POINTER_PLUS_EXPR : PLUS_EXPR,
	       r, ip);

	  case clast_red_min:
	    return clast_to_gcc_expression_red (type, MIN_EXPR, r, ip);

	  case clast_red_max:
	    return clast_to_gcc_expression_red (type, MAX_EXPR, r, ip);

	  default:
	    gcc_unreachable ();
          }
        break;
      }

    case clast_expr_bin:
      {
	struct clast_binary *b = (struct clast_binary *) e;
	struct clast_expr *lhs = (struct clast_expr *) b->LHS;
	tree tl = clast_to_gcc_expression (type, lhs, ip);
	tree tr = gmp_cst_to_tree (type, b->RHS);

	switch (b->type)
	  {
	  case clast_bin_fdiv:
	    return fold_build2 (FLOOR_DIV_EXPR, type, tl, tr);

	  case clast_bin_cdiv:
	    return fold_build2 (CEIL_DIV_EXPR, type, tl, tr);

	  case clast_bin_div:
	    return fold_build2 (EXACT_DIV_EXPR, type, tl, tr);

	  case clast_bin_mod:
	    return fold_build2 (TRUNC_MOD_EXPR, type, tl, tr);

	  default:
	    gcc_unreachable ();
	  }
      }

    default:
      gcc_unreachable ();
    }

  return NULL_TREE;
}

/* Return a type that could represent the values between V1 and V2.  */

static tree
gcc_type_for_interval (mpz_t v1, mpz_t v2)
{
  bool unsigned_p;
  tree type;
  enum machine_mode mode;
  int precision = MAX (mpz_sizeinbase (v1, 2),
		       mpz_sizeinbase (v2, 2));

  if (precision > BITS_PER_WORD)
    {
      gloog_error = true;
      return integer_type_node;
    }

  if (mpz_cmp (v1, v2) <= 0)
    unsigned_p = (mpz_sgn (v1) >= 0);
  else
    unsigned_p = (mpz_sgn (v2) >= 0);

  mode = smallest_mode_for_size (precision, MODE_INT);
  precision = GET_MODE_PRECISION (mode);
  type = build_nonstandard_integer_type (precision, unsigned_p);

  if (!type)
    {
      gloog_error = true;
      return integer_type_node;
    }

  return type;
}

/* Return a type that could represent the integer value VAL, or
   otherwise return NULL_TREE.  */

static tree
gcc_type_for_value (mpz_t val)
{
  return gcc_type_for_interval (val, val);
}

/* Return the type for the clast_term T used in STMT.  */

static tree
gcc_type_for_clast_term (struct clast_term *t,
			 ivs_params_p ip)
{
  gcc_assert (t->expr.type == clast_expr_term);

  if (!t->var)
    return gcc_type_for_value (t->val);

  return TREE_TYPE (clast_name_to_gcc (t->var, ip));
}

static tree
gcc_type_for_clast_expr (struct clast_expr *, ivs_params_p);

/* Return the type for the clast_reduction R used in STMT.  */

static tree
gcc_type_for_clast_red (struct clast_reduction *r,
			ivs_params_p ip)
{
  int i;
  tree type = NULL_TREE;

  if (r->n == 1)
    return gcc_type_for_clast_expr (r->elts[0], ip);

  switch (r->type)
    {
    case clast_red_sum:
    case clast_red_min:
    case clast_red_max:
      type = gcc_type_for_clast_expr (r->elts[0], ip);
      for (i = 1; i < r->n; i++)
	type = max_precision_type (type, gcc_type_for_clast_expr
				   (r->elts[i], ip));

      return type;

    default:
      break;
    }

  gcc_unreachable ();
  return NULL_TREE;
}

/* Return the type for the clast_binary B used in STMT.  */

static tree
gcc_type_for_clast_bin (struct clast_binary *b, ivs_params_p ip)
{
  tree l = gcc_type_for_clast_expr ((struct clast_expr *) b->LHS, ip);
  tree r = gcc_type_for_value (b->RHS);
  return max_signed_precision_type (l, r);
}

/* Returns the type for the CLAST expression E when used in statement
   STMT.  */

static tree
gcc_type_for_clast_expr (struct clast_expr *e,
			 ivs_params_p ip)
{
  switch (e->type)
    {
    case clast_expr_term:
      return gcc_type_for_clast_term ((struct clast_term *) e, ip);

    case clast_expr_red:
      return gcc_type_for_clast_red ((struct clast_reduction *) e, ip);

    case clast_expr_bin:
      return gcc_type_for_clast_bin ((struct clast_binary *) e, ip);

    default:
      gcc_unreachable ();
    }

  return NULL_TREE;
}

/* Returns the type for the equation CLEQ.  */

static tree
gcc_type_for_clast_eq (struct clast_equation *cleq,
		       ivs_params_p ip)
{
  tree l = gcc_type_for_clast_expr (cleq->LHS, ip);
  tree r = gcc_type_for_clast_expr (cleq->RHS, ip);
  return max_precision_type (l, r);
}

/* Translates a clast equation CLEQ to a tree.  */

static tree
graphite_translate_clast_equation (struct clast_equation *cleq,
				   ivs_params_p ip)
{
  enum tree_code comp;
  tree type = gcc_type_for_clast_eq (cleq, ip);
  tree lhs = clast_to_gcc_expression (type, cleq->LHS, ip);
  tree rhs = clast_to_gcc_expression (type, cleq->RHS, ip);

  if (cleq->sign == 0)
    comp = EQ_EXPR;

  else if (cleq->sign > 0)
    comp = GE_EXPR;

  else
    comp = LE_EXPR;

  return fold_build2 (comp, boolean_type_node, lhs, rhs);
}

/* Creates the test for the condition in STMT.  */

static tree
graphite_create_guard_cond_expr (struct clast_guard *stmt,
				 ivs_params_p ip)
{
  tree cond = NULL;
  int i;

  for (i = 0; i < stmt->n; i++)
    {
      tree eq = graphite_translate_clast_equation (&stmt->eq[i], ip);

      if (cond)
	cond = fold_build2 (TRUTH_AND_EXPR, TREE_TYPE (eq), cond, eq);
      else
	cond = eq;
    }

  return cond;
}

/* Creates a new if region corresponding to Cloog's guard.  */

static edge
graphite_create_new_guard (edge entry_edge, struct clast_guard *stmt,
			   ivs_params_p ip)
{
  tree cond_expr = graphite_create_guard_cond_expr (stmt, ip);
  edge exit_edge = create_empty_if_region_on_edge (entry_edge, cond_expr);
  return exit_edge;
}

/* Compute the lower bound LOW and upper bound UP for the induction
   variable at LEVEL for the statement PBB, based on the transformed
   scattering of PBB: T|I|G|Cst, with T the scattering transform, I
   the iteration domain, and G the context parameters.  */

static void
compute_bounds_for_level (poly_bb_p pbb, int level, mpz_t low, mpz_t up)
{
  ppl_Pointset_Powerset_C_Polyhedron_t ps;
  ppl_Linear_Expression_t le;

  combine_context_id_scat (&ps, pbb, false);

  /* Prepare the linear expression corresponding to the level that we
     want to maximize/minimize.  */
  {
    ppl_dimension_type dim = pbb_nb_scattering_transform (pbb)
      + pbb_dim_iter_domain (pbb) + pbb_nb_params (pbb);

    ppl_new_Linear_Expression_with_dimension (&le, dim);
    ppl_set_coef (le, psct_dynamic_dim (pbb, level), 1);
  }

  ppl_max_for_le_pointset (ps, le, up);
  ppl_min_for_le_pointset (ps, le, low);
  ppl_delete_Linear_Expression (le);
  ppl_delete_Pointset_Powerset_C_Polyhedron (ps);
}

/* Compute the type for the induction variable at LEVEL for the
   statement PBB, based on the transformed schedule of PBB.  */

static tree
compute_type_for_level (poly_bb_p pbb, int level)
{
  mpz_t low, up;
  tree type;

  mpz_init (low);
  mpz_init (up);

  compute_bounds_for_level (pbb, level, low, up);
  type = gcc_type_for_interval (low, up);

  mpz_clear (low);
  mpz_clear (up);
  return type;
}

/* Walks a CLAST and returns the first statement in the body of a
   loop.

   FIXME: This function should not be used to get a PBB in the STMT
   loop in order to find out the iteration domain of the loop: the
   counter example from Tobias is:

   | for (i = 0; i < 100; i++)
   |   {
   |     if (i == 0)
   |       S1;
   |     S2;
   |   }

   This function would return S1 whose iteration domain contains only
   one point "i = 0", whereas the iteration domain of S2 has 100 points.

   This should be implemented using some functionality existing in
   CLooG-ISL.  */

static struct clast_user_stmt *
clast_get_body_of_loop (struct clast_stmt *stmt)
{
  if (!stmt
      || CLAST_STMT_IS_A (stmt, stmt_user))
    return (struct clast_user_stmt *) stmt;

  if (CLAST_STMT_IS_A (stmt, stmt_for))
    return clast_get_body_of_loop (((struct clast_for *) stmt)->body);

  if (CLAST_STMT_IS_A (stmt, stmt_guard))
    return clast_get_body_of_loop (((struct clast_guard *) stmt)->then);

  if (CLAST_STMT_IS_A (stmt, stmt_block))
    return clast_get_body_of_loop (((struct clast_block *) stmt)->body);

  gcc_unreachable ();
}

/* Returns the type for the induction variable for the loop translated
   from STMT_FOR.  */

static tree
gcc_type_for_iv_of_clast_loop (struct clast_for *stmt_for, int level,
			       tree lb_type, tree ub_type)
{
  struct clast_stmt *stmt = (struct clast_stmt *) stmt_for;
  struct clast_user_stmt *body = clast_get_body_of_loop (stmt);
  CloogStatement *cs = body->statement;
  poly_bb_p pbb = (poly_bb_p) cloog_statement_usr (cs);

  return max_signed_precision_type (lb_type, max_precision_type
				    (ub_type, compute_type_for_level
				     (pbb, level)));
}

/* Creates a new LOOP corresponding to Cloog's STMT.  Inserts an
   induction variable for the new LOOP.  New LOOP is attached to CFG
   starting at ENTRY_EDGE.  LOOP is inserted into the loop tree and
   becomes the child loop of the OUTER_LOOP.  NEWIVS_INDEX binds
   CLooG's scattering name to the induction variable created for the
   loop of STMT.  The new induction variable is inserted in the NEWIVS
   vector and is of type TYPE.  */

static struct loop *
graphite_create_new_loop (edge entry_edge, struct clast_for *stmt,
			  loop_p outer, tree type, tree lb, tree ub,
			  int level, ivs_params_p ip)
{
  tree stride = gmp_cst_to_tree (type, stmt->stride);
  tree ivvar = create_tmp_var (type, "graphite_IV");
  tree iv, iv_after_increment;
  loop_p loop = create_empty_loop_on_edge
    (entry_edge, lb, stride, ub, ivvar, &iv, &iv_after_increment,
     outer ? outer : entry_edge->src->loop_father);

  add_referenced_var (ivvar);

  save_clast_name_index (ip->newivs_index, stmt->iterator,
			 VEC_length (tree, *(ip->newivs)), level);
  VEC_safe_push (tree, heap, *(ip->newivs), iv);
  return loop;
}

/* Inserts in iv_map a tuple (OLD_LOOP->num, NEW_NAME) for the
   induction variables of the loops around GBB in SESE.  */

static void
build_iv_mapping (VEC (tree, heap) *iv_map, struct clast_user_stmt *user_stmt,
		  ivs_params_p ip)
{
  struct clast_stmt *t;
  int depth = 0;
  CloogStatement *cs = user_stmt->statement;
  poly_bb_p pbb = (poly_bb_p) cloog_statement_usr (cs);
  gimple_bb_p gbb = PBB_BLACK_BOX (pbb);

  for (t = user_stmt->substitutions; t; t = t->next, depth++)
    {
      struct clast_expr *expr = (struct clast_expr *)
       ((struct clast_assignment *)t)->RHS;
      tree type = gcc_type_for_clast_expr (expr, ip);
      tree new_name = clast_to_gcc_expression (type, expr, ip);
      loop_p old_loop = gbb_loop_at_index (gbb, ip->region, depth);

      VEC_replace (tree, iv_map, old_loop->num, new_name);
    }
}

/* Construct bb_pbb_def with BB and PBB.  */

static bb_pbb_def *
new_bb_pbb_def (basic_block bb, poly_bb_p pbb)
{
  bb_pbb_def *bb_pbb_p;

  bb_pbb_p = XNEW (bb_pbb_def);
  bb_pbb_p->bb = bb;
  bb_pbb_p->pbb = pbb;

  return bb_pbb_p;
}

/* Mark BB with it's relevant PBB via hashing table BB_PBB_MAPPING.  */

static void
mark_bb_with_pbb (poly_bb_p pbb, basic_block bb, htab_t bb_pbb_mapping)
{
  bb_pbb_def tmp;
  PTR *x;

  tmp.bb = bb;
  x = htab_find_slot (bb_pbb_mapping, &tmp, INSERT);

  if (x && !*x)
    *x = new_bb_pbb_def (bb, pbb);
}

/* Find BB's related poly_bb_p in hash table BB_PBB_MAPPING.  */

static poly_bb_p
find_pbb_via_hash (htab_t bb_pbb_mapping, basic_block bb)
{
  bb_pbb_def tmp;
  PTR *slot;

  tmp.bb = bb;
  slot = htab_find_slot (bb_pbb_mapping, &tmp, NO_INSERT);

  if (slot && *slot)
    return ((bb_pbb_def *) *slot)->pbb;

  return NULL;
}

/* Check data dependency in LOOP at level LEVEL.
   BB_PBB_MAPPING is a basic_block and it's related poly_bb_p
   mapping.  */

static bool
dependency_in_loop_p (loop_p loop, htab_t bb_pbb_mapping, int level)
{
  unsigned i,j;
  basic_block *bbs = get_loop_body_in_dom_order (loop);

  for (i = 0; i < loop->num_nodes; i++)
    {
      poly_bb_p pbb1 = find_pbb_via_hash (bb_pbb_mapping, bbs[i]);

      if (pbb1 == NULL)
       continue;

      for (j = 0; j < loop->num_nodes; j++)
       {
	 poly_bb_p pbb2 = find_pbb_via_hash (bb_pbb_mapping, bbs[j]);

	 if (pbb2 == NULL)
	   continue;

	 if (dependency_between_pbbs_p (pbb1, pbb2, level))
	   {
	     free (bbs);
	     return true;
	   }
       }
    }

  free (bbs);

  return false;
}

/* Translates a clast user statement STMT to gimple.

   - NEXT_E is the edge where new generated code should be attached.
   - CONTEXT_LOOP is the loop in which the generated code will be placed
   - BB_PBB_MAPPING is is a basic_block and it's related poly_bb_p mapping.  */

static edge
translate_clast_user (struct clast_user_stmt *stmt, edge next_e,
		      htab_t bb_pbb_mapping, ivs_params_p ip)
{
  int i, nb_loops;
  basic_block new_bb;
  poly_bb_p pbb = (poly_bb_p) cloog_statement_usr (stmt->statement);
  gimple_bb_p gbb = PBB_BLACK_BOX (pbb);
  VEC (tree, heap) *iv_map;

  if (GBB_BB (gbb) == ENTRY_BLOCK_PTR)
    return next_e;

  nb_loops = number_of_loops ();
  iv_map = VEC_alloc (tree, heap, nb_loops);
  for (i = 0; i < nb_loops; i++)
    VEC_quick_push (tree, iv_map, NULL_TREE);

  build_iv_mapping (iv_map, stmt, ip);
  next_e = copy_bb_and_scalar_dependences (GBB_BB (gbb), ip->region,
					   next_e, iv_map);
  VEC_free (tree, heap, iv_map);

  new_bb = next_e->src;
  mark_bb_with_pbb (pbb, new_bb, bb_pbb_mapping);
  update_ssa (TODO_update_ssa);

  return next_e;
}

/* Creates a new if region protecting the loop to be executed, if the execution
   count is zero (lb > ub).  */

static edge
graphite_create_new_loop_guard (edge entry_edge, struct clast_for *stmt,
				int level, tree *type, tree *lb, tree *ub,
				ivs_params_p ip)
{
  tree cond_expr;
  edge exit_edge;
  tree lb_type = gcc_type_for_clast_expr (stmt->LB, ip);
  tree ub_type = gcc_type_for_clast_expr (stmt->UB, ip);

  *type = gcc_type_for_iv_of_clast_loop (stmt, level, lb_type, ub_type);
  *lb = clast_to_gcc_expression (*type, stmt->LB, ip);
  *ub = clast_to_gcc_expression (*type, stmt->UB, ip);

  /* When ub is simply a constant or a parameter, use lb <= ub.  */
  if (TREE_CODE (*ub) == INTEGER_CST || TREE_CODE (*ub) == SSA_NAME)
    cond_expr = fold_build2 (LE_EXPR, boolean_type_node, *lb, *ub);
  else
    {
      tree one = (POINTER_TYPE_P (*type)
		  ? size_one_node
		  : fold_convert (*type, integer_one_node));
      /* Adding +1 and using LT_EXPR helps with loop latches that have a
	 loop iteration count of "PARAMETER - 1".  For PARAMETER == 0 this becomes
	 2^k-1 due to integer overflow, and the condition lb <= ub is true,
	 even if we do not want this.  However lb < ub + 1 is false, as
	 expected.  */
      tree ub_one = fold_build2 (POINTER_TYPE_P (*type) ? POINTER_PLUS_EXPR
				 : PLUS_EXPR, *type, *ub, one);

      cond_expr = fold_build2 (LT_EXPR, boolean_type_node, *lb, ub_one);
    }

  exit_edge = create_empty_if_region_on_edge (entry_edge, cond_expr);

  return exit_edge;
}

static edge
translate_clast (loop_p, struct clast_stmt *, edge, htab_t, int, ivs_params_p);

/* Create the loop for a clast for statement.

   - NEXT_E is the edge where new generated code should be attached.
   - BB_PBB_MAPPING is is a basic_block and it's related poly_bb_p mapping.  */

static edge
translate_clast_for_loop (loop_p context_loop, struct clast_for *stmt,
			  edge next_e, htab_t bb_pbb_mapping, int level,
			  tree type, tree lb, tree ub, ivs_params_p ip)
{
  struct loop *loop = graphite_create_new_loop (next_e, stmt, context_loop,
						type, lb, ub, level, ip);
  edge last_e = single_exit (loop);
  edge to_body = single_succ_edge (loop->header);
  basic_block after = to_body->dest;

  /* Create a basic block for loop close phi nodes.  */
  last_e = single_succ_edge (split_edge (last_e));

  /* Translate the body of the loop.  */
  next_e = translate_clast (loop, stmt->body, to_body, bb_pbb_mapping,
			    level + 1, ip);
  redirect_edge_succ_nodup (next_e, after);
  set_immediate_dominator (CDI_DOMINATORS, next_e->dest, next_e->src);

  if (flag_loop_parallelize_all
      && !dependency_in_loop_p (loop, bb_pbb_mapping, level))
    loop->can_be_parallel = true;

  return last_e;
}

/* Translates a clast for statement STMT to gimple.  First a guard is created
   protecting the loop, if it is executed zero times.  In this guard we create
   the real loop structure.

   - NEXT_E is the edge where new generated code should be attached.
   - BB_PBB_MAPPING is is a basic_block and it's related poly_bb_p mapping.  */

static edge
translate_clast_for (loop_p context_loop, struct clast_for *stmt, edge next_e,
		     htab_t bb_pbb_mapping, int level, ivs_params_p ip)
{
  tree type, lb, ub;
  edge last_e = graphite_create_new_loop_guard (next_e, stmt, level, &type,
						&lb, &ub, ip);
  edge true_e = get_true_edge_from_guard_bb (next_e->dest);

  translate_clast_for_loop (context_loop, stmt, true_e, bb_pbb_mapping, level,
			    type, lb, ub, ip);
  return last_e;
}

/* Translates a clast guard statement STMT to gimple.

   - NEXT_E is the edge where new generated code should be attached.
   - CONTEXT_LOOP is the loop in which the generated code will be placed
   - BB_PBB_MAPPING is is a basic_block and it's related poly_bb_p mapping.  */

static edge
translate_clast_guard (loop_p context_loop, struct clast_guard *stmt,
		       edge next_e, htab_t bb_pbb_mapping, int level,
		       ivs_params_p ip)
{
  edge last_e = graphite_create_new_guard (next_e, stmt, ip);
  edge true_e = get_true_edge_from_guard_bb (next_e->dest);

  translate_clast (context_loop, stmt->then, true_e, bb_pbb_mapping, level, ip);
  return last_e;
}

/* Translates a CLAST statement STMT to GCC representation in the
   context of a SESE.

   - NEXT_E is the edge where new generated code should be attached.
   - CONTEXT_LOOP is the loop in which the generated code will be placed
   - BB_PBB_MAPPING is is a basic_block and it's related poly_bb_p mapping.  */

static edge
translate_clast (loop_p context_loop, struct clast_stmt *stmt, edge next_e,
		 htab_t bb_pbb_mapping, int level, ivs_params_p ip)
{
  if (!stmt)
    return next_e;

  if (CLAST_STMT_IS_A (stmt, stmt_root))
    ; /* Do nothing.  */

  else if (CLAST_STMT_IS_A (stmt, stmt_user))
    next_e = translate_clast_user ((struct clast_user_stmt *) stmt,
				   next_e, bb_pbb_mapping, ip);

  else if (CLAST_STMT_IS_A (stmt, stmt_for))
    next_e = translate_clast_for (context_loop, (struct clast_for *) stmt,
				  next_e, bb_pbb_mapping, level, ip);

  else if (CLAST_STMT_IS_A (stmt, stmt_guard))
    next_e = translate_clast_guard (context_loop, (struct clast_guard *) stmt,
				    next_e, bb_pbb_mapping, level, ip);

  else if (CLAST_STMT_IS_A (stmt, stmt_block))
    next_e = translate_clast (context_loop, ((struct clast_block *) stmt)->body,
			      next_e, bb_pbb_mapping, level, ip);
  else
    gcc_unreachable();

  recompute_all_dominators ();
  graphite_verify ();

  return translate_clast (context_loop, stmt->next, next_e, bb_pbb_mapping,
			  level, ip);
}

/* Free the SCATTERING domain list.  */

static void
free_scattering (CloogScatteringList *scattering)
{
  while (scattering)
    {
      CloogScattering *dom = cloog_scattering (scattering);
      CloogScatteringList *next = cloog_next_scattering (scattering);

      cloog_scattering_free (dom);
      free (scattering);
      scattering = next;
    }
}

/* Initialize Cloog's parameter names from the names used in GIMPLE.
   Initialize Cloog's iterator names, using 'graphite_iterator_%d'
   from 0 to scop_nb_loops (scop).  */

static void
initialize_cloog_names (scop_p scop, CloogProgram *prog)
{
  sese region = SCOP_REGION (scop);
  int i;
  int nb_iterators = scop_max_loop_depth (scop);
  int nb_scattering = cloog_program_nb_scattdims (prog);
  int nb_parameters = VEC_length (tree, SESE_PARAMS (region));
  char **iterators = XNEWVEC (char *, nb_iterators * 2);
  char **scattering = XNEWVEC (char *, nb_scattering);
  char **parameters= XNEWVEC (char *, nb_parameters);

  cloog_program_set_names (prog, cloog_names_malloc ());

  for (i = 0; i < nb_parameters; i++)
    {
      tree param = VEC_index (tree, SESE_PARAMS (region), i);
      const char *name = get_name (param);
      int len;

      if (!name)
	name = "T";

      len = strlen (name);
      len += 17;
      parameters[i] = XNEWVEC (char, len + 1);
      snprintf (parameters[i], len, "%s_%d", name, SSA_NAME_VERSION (param));
    }

  cloog_names_set_nb_parameters (cloog_program_names (prog), nb_parameters);
  cloog_names_set_parameters (cloog_program_names (prog), parameters);

  for (i = 0; i < nb_iterators; i++)
    {
      int len = 4 + 16;
      iterators[i] = XNEWVEC (char, len);
      snprintf (iterators[i], len, "git_%d", i);
    }

  cloog_names_set_nb_iterators (cloog_program_names (prog),
				nb_iterators);
  cloog_names_set_iterators (cloog_program_names (prog),
			     iterators);

  for (i = 0; i < nb_scattering; i++)
    {
      int len = 5 + 16;
      scattering[i] = XNEWVEC (char, len);
      snprintf (scattering[i], len, "scat_%d", i);
    }

  cloog_names_set_nb_scattering (cloog_program_names (prog),
				 nb_scattering);
  cloog_names_set_scattering (cloog_program_names (prog),
			      scattering);
}

/* Initialize a CLooG input file.  */

static FILE *
init_cloog_input_file (int scop_number)
{
  FILE *graphite_out_file;
  int len = strlen (dump_base_name);
  char *dumpname = XNEWVEC (char, len + 25);
  char *s_scop_number = XNEWVEC (char, 15);

  memcpy (dumpname, dump_base_name, len + 1);
  strip_off_ending (dumpname, len);
  sprintf (s_scop_number, ".%d", scop_number);
  strcat (dumpname, s_scop_number);
  strcat (dumpname, ".cloog");
  graphite_out_file = fopen (dumpname, "w+b");

  if (graphite_out_file == 0)
    fatal_error ("can%'t open %s for writing: %m", dumpname);

  free (dumpname);

  return graphite_out_file;
}

/* Build cloog program for SCoP.  */

static void
build_cloog_prog (scop_p scop, CloogProgram *prog,
                  CloogOptions *options)
{
  int i;
  int max_nb_loops = scop_max_loop_depth (scop);
  poly_bb_p pbb;
  CloogLoop *loop_list = NULL;
  CloogBlockList *block_list = NULL;
  CloogScatteringList *scattering = NULL;
  int nbs = 2 * max_nb_loops + 1;
  int *scaldims;

  cloog_program_set_context
    (prog, new_Cloog_Domain_from_ppl_Pointset_Powerset (SCOP_CONTEXT (scop),
      scop_nb_params (scop), cloog_state));
  nbs = unify_scattering_dimensions (scop);
  scaldims = (int *) xmalloc (nbs * (sizeof (int)));
  cloog_program_set_nb_scattdims (prog, nbs);
  initialize_cloog_names (scop, prog);

  FOR_EACH_VEC_ELT (poly_bb_p, SCOP_BBS (scop), i, pbb)
    {
      CloogStatement *stmt;
      CloogBlock *block;
      CloogDomain *dom;

      /* Dead code elimination: when the domain of a PBB is empty,
	 don't generate code for the PBB.  */
      if (ppl_Pointset_Powerset_C_Polyhedron_is_empty (PBB_DOMAIN (pbb)))
	continue;

      /* Build the new statement and its block.  */
      stmt = cloog_statement_alloc (cloog_state, pbb_index (pbb));
      dom = new_Cloog_Domain_from_ppl_Pointset_Powerset (PBB_DOMAIN (pbb),
                                                         scop_nb_params (scop),
                                                         cloog_state);
      block = cloog_block_alloc (stmt, 0, NULL, pbb_dim_iter_domain (pbb));
      cloog_statement_set_usr (stmt, pbb);

      /* Build loop list.  */
      {
        CloogLoop *new_loop_list = cloog_loop_malloc (cloog_state);
        cloog_loop_set_next (new_loop_list, loop_list);
        cloog_loop_set_domain (new_loop_list, dom);
        cloog_loop_set_block (new_loop_list, block);
        loop_list = new_loop_list;
      }

      /* Build block list.  */
      {
        CloogBlockList *new_block_list = cloog_block_list_malloc ();

        cloog_block_list_set_next (new_block_list, block_list);
        cloog_block_list_set_block (new_block_list, block);
        block_list = new_block_list;
      }

      /* Build scattering list.  */
      {
        /* XXX: Replace with cloog_domain_list_alloc(), when available.  */
        CloogScatteringList *new_scattering
	  = (CloogScatteringList *) xmalloc (sizeof (CloogScatteringList));
        ppl_Polyhedron_t scat;
	CloogScattering *dom;

	scat = PBB_TRANSFORMED_SCATTERING (pbb);
        dom = new_Cloog_Scattering_from_ppl_Polyhedron
          (scat, scop_nb_params (scop), pbb_nb_scattering_transform (pbb),
           cloog_state);

        cloog_set_next_scattering (new_scattering, scattering);
        cloog_set_scattering (new_scattering, dom);
        scattering = new_scattering;
      }
    }

  cloog_program_set_loop (prog, loop_list);
  cloog_program_set_blocklist (prog, block_list);

  for (i = 0; i < nbs; i++)
    scaldims[i] = 0 ;

  cloog_program_set_scaldims (prog, scaldims);

  /* Extract scalar dimensions to simplify the code generation problem.  */
  cloog_program_extract_scalars (prog, scattering, options);

  /* Dump a .cloog input file, if requested.  This feature is only
     enabled in the Graphite branch.  */
  if (0)
    {
      static size_t file_scop_number = 0;
      FILE *cloog_file = init_cloog_input_file (file_scop_number);

      cloog_program_dump_cloog (cloog_file, prog, scattering);
      ++file_scop_number;
    }

  /* Apply scattering.  */
  cloog_program_scatter (prog, scattering, options);
  free_scattering (scattering);

  /* Iterators corresponding to scalar dimensions have to be extracted.  */
  cloog_names_scalarize (cloog_program_names (prog), nbs,
			 cloog_program_scaldims (prog));

  /* Free blocklist.  */
  {
    CloogBlockList *next = cloog_program_blocklist (prog);

    while (next)
      {
        CloogBlockList *toDelete = next;
        next = cloog_block_list_next (next);
        cloog_block_list_set_next (toDelete, NULL);
        cloog_block_list_set_block (toDelete, NULL);
        cloog_block_list_free (toDelete);
      }
    cloog_program_set_blocklist (prog, NULL);
  }
}

/* Return the options that will be used in GLOOG.  */

static CloogOptions *
set_cloog_options (void)
{
  CloogOptions *options = cloog_options_malloc (cloog_state);

  /* Change cloog output language to C.  If we do use FORTRAN instead, cloog
     will stop e.g. with "ERROR: unbounded loops not allowed in FORTRAN.", if
     we pass an incomplete program to cloog.  */
  options->language = LANGUAGE_C;

  /* Enable complex equality spreading: removes dummy statements
     (assignments) in the generated code which repeats the
     substitution equations for statements.  This is useless for
     GLooG.  */
  options->esp = 1;

#ifdef CLOOG_ORG
  /* Silence CLooG to avoid failing tests due to debug output to stderr.  */
  options->quiet = 1;
#else
  /* Enable C pretty-printing mode: normalizes the substitution
     equations for statements.  */
  options->cpp = 1;
#endif

  /* Allow cloog to build strides with a stride width different to one.
     This example has stride = 4:

     for (i = 0; i < 20; i += 4)
       A  */
  options->strides = 1;

  /* Disable optimizations and make cloog generate source code closer to the
     input.  This is useful for debugging,  but later we want the optimized
     code.

     XXX: We can not disable optimizations, as loop blocking is not working
     without them.  */
  if (0)
    {
      options->f = -1;
      options->l = INT_MAX;
    }

  return options;
}

/* Prints STMT to STDERR.  */

void
print_clast_stmt (FILE *file, struct clast_stmt *stmt)
{
  CloogOptions *options = set_cloog_options ();

  clast_pprint (file, stmt, 0, options);
  cloog_options_free (options);
}

/* Prints STMT to STDERR.  */

DEBUG_FUNCTION void
debug_clast_stmt (struct clast_stmt *stmt)
{
  print_clast_stmt (stderr, stmt);
}

/* Translate SCOP to a CLooG program and clast.  These two
   representations should be freed together: a clast cannot be used
   without a program.  */

cloog_prog_clast
scop_to_clast (scop_p scop)
{
  CloogOptions *options = set_cloog_options ();
  cloog_prog_clast pc;

  /* Connect new cloog prog generation to graphite.  */
  pc.prog = cloog_program_malloc ();
  build_cloog_prog (scop, pc.prog, options);
  pc.prog = cloog_program_generate (pc.prog, options);
  pc.stmt = cloog_clast_create (pc.prog, options);

  cloog_options_free (options);
  return pc;
}

/* Prints to FILE the code generated by CLooG for SCOP.  */

void
print_generated_program (FILE *file, scop_p scop)
{
  CloogOptions *options = set_cloog_options ();

  cloog_prog_clast pc = scop_to_clast (scop);

  fprintf (file, "       (prog: \n");
  cloog_program_print (file, pc.prog);
  fprintf (file, "       )\n");

  fprintf (file, "       (clast: \n");
  clast_pprint (file, pc.stmt, 0, options);
  fprintf (file, "       )\n");

  cloog_options_free (options);
  cloog_clast_free (pc.stmt);
  cloog_program_free (pc.prog);
}

/* Prints to STDERR the code generated by CLooG for SCOP.  */

DEBUG_FUNCTION void
debug_generated_program (scop_p scop)
{
  print_generated_program (stderr, scop);
}

/* Add CLooG names to parameter index.  The index is used to translate
   back from CLooG names to GCC trees.  */

static void
create_params_index (htab_t index_table, CloogProgram *prog) {
  CloogNames* names = cloog_program_names (prog);
  int nb_parameters = cloog_names_nb_parameters (names);
  char **parameters = cloog_names_parameters (names);
  int i;

  for (i = 0; i < nb_parameters; i++)
    save_clast_name_index (index_table, parameters[i], i, i);
}

/* GIMPLE Loop Generator: generates loops from STMT in GIMPLE form for
   the given SCOP.  Return true if code generation succeeded.
   BB_PBB_MAPPING is a basic_block and it's related poly_bb_p mapping.
*/

bool
gloog (scop_p scop, htab_t bb_pbb_mapping)
{
  VEC (tree, heap) *newivs = VEC_alloc (tree, heap, 10);
  loop_p context_loop;
  sese region = SCOP_REGION (scop);
  ifsese if_region = NULL;
  htab_t newivs_index, params_index;
  cloog_prog_clast pc;
  struct ivs_params ip;

  timevar_push (TV_GRAPHITE_CODE_GEN);
  gloog_error = false;

  pc = scop_to_clast (scop);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nCLAST generated by CLooG: \n");
      print_clast_stmt (dump_file, pc.stmt);
      fprintf (dump_file, "\n");
    }

  recompute_all_dominators ();
  graphite_verify ();

  if_region = move_sese_in_condition (region);
  sese_insert_phis_for_liveouts (region,
				 if_region->region->exit->src,
				 if_region->false_region->exit,
				 if_region->true_region->exit);
  recompute_all_dominators ();
  graphite_verify ();

  context_loop = SESE_ENTRY (region)->src->loop_father;
  newivs_index = htab_create (10, clast_name_index_elt_info,
			      eq_clast_name_indexes, free);
  params_index = htab_create (10, clast_name_index_elt_info,
			      eq_clast_name_indexes, free);

  create_params_index (params_index, pc.prog);

  ip.newivs = &newivs;
  ip.newivs_index = newivs_index;
  ip.params = SESE_PARAMS (region);
  ip.params_index = params_index;
  ip.region = region;

  translate_clast (context_loop, pc.stmt, if_region->true_region->entry,
		   bb_pbb_mapping, 0, &ip);
  graphite_verify ();
  scev_reset ();
  recompute_all_dominators ();
  graphite_verify ();

  if (gloog_error)
    set_ifsese_condition (if_region, integer_zero_node);

  free (if_region->true_region);
  free (if_region->region);
  free (if_region);

  htab_delete (newivs_index);
  htab_delete (params_index);
  VEC_free (tree, heap, newivs);
  cloog_clast_free (pc.stmt);
  cloog_program_free (pc.prog);
  timevar_pop (TV_GRAPHITE_CODE_GEN);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      loop_p loop;
      loop_iterator li;
      int num_no_dependency = 0;

      FOR_EACH_LOOP (li, loop, 0)
	if (loop->can_be_parallel)
	  num_no_dependency++;

      fprintf (dump_file, "\n%d loops carried no dependency.\n",
	       num_no_dependency);
    }

  return !gloog_error;
}
#endif
