/* Subroutines common to both C and C++ pretty-printers.
   Copyright (C) 2002, 2003 Free Software Foundation, Inc.
   Contributed by Gabriel Dos Reis <gdr@integrable-solutions.net>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 59 Temple Place - Suite 330, Boston, MA
02111-1307, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "real.h"
#include "c-pretty-print.h"
#include "c-tree.h"

/* The pretty-printer code is primarily designed to closely follow
   (GNU) C and C++ grammars.  That is to be contrasted with spaghetti
   codes we used to have in the past.  Following a structured
   approach (preferaably the official grammars) is believed to make it
   much easier o add extensions and nifty pretty-printing effects that
   takes expresssion or declaration contexts into account.  */


#define pp_c_whitespace(PP)           \
   do {                               \
     pp_space (PP);                   \
     pp_base (PP)->padding = pp_none; \
   } while (0)
#define pp_c_maybe_whitespace(PP)            \
   do {                                      \
     if (pp_base (PP)->padding == pp_before) \
       pp_c_whitespace (PP);                 \
   } while (0)

#define pp_c_left_paren(PP)           \
  do {                                \
    pp_left_paren (PP);               \
    pp_base (PP)->padding = pp_none;  \
  } while (0)

#define pp_c_right_paren(PP)          \
  do {                                \
    pp_right_paren (PP);              \
    pp_base (PP)->padding = pp_none;  \
  } while (0)

#define pp_c_left_brace(PP)           \
  do {                                \
    pp_left_brace (PP);               \
    pp_base (PP)->padding = pp_none;  \
  } while (0)

#define pp_c_right_brace(PP)          \
  do {                                \
    pp_right_brace (PP);              \
    pp_base (PP)->padding = pp_none;  \
  } while (0)

#define pp_c_left_bracket(PP)         \
  do {                                \
    pp_left_bracket (PP);             \
    pp_base (PP)->padding = pp_none;  \
  } while (0)

#define pp_c_right_bracket(PP)        \
  do {                                \
    pp_right_bracket (PP);            \
    pp_base (PP)->padding = pp_none;  \
  } while (0)

#define pp_c_arrow(PP)                \
  do {                                \
    pp_arrow (PP);                    \
    pp_base (PP)->padding = pp_none;  \
  } while (0)

#define pp_c_star(PP)                 \
  do {                                \
    pp_star (PP);                     \
    pp_base (PP)->padding = pp_none;  \
  } while (0)

#define pp_c_dot(PP)                  \
  do {                                \
    pp_dot (PP);                      \
    pp_base (PP)->padding = pp_none;  \
  } while (0)

#define pp_c_semicolon(PP)            \
  do {                                \
    pp_semicolon (PP);                \
    pp_base (PP)->padding = pp_none;  \
  } while (0)

/* literal  */
static void pp_c_char (c_pretty_printer, int);
static void pp_c_primary_expression (c_pretty_printer, tree);

/* postfix-expression  */
static void pp_c_initializer_list (c_pretty_printer, tree);

static void pp_c_multiplicative_expression (c_pretty_printer, tree);
static void pp_c_additive_expression (c_pretty_printer, tree);
static void pp_c_shift_expression (c_pretty_printer, tree);
static void pp_c_relational_expression (c_pretty_printer, tree);
static void pp_c_equality_expression (c_pretty_printer, tree);
static void pp_c_and_expression (c_pretty_printer, tree);
static void pp_c_exclusive_or_expression (c_pretty_printer, tree);
static void pp_c_inclusive_or_expression (c_pretty_printer, tree);
static void pp_c_logical_and_expression (c_pretty_printer, tree);
static void pp_c_conditional_expression (c_pretty_printer, tree);
static void pp_c_assignment_expression (c_pretty_printer, tree);

/* declarations.  */
static void pp_c_declaration_specifiers (c_pretty_printer, tree);
static void pp_c_direct_abstract_declarator (c_pretty_printer, tree);
static void pp_c_init_declarator (c_pretty_printer, tree);
static void pp_c_simple_type_specifier (c_pretty_printer, tree);
static void pp_c_parameter_declaration (c_pretty_printer, tree);
static void pp_c_storage_class_specifier (c_pretty_printer, tree);
static void pp_c_function_specifier (c_pretty_printer, tree);



/* Declarations.  */

static void
pp_c_cv_qualifier (c_pretty_printer pp, const char *cv)
{
  const char *p = pp_last_position_in_text (pp);
  if (p != NULL && *p == '*')
    pp_c_whitespace (pp);
  pp_c_identifier (pp, cv);
}

/* C++ cv-qualifiers are called type-qualifiers in C.  Print out the
   cv-qualifiers of T.  If T is a declaration then it is the cv-qualifier
   of its type.  Take care of possible extensions.
   cv-qualifier:
       const
       volatile
       restrict
       __restrict__   */
void
pp_c_type_qualifier_list (c_pretty_printer pp, tree t)
{
   int qualifiers;
   
  if (!TYPE_P (t))
    t = TREE_TYPE (t);

  qualifiers = TYPE_QUALS (t);
  if (qualifiers & TYPE_QUAL_CONST)
    pp_c_cv_qualifier (pp, "const");
  if (qualifiers & TYPE_QUAL_VOLATILE)
    pp_c_cv_qualifier (pp, "volatile");
  if (qualifiers & TYPE_QUAL_RESTRICT)
    pp_c_cv_qualifier (pp, flag_isoc99 ? "restrict" : "__restrict__");
}

/* pointer:
      * type-qualifier-list(opt)
      * type-qualifier-list(opt) pointer  */
static void
pp_c_pointer (c_pretty_printer pp, tree t)
{
  if (!TYPE_P (t) && TREE_CODE (t) != TYPE_DECL)
    t = TREE_TYPE (t);
  switch (TREE_CODE (t))
    {
    case POINTER_TYPE:
      if (TREE_CODE (TREE_TYPE (t)) == POINTER_TYPE)
        pp_c_pointer (pp, TREE_TYPE (t));
      pp_c_star (pp);
      pp_c_type_qualifier_list (pp, t);
      break;

    default:
      pp_unsupported_tree (pp, t);
    }
}

/*
  simple-type-specifier:
     void
     char
     short
     int
     long
     float
     double
     signed
     unsigned
     _Bool                          -- C99
     _Complex                       -- C99
     _Imaginary                     -- C99
     typedef-name.

  GNU extensions.
  simple-type-specifier:
      __complex__
      __vector__   */
static void
pp_c_simple_type_specifier (c_pretty_printer ppi, tree t)
{
  enum tree_code code;

  if (DECL_P (t) && TREE_CODE (t) != TYPE_DECL)
    t = TREE_TYPE (t);

  code = TREE_CODE (t);
  switch (code)
    {
    case ERROR_MARK:
      pp_c_identifier (ppi, "<type-error>");
      break;

#if 0
    case UNKNOWN_TYPE:
      pp_c_identifier (ppi, "<unkown-type>");
      break;
#endif

    case IDENTIFIER_NODE:
      pp_c_tree_identifier (ppi, t);
      break;

    case VOID_TYPE:
    case BOOLEAN_TYPE:
    case CHAR_TYPE:
    case INTEGER_TYPE:
    case REAL_TYPE:
      pp_c_simple_type_specifier (ppi, TYPE_NAME (t));
      break;

    case COMPLEX_TYPE:
    case VECTOR_TYPE:
      pp_c_simple_type_specifier (ppi, TYPE_MAIN_VARIANT (TREE_TYPE (t)));
      if (code == COMPLEX_TYPE)
	pp_c_identifier (ppi, flag_isoc99 ? "_Complex" : "__complex__");
      else if (code == VECTOR_TYPE)
	pp_c_identifier (ppi, "__vector__");
      break;

    case TYPE_DECL:
      if (DECL_NAME (t))
	pp_id_expression (ppi, t);
      else
	pp_c_identifier (ppi, "<typedef-error>");
      break;

    case UNION_TYPE:
    case RECORD_TYPE:
    case ENUMERAL_TYPE:
      if (code == UNION_TYPE)
	pp_c_identifier (ppi, "union");
      else if (code == RECORD_TYPE)
	pp_c_identifier (ppi, "struct");
      else if (code == ENUMERAL_TYPE)
	pp_c_identifier (ppi, "enum");
      else
	pp_c_identifier (ppi, "<tag-error>");

      if (TYPE_NAME (t))
	pp_id_expression (ppi, TYPE_NAME (t));
      else
	pp_c_identifier (ppi, "<anonymous>");
      break;

    case POINTER_TYPE:
    case ARRAY_TYPE:
    case FUNCTION_TYPE:
      pp_c_simple_type_specifier (ppi, TREE_TYPE (t));
      break;

    default:
      pp_unsupported_tree (ppi, t);
      break;
    }
}

/* specifier-qualifier-list:
      type-specifier specifier-qualifier-list-opt
      cv-qualifier specifier-qualifier-list-opt


  Implementation note:  Because of the non-linearities in array or
  function declarations, this routinie prints not just the
  specifier-qualifier-list of such entities or types of such entities,
  but also the 'pointer' production part of their declarators.  The
  remaining part is done by pp_declarator or pp_c_abstract_declarator.  */
void
pp_c_specifier_qualifier_list (c_pretty_printer pp, tree t)
{
  if (TREE_CODE (t) != POINTER_TYPE)
    pp_c_type_qualifier_list (pp, t);
  switch (TREE_CODE (t))
    {
    case POINTER_TYPE:
      {
        /* Get the types-specifier of this type.  */
        tree pointee = TREE_TYPE (t);
        while (TREE_CODE (pointee) == POINTER_TYPE)
          pointee = TREE_TYPE (pointee);
        pp_c_specifier_qualifier_list (pp, pointee);
        if (TREE_CODE (pointee) == ARRAY_TYPE
            || TREE_CODE (pointee) == FUNCTION_TYPE)
          {
            pp_c_whitespace (pp);
            pp_c_left_paren (pp);
          }
        pp_c_pointer (pp, t);
        if (TREE_CODE (pointee) != FUNCTION_TYPE
            && TREE_CODE (pointee) != ARRAY_TYPE)
          pp_c_whitespace (pp);
      }
      break;

    case FUNCTION_TYPE:
    case ARRAY_TYPE:
      pp_c_specifier_qualifier_list (pp, TREE_TYPE (t));
      break;

    case VECTOR_TYPE:
    case COMPLEX_TYPE:
      pp_c_specifier_qualifier_list (pp, TREE_TYPE (t));
      pp_space (pp);
      pp_c_simple_type_specifier (pp, t);
      break;

    default:
      pp_c_simple_type_specifier (pp, t);
      break;
    }
}

/* parameter-type-list:
      parameter-list
      parameter-list , ...

   parameter-list:
      parameter-declaration
      parameter-list , parameter-declaration

   parameter-declaration:
      declaration-specifiers declarator
      declaration-specifiers abstract-declarator(opt)   */
static void
pp_c_parameter_type_list (c_pretty_printer pp, tree t)
{
  pp_c_left_paren (pp);
  if (t == void_list_node)
    pp_c_identifier (pp, "void");
  else
    {
      bool first = true;
      bool want_parm_decl = t && DECL_P (t);
      for ( ; t != NULL && t != void_list_node; t = TREE_CHAIN (t))
        {
          if (!first)
            pp_separate_with (pp, ',');
          first = false;
          pp_declaration_specifiers (pp, want_parm_decl ? t : TREE_VALUE (t));
          if (want_parm_decl)
            pp_declarator (pp, t);
          else
            pp_abstract_declarator (pp, TREE_VALUE (t));
        }
    }
  pp_c_right_paren (pp);
}

/* abstract-declarator:
      pointer
      pointer(opt) direct-abstract-declarator  */
static inline void
pp_c_abstract_declarator (c_pretty_printer pp, tree t)
{
  if (TREE_CODE (t) == POINTER_TYPE)
    {
      if (TREE_CODE (TREE_TYPE (t)) == ARRAY_TYPE
          || TREE_CODE (TREE_TYPE (t)) == FUNCTION_TYPE)
        pp_c_right_paren (pp);
      t = TREE_TYPE (t);
    }

  pp_c_direct_abstract_declarator (pp, t);
}

/* direct-abstract-declarator:
      ( abstract-declarator )
      direct-abstract-declarator(opt) [ assignment-expression(opt) ]
      direct-abstract-declarator(opt) [ * ]
      direct-abstract-declarator(opt) ( parameter-type-list(opt) )  */
static void
pp_c_direct_abstract_declarator (c_pretty_printer pp, tree t)
{
  switch (TREE_CODE (t))
    {
    case POINTER_TYPE:
      pp_c_abstract_declarator (pp, t);
      break;
      
    case FUNCTION_TYPE:
      pp_c_parameter_type_list (pp, TYPE_ARG_TYPES (t));
      pp_c_direct_abstract_declarator (pp, TREE_TYPE (t));
      break;

    case ARRAY_TYPE:
      pp_c_left_bracket (pp);
      if (TYPE_DOMAIN (t))
        pp_c_expression (pp, TYPE_MAX_VALUE (TYPE_DOMAIN (t)));
      pp_c_right_bracket (pp);
      pp_c_direct_abstract_declarator (pp, TREE_TYPE (t));
      break;

    case IDENTIFIER_NODE:
    case VOID_TYPE:
    case BOOLEAN_TYPE:
    case INTEGER_TYPE:
    case REAL_TYPE:
    case ENUMERAL_TYPE:
    case RECORD_TYPE:
    case UNION_TYPE:
    case VECTOR_TYPE:
    case COMPLEX_TYPE:
    case TYPE_DECL:
      break;
      
    default:
      pp_unsupported_tree (pp, t);
      break;
    }
}

void
pp_c_type_id (c_pretty_printer ppi, tree t)
{
  pp_c_specifier_qualifier_list (ppi, t);
  pp_abstract_declarator (ppi, t);
}

static inline void
pp_c_storage_class_specifier (c_pretty_printer pp, tree t)
{
  if (TREE_CODE (t) == TYPE_DECL)
    pp_c_identifier (pp, "typedef");
  else if (DECL_P (t))
    {
      if (DECL_REGISTER (t))
        pp_c_identifier (pp, "register");
      else if (TREE_STATIC (t) && TREE_CODE (t) == VAR_DECL)
        pp_c_identifier (pp, "static");
    }
}

static inline void
pp_c_function_specifier (c_pretty_printer pp, tree t)
{
  if (TREE_CODE (t) == FUNCTION_DECL && DECL_DECLARED_INLINE_P (t))
    pp_c_identifier (pp, "inline");
}

/* declaration-specifiers:
      storage-class-specifier declaration-specifiers(opt)
      type-specifier declaration-specifiers(opt)
      type-qualifier declaration-specifiers(opt)
      function-specifier declaration-specifiers(opt)  */
static inline void
pp_c_declaration_specifiers (c_pretty_printer pp, tree t)
{
  pp_storage_class_specifier (pp, t);
  pp_function_specifier (pp, t);
  pp_c_specifier_qualifier_list (pp, DECL_P (t) ?  TREE_TYPE (t) : t);
}

/* direct-declarator
      identifier
      ( declarator )
      direct-declarator [ type-qualifier-list(opt) assignment-expression(opt) ]
      direct-declarator [ static type-qualifier-list(opt) assignment-expression(opt)]
      direct-declarator [ type-qualifier-list static assignment-exression ]
      direct-declarator [ type-qualifier-list * ]
      direct-declaratpr ( parameter-type-list )
      direct-declarator ( identifier-list(opt) )  */
void
pp_c_direct_declarator (c_pretty_printer pp, tree t)
{
  switch (TREE_CODE (t))
    {
    case VAR_DECL:
    case PARM_DECL:
    case TYPE_DECL:
    case FIELD_DECL:
    case LABEL_DECL:
      pp_c_tree_identifier (pp, DECL_NAME (t));
    case ARRAY_TYPE:
    case POINTER_TYPE:
      pp_abstract_declarator (pp, TREE_TYPE (t));
      break;

    case FUNCTION_TYPE:
      pp_c_parameter_type_list (pp, TYPE_ARG_TYPES (t));
      pp_c_abstract_declarator (pp, TREE_TYPE (t));
      break;

    case FUNCTION_DECL:
      pp_c_tree_identifier (pp, DECL_NAME (t));
      if (pp_c_base (pp)->flags & pp_c_flag_abstract)
        pp_c_abstract_declarator (pp, TREE_TYPE (t));
      else
        {
          pp_c_parameter_type_list (pp, DECL_ARGUMENTS (t));
          pp_c_abstract_declarator (pp, TREE_TYPE (TREE_TYPE (t)));
        }
      break;

    case INTEGER_TYPE:
    case REAL_TYPE:
    case ENUMERAL_TYPE:
    case UNION_TYPE:
    case RECORD_TYPE:
      break;

    default:
      pp_unsupported_tree (pp, t);
      break;
    }
}


/* declarator:
      pointer(opt)  direct-declarator   */
void
pp_c_declarator (c_pretty_printer pp, tree t)
{
  switch (TREE_CODE (t))
    {
    case INTEGER_TYPE:
    case REAL_TYPE:
    case ENUMERAL_TYPE:
    case UNION_TYPE:
    case RECORD_TYPE:
      break;

    case VAR_DECL:
    case PARM_DECL:
    case FIELD_DECL:
    case ARRAY_TYPE:
    case FUNCTION_TYPE:
    case FUNCTION_DECL:
    case TYPE_DECL:
      pp_direct_declarator (pp, t);
    break;

    
    default:
      pp_unsupported_tree (pp, t);
      break;
    }
}

/* init-declarator:
      declarator:
      declarator = initializer   */
static inline void
pp_c_init_declarator (c_pretty_printer pp, tree t)
{
  pp_declarator (pp, t);
  if (DECL_INITIAL (t))
    {
      pp_space (pp);
      pp_equal (pp);
      pp_space (pp);
      pp_c_initializer (pp, DECL_INITIAL (t));
    }
}

/* declaration:
      declaration-specifiers init-declarator-list(opt) ;  */

void
pp_c_declaration (c_pretty_printer pp, tree t)
{
  pp_declaration_specifiers (pp, t);
  pp_c_init_declarator (pp, t);
}

static void
pp_c_parameter_declaration (c_pretty_printer pp, tree t)
{
  pp_unsupported_tree (pp, t);
}

/* Pretty-print ATTRIBUTES using GNU C extension syntax.  */
void
pp_c_attributes (c_pretty_printer pp, tree attributes)
{
  if (attributes == NULL_TREE)
    return;

  pp_c_identifier (pp, "__attribute__");
  pp_c_left_paren (pp);
  pp_c_left_paren (pp);
  for (; attributes != NULL_TREE; attributes = TREE_CHAIN (attributes))
    {
      pp_tree_identifier (pp, TREE_PURPOSE (attributes));
      if (TREE_VALUE (attributes))
	{
	  pp_c_left_paren (pp);
	  pp_c_expression_list (pp, TREE_VALUE (attributes));
	  pp_c_right_paren (pp);
	}

      if (TREE_CHAIN (attributes))
	pp_separate_with (pp, ',');
    }
  pp_c_right_paren (pp);
  pp_c_right_paren (pp);
}

/* function-definition:
      declaration-specifiers declarator compound-statement  */
void
pp_c_function_definition (c_pretty_printer pp, tree t)
{
  pp_declaration_specifiers (pp, t);
  pp_declarator (pp, t);
  pp_needs_newline (pp) = true;
  pp_statement (pp, DECL_SAVED_TREE (t));
  pp_newline (pp);
  pp_flush (pp);
}


/* Expressions.  */

/* Print out a c-char.  */
static void
pp_c_char (c_pretty_printer ppi, int c)
{
  switch (c)
    {
    case TARGET_NEWLINE:
      pp_string (ppi, "\\n");
      break;
    case TARGET_TAB:
      pp_string (ppi, "\\t");
      break;
    case TARGET_VT:
      pp_string (ppi, "\\v");
      break;
    case TARGET_BS:
      pp_string (ppi, "\\b");
      break;
    case TARGET_CR:
      pp_string (ppi, "\\r");
      break;
    case TARGET_FF:
      pp_string (ppi, "\\f");
      break;
    case TARGET_BELL:
      pp_string (ppi, "\\a");
      break;
    case '\\':
      pp_string (ppi, "\\\\");
      break;
    case '\'':
      pp_string (ppi, "\\'");
      break;
    case '\"':
      pp_string (ppi, "\\\"");
      break;
    default:
      if (ISPRINT (c))
	pp_character (ppi, c);
      else
	pp_scalar (ppi, "\\%03o", (unsigned) c);
      break;
    }
}

/* Print out a STRING literal.  */
void
pp_c_string_literal (c_pretty_printer ppi, tree s)
{
  const char *p = TREE_STRING_POINTER (s);
  int n = TREE_STRING_LENGTH (s) - 1;
  int i;
  pp_doublequote (ppi);
  for (i = 0; i < n; ++i)
    pp_c_char (ppi, p[i]);
  pp_doublequote (ppi);
}

static void
pp_c_integer_constant (c_pretty_printer pp, tree i)
{
  if (host_integerp (i, 0))
    pp_wide_integer (pp, TREE_INT_CST_LOW (i));
  else
    {
      if (tree_int_cst_sgn (i) < 0)
        {
          pp_c_char (pp, '-');
          i = build_int_2 (-TREE_INT_CST_LOW (i),
                           ~TREE_INT_CST_HIGH (i) + !TREE_INT_CST_LOW (i));
        }
      sprintf (pp_buffer (pp)->digit_buffer,
               HOST_WIDE_INT_PRINT_DOUBLE_HEX,
               TREE_INT_CST_HIGH (i), TREE_INT_CST_LOW (i));
      pp_string (pp, pp_buffer (pp)->digit_buffer);
    }
}

/* Print out a CHARACTER literal.  */
static inline void
pp_c_character_constant (c_pretty_printer pp, tree c)
{
  tree type = TREE_TYPE (c);
  if (type == wchar_type_node)
    pp_character (pp, 'L'); 
  pp_quote (pp);
  if (host_integerp (c, TREE_UNSIGNED (type)))
    pp_c_char (pp, tree_low_cst (c, TREE_UNSIGNED (type)));
  else
    pp_scalar (pp, "\\x%x", (unsigned) TREE_INT_CST_LOW (c));
  pp_quote (pp);
}

/* Print out a BOOLEAN literal.  */
static void
pp_c_bool_constant (c_pretty_printer pp, tree b)
{
  if (b == boolean_false_node)
    {
      if (c_dialect_cxx ())
	pp_c_identifier (pp, "false");
      else if (flag_isoc99)
	pp_c_identifier (pp, "_False");
      else
	pp_unsupported_tree (pp, b);
    }
  else if (b == boolean_true_node)
    {
      if (c_dialect_cxx ())
	pp_c_identifier (pp, "true");
      else if (flag_isoc99)
	pp_c_identifier (pp, "_True");
      else
	pp_unsupported_tree (pp, b);
    }
  else if (TREE_CODE (b) == INTEGER_CST)
    pp_c_integer_constant (pp, b);
  else
    pp_unsupported_tree (pp, b);
}

/* Attempt to print out an ENUMERATOR.  Return true on success.  Else return
   false; that means the value was obtained by a cast, in which case
   print out the type-id part of the cast-expression -- the casted value
   is then printed by pp_c_integer_literal.  */
static bool
pp_c_enumeration_constant (c_pretty_printer ppi, tree e)
{
  bool value_is_named = true;
  tree type = TREE_TYPE (e);
  tree value;

  /* Find the name of this constant.  */
  for (value = TYPE_VALUES (type);
       value != NULL_TREE && !tree_int_cst_equal (TREE_VALUE (value), e);
       value = TREE_CHAIN (value))
    ;

  if (value != NULL_TREE)
    pp_id_expression (ppi, TREE_PURPOSE (value));
  else
    {
      /* Value must have been cast.  */
      pp_c_left_paren (ppi);
      pp_type_id (ppi, type);
      pp_c_right_paren (ppi);
      value_is_named = false;
    }

  return value_is_named;
}

/* Print out a REAL value.  */
static inline void
pp_c_floating_constant (c_pretty_printer pp, tree r)
{
  real_to_decimal (pp_buffer (pp)->digit_buffer, &TREE_REAL_CST (r),
		   sizeof (pp_buffer (pp)->digit_buffer), 0, 1);
  pp_string (pp, pp_buffer(pp)->digit_buffer);
}

/* constant:
      integer-constant
      floating-constant
      enumeration-constant
      chatracter-constant   */
void
pp_c_constant (c_pretty_printer pp, tree e)
{
  switch (TREE_CODE (e))
    {
    case INTEGER_CST:
      {
        tree type = TREE_TYPE (e);
        if (type == boolean_type_node)
          pp_c_bool_constant (pp, e);
        else if (type == char_type_node)
          pp_c_character_constant (pp, e);
        else if (TREE_CODE (type) == ENUMERAL_TYPE
                 && pp_c_enumeration_constant (pp, e))
          ; 
        else 
          pp_c_integer_constant (pp, e);
      }
      break;

    case REAL_CST:
      pp_c_floating_constant (pp, e);
      break;

    case STRING_CST:
      pp_c_string_literal (pp, e);
      break;

    default:
      pp_unsupported_tree (pp, e);
      break;
    }
}

void
pp_c_identifier (c_pretty_printer pp, const char *id)
{
  pp_c_maybe_whitespace (pp);            
  pp_identifier (pp, id);  
  pp_base (pp)->padding = pp_before;
}

/* Pretty-print a C primary-expression.
   primary-expression:
      identifier
      constant
      string-literal
      ( expression )   */
static void
pp_c_primary_expression (c_pretty_printer ppi, tree e)
{
  switch (TREE_CODE (e))
    {
    case VAR_DECL:
    case PARM_DECL:
    case FIELD_DECL:
    case CONST_DECL:
    case FUNCTION_DECL:
    case LABEL_DECL:
      e = DECL_NAME (e);
      /* Fall through.  */
    case IDENTIFIER_NODE:
      pp_c_tree_identifier (ppi, e);
      break;

    case ERROR_MARK:
      pp_c_identifier (ppi, "<erroneous-expression>");
      break;

    case RESULT_DECL:
      pp_c_identifier (ppi, "<return-value>");
      break;

    case INTEGER_CST:
    case REAL_CST:
    case STRING_CST:
      pp_c_constant (ppi, e);
      break;

    case TARGET_EXPR:
      pp_c_left_paren (ppi);
      pp_c_identifier (ppi, "__builtin_memcpy");
      pp_c_left_paren (ppi);
      pp_ampersand (ppi);
      pp_primary_expression (ppi, TREE_OPERAND (e, 0));
      pp_separate_with (ppi, ',');
      pp_ampersand (ppi);
      pp_initializer (ppi, TREE_OPERAND (e, 1));
      if (TREE_OPERAND (e, 2))
	{
	  pp_separate_with (ppi, ',');
	  pp_c_expression (ppi, TREE_OPERAND (e, 2));
	}
      pp_c_right_paren (ppi);

    case STMT_EXPR:
      pp_c_left_paren (ppi);
      pp_statement (ppi, STMT_EXPR_STMT (e));
      pp_c_right_paren (ppi);
      break;

    default:
      /* FIXME:  Make sure we won't get into an infinie loop.  */
      pp_c_left_paren (ppi);
      pp_c_expression (ppi, e);
      pp_c_right_paren (ppi);
      break;
    }
}

/* Print out a C initializer -- also support C compound-literals.
   initializer:
      assignment-expression:
      { initializer-list }
      { initializer-list , }   */

void
pp_c_initializer (c_pretty_printer ppi, tree e)
{
  if (TREE_CODE (e) == CONSTRUCTOR)
    {
      enum tree_code code = TREE_CODE (TREE_TYPE (e));
      if (code == RECORD_TYPE || code == UNION_TYPE || code == ARRAY_TYPE)
	{
	  pp_c_left_brace (ppi);
	  pp_c_initializer_list (ppi, e);
	  pp_c_right_brace (ppi);
	}
      else
	pp_unsupported_tree (ppi, TREE_OPERAND (e, 1));
    }
  else
    pp_assignment_expression (ppi, e);
}

/* initializer-list:
      designation(opt) initializer
      initializer-list , designation(opt) initializer

   designation:
      designator-list =

   designator-list:
      designator
      designator-list designator

   designator:
      [ constant-expression ]
      identifier   */
static void
pp_c_initializer_list (c_pretty_printer ppi, tree e)
{
  tree type = TREE_TYPE (e);
  const enum tree_code code = TREE_CODE (type);

  if (code == RECORD_TYPE || code == UNION_TYPE || code == ARRAY_TYPE)
    {
      tree init = TREE_OPERAND (e, 0);
      for (; init != NULL_TREE; init = TREE_CHAIN (init))
	{
	  if (code == RECORD_TYPE || code == UNION_TYPE)
	    {
	      pp_c_dot (ppi);
	      pp_c_primary_expression (ppi, TREE_PURPOSE (init));
	    }
	  else
	    {
	      pp_c_left_bracket (ppi);
	      if (TREE_PURPOSE (init))
		pp_c_constant (ppi, TREE_PURPOSE (init));
	      pp_c_right_bracket (ppi);
	    }
	  pp_c_whitespace (ppi);
	  pp_equal (ppi);
	  pp_c_whitespace (ppi);
	  pp_initializer (ppi, TREE_VALUE (init));
	  if (TREE_CHAIN (init))
	    pp_separate_with (ppi, ',');
	}
    }
  else
    pp_unsupported_tree (ppi, type);
}

/*  This is a convenient function, used to bridge gap between C and C++
    grammars.

    id-expression:
       identifier  */
void
pp_c_id_expression (c_pretty_printer pp, tree t)
{
  switch (TREE_CODE (t))
    {
    case VAR_DECL:
    case PARM_DECL:
    case CONST_DECL:
    case TYPE_DECL:
    case FUNCTION_DECL:
    case FIELD_DECL:
    case LABEL_DECL:
      t = DECL_NAME (t);
    case IDENTIFIER_NODE:
      pp_c_tree_identifier (pp, t);
      break;

    default:
      pp_unsupported_tree (pp, t);
      break;
    }
}

/* postfix-expression:
      primary-expression
      postfix-expression [ expression ]
      postfix-expression ( argument-expression-list(opt) )
      postfix-expression . identifier
      postfix-expression -> identifier
      postfix-expression ++
      postfix-expression --
      ( type-name ) { initializer-list }
      ( type-name ) { initializer-list , }  */
void
pp_c_postfix_expression (c_pretty_printer ppi, tree e)
{
  enum tree_code code = TREE_CODE (e);
  switch (code)
    {
    case POSTINCREMENT_EXPR:
    case POSTDECREMENT_EXPR:
      pp_postfix_expression (ppi, TREE_OPERAND (e, 0));
      pp_identifier (ppi, code == POSTINCREMENT_EXPR ? "++" : "--");
      break;

    case ARROW_EXPR:
      pp_postfix_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_arrow (ppi);
      break;

    case ARRAY_REF:
      pp_postfix_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_left_bracket (ppi);
      pp_c_expression (ppi, TREE_OPERAND (e, 1));
      pp_c_right_bracket (ppi);
      break;

    case CALL_EXPR:
      pp_postfix_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_left_paren (ppi);
      pp_c_expression_list (ppi, TREE_OPERAND (e, 1));
      pp_c_right_paren (ppi);
      break;

    case ABS_EXPR:
    case FFS_EXPR:
      pp_c_identifier (ppi,
		       code == ABS_EXPR ? "__builtin_abs" : "__builtin_ffs");
      pp_c_left_paren (ppi);
      pp_c_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_right_paren (ppi);
      break;

    case COMPONENT_REF:
      {
	tree object = TREE_OPERAND (e, 0);
	if (TREE_CODE (object) == INDIRECT_REF)
	  {
	    pp_postfix_expression (ppi, TREE_OPERAND (object, 0));
	    pp_c_arrow (ppi);
	  }
	else
	  {
	    pp_postfix_expression (ppi, object);
	    pp_c_dot (ppi);
	  }
	pp_c_expression (ppi, TREE_OPERAND (e, 1));
      }
      break;

    case COMPLEX_CST:
    case VECTOR_CST:
    case COMPLEX_EXPR:
      pp_c_left_paren (ppi);
      pp_type_id (ppi, TREE_TYPE (e));
      pp_c_right_paren (ppi);
      pp_c_left_brace (ppi);

      if (code == COMPLEX_CST)
	{
	  pp_c_expression (ppi, TREE_REALPART (e));
	  pp_separate_with (ppi, ',');
	  pp_c_expression (ppi, TREE_IMAGPART (e));
	}
      else if (code == VECTOR_CST)
	pp_c_expression_list (ppi, TREE_VECTOR_CST_ELTS (e));
      else if (code == COMPLEX_EXPR)
	{
	  pp_c_expression (ppi, TREE_OPERAND (e, 0));
	  pp_separate_with (ppi, ',');
	  pp_c_expression (ppi, TREE_OPERAND (e, 1));
	}

      pp_c_right_brace (ppi);
      break;

    case COMPOUND_LITERAL_EXPR:
      e = DECL_INITIAL (e);
      /* Fall through.  */
    case CONSTRUCTOR:
      pp_initializer (ppi, e);
      break;

    case VA_ARG_EXPR:
      pp_c_identifier (ppi, "__builtin_va_arg");
      pp_c_left_paren (ppi);
      pp_assignment_expression (ppi, TREE_OPERAND (e, 0));
      pp_separate_with (ppi, ',');
      pp_type_id (ppi, TREE_TYPE (e));
      pp_c_right_paren (ppi);
      break;

    case ADDR_EXPR:
      if (TREE_CODE (TREE_OPERAND (e, 0)) == FUNCTION_DECL)
        {
          pp_c_id_expression (ppi, TREE_OPERAND (e, 0));
          break;
        }
      /* else fall through.   */

    default:
      pp_primary_expression (ppi, e);
      break;
    }
}

/* Print out an expression-list; E is expected to be a TREE_LIST  */
void
pp_c_expression_list (c_pretty_printer ppi, tree e)
{
  for (; e != NULL_TREE; e = TREE_CHAIN (e))
    {
      pp_c_assignment_expression (ppi, TREE_VALUE (e));
      if (TREE_CHAIN (e))
	pp_separate_with (ppi, ',');
    }
}

/* unary-expression:
      postfix-expression
      ++ cast-expression
      -- cast-expression
      unary-operator cast-expression
      sizeof unary-expression
      sizeof ( type-id )

  unary-operator: one of
      * &  + - ! ~
      
   GNU extensions.
   unary-expression:
      __alignof__ unary-expression
      __alignof__ ( type-id )
      __real__ unary-expression
      __imag__ unary-expression  */
void
pp_c_unary_expression (c_pretty_printer ppi, tree e)
{
  enum tree_code code = TREE_CODE (e);
  switch (code)
    {
    case PREINCREMENT_EXPR:
    case PREDECREMENT_EXPR:
      pp_identifier (ppi, code == PREINCREMENT_EXPR ? "++" : "--");
      pp_c_unary_expression (ppi, TREE_OPERAND (e, 0));
      break;

    case ADDR_EXPR:
    case INDIRECT_REF:
    case NEGATE_EXPR:
    case BIT_NOT_EXPR:
    case TRUTH_NOT_EXPR:
    case CONJ_EXPR:
      /* String literal are used by address.  */
      if (code == ADDR_EXPR && TREE_CODE (TREE_OPERAND (e, 0)) != STRING_CST)
	pp_ampersand (ppi);
      else if (code == INDIRECT_REF)
	pp_c_star (ppi);
      else if (code == NEGATE_EXPR)
	pp_minus (ppi);
      else if (code == BIT_NOT_EXPR || code == CONJ_EXPR)
	pp_complement (ppi);
      else if (code == TRUTH_NOT_EXPR)
	pp_exclamation (ppi);
      pp_c_cast_expression (ppi, TREE_OPERAND (e, 0));
      break;

    case SIZEOF_EXPR:
    case ALIGNOF_EXPR:
      pp_c_identifier (ppi, code == SIZEOF_EXPR ? "sizeof" : "__alignof__");
      pp_c_whitespace (ppi);
      if (TYPE_P (TREE_OPERAND (e, 0)))
	{
	  pp_c_left_paren (ppi);
	  pp_type_id (ppi, TREE_OPERAND (e, 0));
	  pp_c_right_paren (ppi);
	}
      else
	pp_unary_expression (ppi, TREE_OPERAND (e, 0));
      break;

    case REALPART_EXPR:
    case IMAGPART_EXPR:
      pp_c_identifier (ppi, code == REALPART_EXPR ? "__real__" : "__imag__");
      pp_c_whitespace (ppi);
      pp_unary_expression (ppi, TREE_OPERAND (e, 0));
      break;

    default:
      pp_postfix_expression (ppi, e);
      break;
    }
}

void
pp_c_cast_expression (c_pretty_printer ppi, tree e)
{
  switch (TREE_CODE (e))
    {
    case FLOAT_EXPR:
    case FIX_TRUNC_EXPR:
    case CONVERT_EXPR:
      pp_c_left_paren (ppi);
      pp_type_id (ppi, TREE_TYPE (e));
      pp_c_right_paren (ppi);
      pp_c_cast_expression (ppi, TREE_OPERAND (e, 0));
      break;

    default:
      pp_unary_expression (ppi, e);
    }
}

static void
pp_c_multiplicative_expression (c_pretty_printer ppi, tree e)
{
  enum tree_code code = TREE_CODE (e);
  switch (code)
    {
    case MULT_EXPR:
    case TRUNC_DIV_EXPR:
    case TRUNC_MOD_EXPR:
      pp_multiplicative_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_whitespace (ppi);
      if (code == MULT_EXPR)
	pp_c_star (ppi);
      else if (code == TRUNC_DIV_EXPR)
	pp_slash (ppi);
      else
	pp_modulo (ppi);
      pp_c_whitespace (ppi);
      pp_c_cast_expression (ppi, TREE_OPERAND (e, 1));
      break;

    default:
      pp_c_cast_expression (ppi, e);
      break;
    }
}

static inline void
pp_c_additive_expression (c_pretty_printer ppi, tree e)
{
  enum tree_code code = TREE_CODE (e);
  switch (code)
    {
    case PLUS_EXPR:
    case MINUS_EXPR:
      pp_c_additive_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_whitespace (ppi);
      if (code == PLUS_EXPR)
	pp_plus (ppi);
      else
	pp_minus (ppi);
      pp_c_whitespace (ppi);
      pp_multiplicative_expression (ppi, TREE_OPERAND (e, 1)); 
      break;

    default:
      pp_multiplicative_expression (ppi, e);
      break;
    }
}

static inline void
pp_c_shift_expression (c_pretty_printer ppi, tree e)
{
  enum tree_code code = TREE_CODE (e);
  switch (code)
    {
    case LSHIFT_EXPR:
    case RSHIFT_EXPR:
      pp_c_shift_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_whitespace (ppi);
      pp_identifier (ppi, code == LSHIFT_EXPR ? "<<" : ">>");
      pp_c_whitespace (ppi);
      pp_c_additive_expression (ppi, TREE_OPERAND (e, 1));
      break;

    default:
      pp_c_additive_expression (ppi, e);
    }
}

static void
pp_c_relational_expression (c_pretty_printer ppi, tree e)
{
  enum tree_code code = TREE_CODE (e);
  switch (code)
    {
    case LT_EXPR:
    case GT_EXPR:
    case LE_EXPR:
    case GE_EXPR:
      pp_c_relational_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_whitespace (ppi);
      if (code == LT_EXPR)
	pp_less (ppi);
      else if (code == GT_EXPR)
	pp_greater (ppi);
      else if (code == LE_EXPR)
	pp_identifier (ppi, "<=");
      else if (code == GE_EXPR)
	pp_identifier (ppi, ">=");
      pp_c_whitespace (ppi);
      pp_c_shift_expression (ppi, TREE_OPERAND (e, 1));
      break;

    default:
      pp_c_shift_expression (ppi, e);
      break;
    }
}

static inline void
pp_c_equality_expression (c_pretty_printer ppi, tree e)
{
  enum tree_code code = TREE_CODE (e);
  switch (code)
    {
    case EQ_EXPR:
    case NE_EXPR:
      pp_c_equality_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_whitespace (ppi);
      pp_identifier (ppi, code == EQ_EXPR ? "==" : "!=");
      pp_c_whitespace (ppi);
      pp_c_relational_expression (ppi, TREE_OPERAND (e, 1));
      break;

    default:
      pp_c_relational_expression (ppi, e);
      break;
    }
}

static inline void
pp_c_and_expression (c_pretty_printer ppi, tree e)
{
  if (TREE_CODE (e) == BIT_AND_EXPR)
    {
      pp_c_and_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_whitespace (ppi);
      pp_ampersand (ppi);
      pp_c_whitespace (ppi);
      pp_c_equality_expression (ppi, TREE_OPERAND (e, 1));
    }
  else
    pp_c_equality_expression (ppi, e);
}

static inline void
pp_c_exclusive_or_expression (c_pretty_printer ppi, tree e)
{
  if (TREE_CODE (e) == BIT_XOR_EXPR)
    {
      pp_c_exclusive_or_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_maybe_whitespace (ppi);
      pp_carret (ppi);
      pp_c_whitespace (ppi);
      pp_c_and_expression (ppi, TREE_OPERAND (e, 1));
    }
  else
    pp_c_and_expression (ppi, e);
}

static inline void
pp_c_inclusive_or_expression (c_pretty_printer ppi, tree e)
{
  if (TREE_CODE (e) == BIT_IOR_EXPR)
    {
      pp_c_exclusive_or_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_whitespace (ppi);
      pp_bar (ppi);
      pp_c_whitespace (ppi);
      pp_c_exclusive_or_expression (ppi, TREE_OPERAND (e, 1));
    }
  else
    pp_c_exclusive_or_expression (ppi, e);
}

static inline void
pp_c_logical_and_expression (c_pretty_printer ppi, tree e)
{
  if (TREE_CODE (e) == TRUTH_ANDIF_EXPR)
    {
      pp_c_logical_and_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_whitespace (ppi);
      pp_identifier (ppi, "&&");
      pp_c_whitespace (ppi);
      pp_c_inclusive_or_expression (ppi, TREE_OPERAND (e, 1));
    }
  else
    pp_c_inclusive_or_expression (ppi, e);
}

void
pp_c_logical_or_expression (c_pretty_printer ppi, tree e)
{
  if (TREE_CODE (e) == TRUTH_ORIF_EXPR)
    {
      pp_c_logical_or_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_whitespace (ppi);
      pp_identifier (ppi, "||");
      pp_c_whitespace (ppi);
      pp_c_logical_and_expression (ppi, TREE_OPERAND (e, 1));
    }
  else
    pp_c_logical_and_expression (ppi, e);
}

static void
pp_c_conditional_expression (c_pretty_printer ppi, tree e)
{
  if (TREE_CODE (e) == COND_EXPR)
    {
      pp_c_logical_or_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_whitespace (ppi);
      pp_question (ppi);
      pp_c_whitespace (ppi);
      pp_c_expression (ppi, TREE_OPERAND (e, 1));
      pp_c_maybe_whitespace (ppi);
      pp_colon (ppi);
      pp_c_whitespace (ppi);
      pp_c_conditional_expression (ppi, TREE_OPERAND (e, 2));
    }
  else
    pp_c_logical_or_expression (ppi, e);
}


/* Pretty-print a C assignment-expression.  */
static void
pp_c_assignment_expression (c_pretty_printer ppi, tree e)
{
  if (TREE_CODE (e) == MODIFY_EXPR || TREE_CODE (e) == INIT_EXPR)
    {
      pp_c_unary_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_maybe_whitespace (ppi);
      pp_equal (ppi);
      pp_space (ppi);
      pp_c_assignment_expression (ppi, TREE_OPERAND (e, 1));
    }
  else
    pp_c_conditional_expression (ppi, e);
}

/* Pretty-print an expression.  */
void
pp_c_expression (c_pretty_printer ppi, tree e)
{
  switch (TREE_CODE (e))
    {
    case INTEGER_CST:
      pp_c_integer_constant (ppi, e);
      break;

    case REAL_CST:
      pp_c_floating_constant (ppi, e);
      break;

    case STRING_CST:
      pp_c_string_literal (ppi, e);
      break;

    case FUNCTION_DECL:
    case VAR_DECL:
    case CONST_DECL:
    case PARM_DECL:
    case RESULT_DECL:
    case FIELD_DECL:
    case LABEL_DECL:
    case ERROR_MARK:
    case TARGET_EXPR:
    case STMT_EXPR:
      pp_primary_expression (ppi, e);
      break;

    case POSTINCREMENT_EXPR:
    case POSTDECREMENT_EXPR:
    case ARROW_EXPR:
    case ARRAY_REF:
    case CALL_EXPR:
    case COMPONENT_REF:
    case COMPLEX_CST:
    case VECTOR_CST:
    case ABS_EXPR:
    case FFS_EXPR:
    case CONSTRUCTOR:
    case COMPOUND_LITERAL_EXPR:
    case COMPLEX_EXPR:
    case VA_ARG_EXPR:
      pp_postfix_expression (ppi, e);
      break;

    case CONJ_EXPR:
    case ADDR_EXPR:
    case INDIRECT_REF:
    case NEGATE_EXPR:
    case BIT_NOT_EXPR:
    case TRUTH_NOT_EXPR:
    case PREINCREMENT_EXPR:
    case PREDECREMENT_EXPR:
    case SIZEOF_EXPR:
    case ALIGNOF_EXPR:
    case REALPART_EXPR:
    case IMAGPART_EXPR:
      pp_c_unary_expression (ppi, e);
      break;

    case FLOAT_EXPR:
    case FIX_TRUNC_EXPR:
    case CONVERT_EXPR:
      pp_c_cast_expression (ppi, e);
      break;

    case MULT_EXPR:
    case TRUNC_MOD_EXPR:
    case TRUNC_DIV_EXPR:
      pp_multiplicative_expression (ppi, e);
      break;

    case LSHIFT_EXPR:
    case RSHIFT_EXPR:
      pp_c_shift_expression (ppi, e);
      break;

    case LT_EXPR:
    case GT_EXPR:
    case LE_EXPR:
    case GE_EXPR:
      pp_c_relational_expression (ppi, e);
      break;

    case BIT_AND_EXPR:
      pp_c_and_expression (ppi, e);
      break;

    case BIT_XOR_EXPR:
      pp_c_exclusive_or_expression (ppi, e);
      break;

    case BIT_IOR_EXPR:
      pp_c_inclusive_or_expression (ppi, e);
      break;

    case TRUTH_ANDIF_EXPR:
      pp_c_logical_and_expression (ppi, e);
      break;

    case TRUTH_ORIF_EXPR:
      pp_c_logical_or_expression (ppi, e);
      break;

    case EQ_EXPR:
    case NE_EXPR:
      pp_c_equality_expression (ppi, e);
      break;
      
    case COND_EXPR:
      pp_conditional_expression (ppi, e);
      break;

    case PLUS_EXPR:
    case MINUS_EXPR:
      pp_c_additive_expression (ppi, e);
      break;

    case MODIFY_EXPR:
    case INIT_EXPR:
      pp_assignment_expression (ppi, e);
      break;

    case COMPOUND_EXPR:
      pp_c_left_paren (ppi);
      pp_c_expression (ppi, TREE_OPERAND (e, 0));
      pp_separate_with (ppi, ',');
      pp_assignment_expression (ppi, TREE_OPERAND (e, 1));
      pp_c_right_paren (ppi);
      break;

    case NOP_EXPR:
    case NON_LVALUE_EXPR:
    case SAVE_EXPR:
    case UNSAVE_EXPR:
      pp_c_expression (ppi, TREE_OPERAND (e, 0));
      break;
      
    default:
      pp_unsupported_tree (ppi, e);
      break;
    }
}



/* Statements.  */
void
pp_c_statement (c_pretty_printer ppi, tree stmt)
{
  enum tree_code code;

  if (stmt == NULL)
    return;
  
  code = TREE_CODE (stmt);
  switch (code)
    {
    case LABEL_STMT:
    case CASE_LABEL:
      if (pp_needs_newline (ppi))
        pp_newline_and_indent (ppi, -3);
      else
        pp_indentation (ppi) -= 3;
      if (code == LABEL_STMT)
	pp_tree_identifier (ppi, DECL_NAME (LABEL_STMT_LABEL (stmt)));
      else if (code == CASE_LABEL)
	{
	  if (CASE_LOW (stmt) == NULL_TREE)
	    pp_identifier (ppi, "default");
	  else
	    {
	      pp_c_identifier (ppi, "case");
	      pp_c_whitespace (ppi);
	      pp_conditional_expression (ppi, CASE_LOW (stmt));
	      if (CASE_HIGH (stmt))
		{
		  pp_identifier (ppi, "...");
		  pp_conditional_expression (ppi, CASE_HIGH (stmt));
		}
	    }
	}
      pp_colon (ppi);
      pp_indentation (ppi) += 3;
      pp_needs_newline (ppi) = true;
      break;

    case COMPOUND_STMT:
      if (pp_needs_newline (ppi))
        pp_newline_and_indent (ppi, 0);
      pp_c_left_brace (ppi);
      pp_newline_and_indent (ppi, 3);
      for (stmt = COMPOUND_BODY (stmt); stmt; stmt = TREE_CHAIN (stmt))
	pp_c_statement (ppi, stmt);
      pp_newline_and_indent (ppi, -3);
      pp_c_right_brace (ppi);
      pp_needs_newline (ppi) = true;
      break;

    case EXPR_STMT:
    case CLEANUP_STMT:
      if (pp_needs_newline (ppi))
        pp_newline_and_indent (ppi, 0);
      {
        tree e = code == EXPR_STMT
          ? EXPR_STMT_EXPR (stmt)
          : CLEANUP_EXPR (stmt);
        if (e)
          pp_c_expression (ppi, e);
      }
      pp_c_semicolon (ppi);
      pp_needs_newline (ppi) = true;
      break;

    case IF_STMT:
      if (pp_needs_newline (ppi))
        pp_newline_and_indent (ppi, 0);
      pp_c_identifier (ppi, "if");
      pp_c_whitespace (ppi);
      pp_c_left_paren (ppi);
      pp_c_expression (ppi, IF_COND (stmt));
      pp_c_right_paren (ppi);
      pp_newline_and_indent (ppi, 3);
      pp_statement (ppi, THEN_CLAUSE (stmt));
      pp_newline_and_indent (ppi, -3);
      if (ELSE_CLAUSE (stmt))
	{
	  tree else_clause = ELSE_CLAUSE (stmt);
	  pp_c_identifier (ppi, "else");
	  if (TREE_CODE (else_clause) == IF_STMT)
	    pp_c_whitespace (ppi);
	  else
	    pp_newline_and_indent (ppi, 3);
	  pp_statement (ppi, else_clause);
	  if (TREE_CODE (else_clause) != IF_STMT)
	    pp_newline_and_indent (ppi, -3);
	}
      break;

    case SWITCH_STMT:
      if (pp_needs_newline (ppi))
        pp_newline_and_indent (ppi, 0);
      pp_c_identifier (ppi, "switch");
      pp_space (ppi);
      pp_c_left_paren (ppi);
      pp_c_expression (ppi, SWITCH_COND (stmt));
      pp_c_right_paren (ppi);
      pp_indentation (ppi) += 3;
      pp_needs_newline (ppi) = true;
      pp_statement (ppi, SWITCH_BODY (stmt));
      pp_newline_and_indent (ppi, -3);
      break;

    case WHILE_STMT:
      if (pp_needs_newline (ppi))
        pp_newline_and_indent (ppi, 0);
      pp_c_identifier (ppi, "while");
      pp_space (ppi);
      pp_c_left_paren (ppi);
      pp_c_expression (ppi, WHILE_COND (stmt));
      pp_c_right_paren (ppi);
      pp_newline_and_indent (ppi, 3);
      pp_statement (ppi, WHILE_BODY (stmt));
      pp_indentation (ppi) -= 3;
      pp_needs_newline (ppi) = true;
      break;

    case DO_STMT:
      if (pp_needs_newline (ppi))
        pp_newline_and_indent (ppi, 0);
      pp_c_identifier (ppi, "do");
      pp_newline_and_indent (ppi, 3);
      pp_statement (ppi, DO_BODY (stmt));
      pp_newline_and_indent (ppi, -3);
      pp_c_identifier (ppi, "while");
      pp_space (ppi);
      pp_c_left_paren (ppi);
      pp_c_expression (ppi, DO_COND (stmt));
      pp_c_right_paren (ppi);
      pp_c_semicolon (ppi);
      pp_needs_newline (ppi) = true;
      break;

    case FOR_STMT:
      if (pp_needs_newline (ppi))
        pp_newline_and_indent (ppi, 0);
      pp_c_identifier (ppi, "for");
      pp_space (ppi);
      pp_c_left_paren (ppi);
      if (FOR_INIT_STMT (stmt))
        pp_statement (ppi, FOR_INIT_STMT (stmt));
      else
        pp_c_semicolon (ppi);
      pp_needs_newline (ppi) = false;
      pp_c_whitespace (ppi);
      if (FOR_COND (stmt))
	pp_c_expression (ppi, FOR_COND (stmt));
      pp_c_semicolon (ppi);
      pp_needs_newline (ppi) = false;
      pp_c_whitespace (ppi);
      if (FOR_EXPR (stmt))
	pp_c_expression (ppi, FOR_EXPR (stmt));
      pp_c_right_paren (ppi);
      pp_newline_and_indent (ppi, 3);
      pp_statement (ppi, FOR_BODY (stmt));
      pp_indentation (ppi) -= 3;
      pp_needs_newline (ppi) = true;
      break;

    case BREAK_STMT:
    case CONTINUE_STMT:
      if (pp_needs_newline (ppi))
        pp_newline_and_indent (ppi, 0);
      pp_identifier (ppi, code == BREAK_STMT ? "break" : "continue");
      pp_c_semicolon (ppi);
      pp_needs_newline (ppi) = true;
      break;

    case RETURN_STMT:
    case GOTO_STMT:
      {
	tree e = code == RETURN_STMT
	  ? RETURN_STMT_EXPR (stmt)
	  : GOTO_DESTINATION (stmt);
        if (pp_needs_newline (ppi))
          pp_newline_and_indent (ppi, 0);
	pp_c_identifier (ppi, code == RETURN_STMT ? "return" : "goto");
	if (e)
	  pp_c_expression (ppi, e);
	pp_c_semicolon (ppi);
	pp_needs_newline (ppi) = true;
      }
      break;

    case SCOPE_STMT:
      if (!SCOPE_NULLIFIED_P (stmt) && SCOPE_NO_CLEANUPS_P (stmt))
        {
          int i = 0;
          if (pp_needs_newline (ppi))
            pp_newline_and_indent (ppi, 0);
          if (SCOPE_BEGIN_P (stmt))
            {
              pp_left_brace (ppi);
              i = 3;
            }
          else if (SCOPE_END_P (stmt))
            {
              pp_right_brace (ppi);
              i = -3;
            }
          pp_indentation (ppi) += i;
          pp_needs_newline (ppi) = true;
        }
      break;

    case DECL_STMT:
      if (pp_needs_newline (ppi))
        pp_newline_and_indent (ppi, 0);
      pp_declaration (ppi, DECL_STMT_DECL (stmt));
      pp_c_semicolon (ppi);
      pp_needs_newline (ppi) = true;
      break;

    case ASM_STMT:
      {
	bool has_volatile_p = ASM_VOLATILE_P (stmt);
	bool is_extended = has_volatile_p || ASM_INPUTS (stmt)
	  || ASM_OUTPUTS (stmt) || ASM_CLOBBERS (stmt);
	pp_c_identifier (ppi, is_extended ? "__asm__" : "asm");
	if (has_volatile_p)
	  pp_c_identifier (ppi, "__volatile__");
	pp_space (ppi);
	pp_c_left_paren (ppi);
	pp_c_string_literal (ppi, ASM_STRING (stmt));
	if (is_extended)
	  {
	    pp_space (ppi);
	    pp_separate_with (ppi, ':');
	    if (ASM_OUTPUTS (stmt))
	      pp_c_expression (ppi, ASM_OUTPUTS (stmt));
	    pp_space (ppi);
	    pp_separate_with (ppi, ':');
	    if (ASM_INPUTS (stmt))
	      pp_c_expression (ppi, ASM_INPUTS (stmt));
	    pp_space (ppi);
	    pp_separate_with (ppi, ':');
	    if (ASM_CLOBBERS (stmt))
	      pp_c_expression (ppi, ASM_CLOBBERS (stmt));
	  }
	pp_c_right_paren (ppi);
	pp_newline (ppi);
      }
      break;

    case FILE_STMT:
      pp_c_identifier (ppi, "__FILE__");
      pp_space (ppi);
      pp_equal (ppi);
      pp_c_whitespace (ppi);
      pp_c_identifier (ppi, FILE_STMT_FILENAME (stmt));
      pp_c_semicolon (ppi);
      pp_needs_newline (ppi) = true;
      break;

    default:
      pp_unsupported_tree (ppi, stmt);
    }

}


/* Initialize the PRETTY-PRINTER for handling C codes.  */
void
pp_c_pretty_printer_init (c_pretty_printer pp)
{
  pp->offset_list               = 0;

  pp->declaration               = pp_c_declaration;
  pp->declaration_specifiers    = pp_c_declaration_specifiers;
  pp->declarator                = pp_c_declarator;
  pp->direct_declarator         = pp_c_direct_declarator;
  pp->type_specifier            = pp_c_simple_type_specifier;
  pp->abstract_declarator       = pp_c_abstract_declarator;
  pp->parameter_declaration     = pp_c_parameter_declaration;
  pp->type_id                   = pp_c_type_id;
  pp->function_specifier        = pp_c_function_specifier;
  pp->storage_class_specifier   = pp_c_storage_class_specifier;

  pp->statement                 = pp_c_statement;

  pp->id_expression             = pp_c_id_expression;
  pp->primary_expression        = pp_c_primary_expression;
  pp->postfix_expression        = pp_c_postfix_expression;
  pp->unary_expression          = pp_c_unary_expression;
  pp->initializer               = pp_c_initializer;
  pp->multiplicative_expression = pp_c_multiplicative_expression;
  pp->conditional_expression    = pp_c_conditional_expression;
  pp->assignment_expression     = pp_c_assignment_expression;
}
