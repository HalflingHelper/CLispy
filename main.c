// TODO: Improve error reporting
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "mpc.h"
#include "mathutil.h"

/* If we are compiling on Windows compile these functions */
#ifdef _WIN32
#include <string.h>

static char buffer[2048];

/* Fake readline function */
char* readline(char* prompt) {
  fputs(prompt, stdout);
  fgets(buffer, 2048, stdin);
  char* cpy = malloc(strlen(buffer)+1);
  strcpy(cpy, buffer);
  cpy[strlen(cpy)-1] = '\0';
  return cpy;
}

/* Fake add_history function */
void add_history(char* unused) {}

/* Otherwise include the editline headers */
#else
#include <editline.h>
#endif

#define LASSERT(args, cond, fmt, ...) \
  if (!(cond)) { \
    lval* err = lval_err(fmt, ##__VA_ARGS__); \
    lval_del(args); \
    return err; \
  }

mpc_parser_t* Number;
mpc_parser_t* String;
mpc_parser_t* Boolean;
mpc_parser_t* Comment;
mpc_parser_t* Symbol;
mpc_parser_t* Sexpr;
mpc_parser_t* Qexpr;
mpc_parser_t* Expr;
mpc_parser_t* Lispy;

typedef enum { LVAL_NUM, LVAL_ERR, LVAL_FUN, LVAL_BOOL, 
               LVAL_STR, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR } Val_Type;

struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;

typedef lval*(*lbuiltin)(lenv*, lval*);

struct lval {
  Val_Type type;
  /* Basic */
  long num;
  bool boolean;
  char* err;
  char* sym;
  char* string;
  /* Functions */
  lbuiltin builtin;
  lenv* env;
  lval* formals;
  lval* body;
  /* Expression */
  int count;
  lval** cell;
};

struct lenv {
  lenv* par;
  int count;
  char** syms;
  lval** vals;
};

lval* lval_eval(lenv* e, lval* v);
void lval_del(lval* v);
lval* lval_err(char* err, ...);
lval* lval_copy(lval* v);

lenv* lenv_new(void) {
  lenv* e = malloc(sizeof(lenv));
  e->count = 0;
  e->syms = NULL;
  e->vals = NULL;
  e->par = NULL;
  return e;
} 

void lenv_del(lenv* e) {
  for (int i = 0; i != e->count; ++i) {
    free(e->syms[i]);
    lval_del(e->vals[i]);
  }
  free(e->syms);
  free(e->vals);
  free(e);
}

lval* lenv_get(lenv* e, lval* k) {
  for (int i = 0; i != e->count; ++i) {
    if (strcmp(e->syms[i], k->sym) == 0) {
      return lval_copy(e->vals[i]);
    }
  }

  if (e->par) {
    return lenv_get(e->par, k);
  }

  return lval_err("unbound symbol '%s'!", k->sym);
}

void lenv_put(lenv* e, lval* k, lval* v) {
  for (int i = 0; i != e->count; ++i) {
    if (strcmp(e->syms[i], k->sym) == 0) {
      lval_del(e->vals[i]);
      e->vals[i] = lval_copy(v);
      return;
    }
  }

  e->count++;
  e->vals = realloc(e->vals, sizeof(lval*) * e->count);
  e->syms = realloc(e->syms, sizeof(char*) * e->count);

  e->vals[e->count-1] = lval_copy(v);
  e->syms[e->count-1] = malloc(strlen(k->sym)+1);
  strcpy(e->syms[e->count-1], k->sym);
}

void lenv_def(lenv* e, lval* k, lval* v) {
  /* Iterate till e has no parent */
  while (e->par) { e = e->par; }
  /* Put value in e */
  lenv_put(e, k, v);
}

lval* lval_lambda(lval* formals, lval* body) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_FUN;
  v->builtin = NULL;

  v->env = lenv_new();

  v->formals = formals;
  v->body = body;
  return v;
}

lval* lval_num(long x) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_NUM;
  v->num = x;
  return v;
}

lval* lval_str(char* s) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_STR;
  v->string = malloc(strlen(s) + 1);
  strcpy(v->string, s);
  return v;
}

lval* lval_bool(bool b) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_BOOL;
  v->boolean = b;
  return v;
}

lval* lval_err(char* fmt, ...) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_ERR;

  /* Create a va list and initialize it */
  va_list va;
  va_start(va, fmt);

  /* Allocate 512 bytes of space */
  v->err = malloc(512);

  /* printf the error string with a maximum of 511 characters */
  vsnprintf(v->err, 511, fmt, va);

  /* Reallocate to number of bytes actually used */
  v->err = realloc(v->err, strlen(v->err)+1);

  /* Cleanup our va list */
  va_end(va);

  return v;
}

lval* lval_fun(lbuiltin func) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_FUN;
  v->builtin = func;
  return v;
}

lval* lval_sym(char* y) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SYM;
  v->sym = malloc(strlen(y) + 1);
  strcpy(v->sym, y);
  return v;
}

lval* lval_sexpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

lval* lval_qexpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_QEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

void lval_del(lval* v) {
  switch (v->type) {
    case LVAL_NUM: 
    case LVAL_BOOL:
      break;
    case LVAL_STR:
      free(v->string);
      break;
    case LVAL_FUN:
      if (v->builtin == NULL) {
        lenv_del(v->env);
        lval_del(v->formals);
        lval_del(v->body);
      }
      break;
    case LVAL_ERR:
      free(v->err);
      break;
    case LVAL_SYM:
      free(v->sym);
      break;
    case LVAL_QEXPR:
    case LVAL_SEXPR:
      for (int i = 0; i != v->count; ++i) {
        lval_del(v->cell[i]);
      }
      free(v->cell);
      break;
  }

  free(v);
}

lval* lval_read_num(mpc_ast_t* t) {
  errno = 0;
  long x = strtol(t->contents, NULL, 10);
  return errno != ERANGE ?
    lval_num(x) : lval_err("invalid number");
}

lval* lval_add(lval* v, lval* x) {
  v->count++;
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);
  v->cell[v->count - 1] = x;
  return v;
}

lval* lval_read_str(mpc_ast_t* t) {
  /* Cut off the final quote character */
  t->contents[strlen(t->contents)-1] = '\0';
  /* Copy the string missing out the first quote character */
  char* unescaped = malloc(strlen(t->contents+1)+1);
  strcpy(unescaped, t->contents+1);
  /* Pass through the unescape function */
  unescaped = mpcf_unescape(unescaped);
  /* Construct a new lval using the string */
  lval* str = lval_str(unescaped);
  /* Free the string and return */
  free(unescaped);
  return str;
}

lval* lval_read(mpc_ast_t* t) {
  if (strstr(t->tag, "number")) {
    return lval_read_num(t);
  } else if (strstr(t->tag, "boolean")) {
    if (strcmp(t->contents, "#t") == 0) {
      return lval_bool(true);
    } else {
      return lval_bool(false);
    }
  } else if (strstr(t->tag, "symbol")) {
    return lval_sym(t->contents);
  } else if (strstr(t->tag, "string")) {
    return lval_read_str(t);
  }

  lval* x = NULL;
  if (strcmp(t->tag, ">") == 0 || strstr(t->tag, "sexpr")) {
    x = lval_sexpr();
  }
  if (strstr(t->tag, "qexpr")) {
    x = lval_qexpr();
  }

  for (int i = 0; i != t->children_num; ++i) {
    if (strstr(t->children[i]->tag, "comment"))     { continue; }
    if (strcmp(t->children[i]->contents, "(") == 0) { continue; }
    if (strcmp(t->children[i]->contents, ")") == 0) { continue; }
    if (strcmp(t->children[i]->contents, "{") == 0) { continue; }
    if (strcmp(t->children[i]->contents, "}") == 0) { continue; }
    if (strcmp(t->children[i]->tag,  "regex") == 0) { continue; }
    x = lval_add(x, lval_read(t->children[i]));
  }

  return x;
}

void lval_print(lval* v);

void lval_expr_print(lval* v, char open, char close) {
  putchar(open);
  for (int i = 0; i != v->count; ++i) {
    lval_print(v->cell[i]);

    if (i != (v->count-1)) {
      putchar(' ');
    }
  }
  putchar(close);
}

void lval_print_str(lval* v) {
  /* Make a Copy of the string */
  char* escaped = malloc(strlen(v->string)+1);
  strcpy(escaped, v->string);
  /* Pass it through the escape function */
  escaped = mpcf_escape(escaped);
  /* Print it between " characters */
  printf("\"%s\"", escaped);
  /* free the copied string */
  free(escaped);
}


/* Print an "lval" */
void lval_print(lval* v) {
  switch (v->type) {
    case LVAL_NUM: 
      printf("%li", v->num); 
      break;

    case LVAL_STR:
      lval_print_str(v);
      break;

    case LVAL_BOOL:
      if (v->boolean) {
        printf("#t");
      } else {
        printf("#f");
      }
      break;
    case LVAL_ERR:
      printf("Error: %s\n", v->err);
      break;

    case LVAL_FUN:
      if (v->builtin) {
        printf("<builtin>");
      } else {
        printf("(\\ "); lval_print(v->formals);
        putchar(' '); lval_print(v->body); putchar(')');
      }
      break;

    case LVAL_SYM:
      printf("%s", v->sym);
      break;
    
    case LVAL_SEXPR:
      lval_expr_print(v, '(', ')');
      break;

    case LVAL_QEXPR:
      lval_expr_print(v, '{', '}');
      break;
  }
}
lenv* lenv_copy(lenv* e) {
  lenv* n = malloc(sizeof(lenv));
  n->par = e->par;
  n->count = e->count;
  n->syms = malloc(sizeof(char*) * n->count);
  n->vals = malloc(sizeof(lval*) * n->count);
  for (int i = 0; i < e->count; i++) {
    n->syms[i] = malloc(strlen(e->syms[i]) + 1);
    strcpy(n->syms[i], e->syms[i]);
    n->vals[i] = lval_copy(e->vals[i]);
  }
  return n;
}

/* Copy and lval */
lval* lval_copy(lval* v) {
  lval* x = malloc(sizeof(lval));
  x->type = v->type;

  switch (v->type) {
    case LVAL_BOOL:
      x->boolean = v->boolean;
      break;
    case LVAL_NUM: 
      x->num = v->num; 
      break;
    case LVAL_STR:
      x->string = malloc(strlen(v->string) + 1);
      strcpy(x->string, v->string);
      break;
    case LVAL_FUN:
      if (v->builtin != NULL) {
        x->builtin = v->builtin;
      } else {
        x->builtin = NULL;
        x->env = lenv_copy(v->env);
        x->formals = lval_copy(v->formals);
        x->body = lval_copy(v->body);
      }
      break;
    case LVAL_ERR:
      x->err = malloc(strlen(v->err) + 1);
      strcpy(x->err, v->err);
      break;
    case LVAL_SYM:
      x->sym = malloc(strlen(v->sym) + 1);
      strcpy(x->sym, v->sym);
      break;
    case LVAL_QEXPR:
    case LVAL_SEXPR:
      x->count = v->count;
      x->cell = malloc(sizeof(lval*) * x->count);
      for (int i = 0; i != v->count; ++i) {
        x->cell[i] = lval_copy(v->cell[i]);
      }
      break;
  }

  return x;
}

/* Print an lval followed by a newline */
void lval_println(lval* v) { lval_print(v); putchar('\n'); }

lval* lval_pop(lval* v, int i) {
  /* Find the item at "i" */
  lval* x = v->cell[i];

  /* Shift memory after the item at "i" over the top */
  memmove(&v->cell[i], &v->cell[i+1],
    sizeof(lval*) * (v->count-i-1));

  /* Decrease the count of items in the list */
  v->count--;

  /* Reallocate the memory used */
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);
  return x;
}

lval* lval_take(lval* v, int i) {
  lval* x = lval_pop(v, i);
  lval_del(v);
  return x;
}

lval* builtin_op(lenv* e, lval* a, char* op) {
  for (int i = 0; i != a->count; ++i) {
    if (a->cell[i]->type != LVAL_NUM) {
      lval_del(a);
      return lval_err("Cannot operate on a non-number!");
    }
  }

  lval* x = lval_pop(a, 0);

  /* If no arguments and sub then perform unary negation */
  if ((strcmp(op, "-") == 0) && a->count == 0) {
    x->num = -x->num;
  }

  while (a->count != 0) {
    lval* y = lval_pop(a, 0);

    if (strcmp(op, "+") == 0)  x->num += y->num;
    if (strcmp(op, "-") == 0)  x->num -= y->num;
    if (strcmp(op, "*") == 0)  x->num *= y->num;
    if (strcmp(op, "/") == 0) {
      if (y->num == 0) {
        lval_del(x); lval_del(y);
        x = lval_err("Can't divide by 0");
        break;
      }
      x->num /= y->num;
    }
    if (strcmp(op, "%") == 0) x->num %= y->num;
    if (strcmp(op, "^") == 0) x->num = lpow(x->num, y->num);

    lval_del(y);
  }
  
  lval_del(a);
  return x;
}

lval* builtin_load(lenv* e, lval* a) {
  LASSERT(a, a->count == 1, "'load' expects 1 argument.");
  LASSERT(a, a->cell[0]->type == LVAL_STR, "'load' expects a string.");
  mpc_result_t r;
  if (mpc_parse_contents(a->cell[0]->string, Lispy, &r)) {
    
    lval* expr = lval_read(r.output);
    mpc_ast_delete(r.output);

    while (expr->count) {
      lval* x = lval_eval(e, lval_pop(expr, 0));

      if (x->type == LVAL_ERR) {
        lval_println(x);
      }
      lval_del(x);
    }

    lval_del(expr);
    lval_del(a);

    return lval_sexpr();
  } else {
    puts("A");
    char* err_msg = mpc_err_string(r.error);
    mpc_err_delete(r.error);

    lval* err = lval_err("Could not load Libarry %s", err_msg);
    free(err_msg);
    lval_del(a);

    return err;
  }
    puts("C\n");

}

lval* builtin_var(lenv* e, lval* a, char* func) {
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
    "Function 'def' passed incorrect type!");
  
  lval* syms = a->cell[0];

  for (int i = 0; i != syms->count; ++i) {
    LASSERT(a, syms->cell[i]->type == LVAL_SYM,
      "Function 'def' cannot define non-symbols!");
  }

  LASSERT(a, syms->count == a->count - 1,
    "Function 'def' cannot define incorrect number of values to symbols.");

  for (int i = 0; i != syms->count; ++i) {
    if (strcmp("def", func) == 0) {
      lenv_def(e, syms->cell[i], a->cell[i+1]);
    }

    if (strcmp(func, "=") == 0) {
      lenv_put(e, syms->cell[i], a->cell[i+1]);
    }
  }
  lval_del(a);
  return lval_sexpr();
}

lval* builtin_head(lenv* e, lval* a) {
  LASSERT(a, a->count == 1, "Function 'head' passed too many arguments!");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Function 'head' passed incorrect type!");
  LASSERT(a, a->cell[0]->count != 0, "Function 'head' passed {}!");

  lval* v = lval_take(a, 0);
  while (v->count > 1) {
    lval_del(lval_pop(v, 1));
  }
  return v;
}

lval* builtin_tail(lenv* e, lval* a) {
  LASSERT(a, a->count == 1, "Function 'tail' passed too many arguments!");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Function 'tail' passed incorrect type!");
  LASSERT(a, a->cell[0]->count != 0, "Function 'tail' passed {}!");

  lval* v = lval_take(a, 0);
  lval_del(lval_pop(v,0));
  return v;
}

lval* builtin_list(lenv* e, lval* a) {
  a->type = LVAL_QEXPR;
  return a;
}

lval* builtin_eval(lenv* e, lval* a) {
  LASSERT(a, a->count == 1,
    "Function 'eval' passed too many arguments!");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
    "Function 'eval' passed incorrect type!");

  lval* x = lval_take(a, 0);
  x->type = LVAL_SEXPR;
  return lval_eval(e, x);
}
lval* lval_call(lenv* e, lval* f, lval* a) {

  /* If Builtin then simply apply that */
  if (f->builtin) { return f->builtin(e, a); }

  /* Record Argument Counts */
  int given = a->count;
  int total = f->formals->count;

  /* While arguments still remain to be processed */
  while (a->count) {

    /* If we've ran out of formal arguments to bind */
    if (f->formals->count == 0) {
      lval_del(a); return lval_err(
        "Function passed too many arguments. "
        "Got %i, Expected %i.", given, total);
    }

    /* Pop the first symbol from the formals */
    lval* sym = lval_pop(f->formals, 0);

    if (strcmp(sym->sym, "&") == 0) {
      if (f->formals->count != 1) {
        lval_del(a);
        return lval_err("Function format invalid");
      }

      lval* nsym = lval_pop(f->formals, 0);
      lenv_put(f->env, nsym, builtin_list(e, a));
      lval_del(sym);
      lval_del(nsym);
      break;
    }

    /* Pop the next argument from the list */
    lval* val = lval_pop(a, 0);

    /* Bind a copy into the function's environment */
    lenv_put(f->env, sym, val);

    /* Delete symbol and value */
    lval_del(sym); lval_del(val);
  }

  /* Argument list is now bound so can be cleaned up */
  lval_del(a);

  /* If '&' remains in formal list bind to empty list */
  if (f->formals->count > 0 &&
    strcmp(f->formals->cell[0]->sym, "&") == 0) {
    
    /* Check to ensure that & is not passed invalidly. */
    if (f->formals->count != 2) {
      return lval_err("Function format invalid. "
        "Symbol '&' not followed by single symbol.");
    }
  
    /* Pop and delete '&' symbol */
    lval_del(lval_pop(f->formals, 0));
  
    /* Pop next symbol and create empty list */
    lval* sym = lval_pop(f->formals, 0);
    lval* val = lval_qexpr();
  
    /* Bind to environment and delete */
    lenv_put(f->env, sym, val);
    lval_del(sym); lval_del(val);
  }

  /* If all formals have been bound evaluate */
  if (f->formals->count == 0) {

    /* Set environment parent to evaluation environment */
    f->env->par = e;

    /* Evaluate and return */
    return builtin_eval(
      f->env, lval_add(lval_sexpr(), lval_copy(f->body)));
  } else {
    /* Otherwise return partially evaluated function */
    return lval_copy(f);
  }

}

bool lval_eqv(lval* x, lval* y) {
  if (x->type != y->type) return false;

  /* Compare Based upon type */
  switch (x->type) {
    /* Compare Number Value */
    case LVAL_NUM: return (x->num == y->num);
    case LVAL_BOOL: return (x->boolean == y->boolean);

    /* Compare String Values */
    case LVAL_ERR: return (strcmp(x->err, y->err) == 0);
    case LVAL_SYM: return (strcmp(x->sym, y->sym) == 0);
    case LVAL_STR: return (strcmp(x->string, y->string) == 0);

    /* If builtin compare, otherwise compare formals and body */
    case LVAL_FUN:
      if (x->builtin || y->builtin) {
        return x->builtin == y->builtin;
      } else {
        return lval_eqv(x->formals, y->formals)
          && lval_eqv(x->body, y->body);
      }

    /* If list compare every individual element */
    case LVAL_QEXPR:
    case LVAL_SEXPR:
      if (x->count != y->count) { return 0; }
      for (int i = 0; i < x->count; i++) {
        /* If any element not equal then whole list not equal */
        if (!lval_eqv(x->cell[i], y->cell[i])) { return 0; }
      }
      /* Otherwise lists must be equal */
      return true;
    break;
  }
  return false;
}

lval* lval_join(lval* x, lval* y) {
  while (y->count != 0) {
    lval_add(x, lval_pop(y, 0));
  }

  lval_del(y);
  return x;
}

lval* builtin_join(lenv* e, lval* a) {
  for (int i = 0; i != a->count; ++i) {
    LASSERT(a, a->cell[i]->type == LVAL_QEXPR, 
      "Function 'join' passed incorrect type!");
  }

  lval* x = lval_pop(a, 0);

  while (a->count) {
    x = lval_join(x, lval_pop(a, 0));
  }

  lval_del(a);
  return x;
}

lval* builtin_cons(lenv* e, lval* a) {
  LASSERT(a, a->count == 2, "Function 'cons' should be passed two arguments!");
  LASSERT(a, a->cell[1]->type == LVAL_QEXPR, "Function 'cons' passed incorrect type!");

  // Create an empty list

  lval* car = lval_pop(a, 0);
  lval* cdr = lval_pop(a, 0);

  lval_del(a);

  lval* v = lval_qexpr();

  lval_add(v, car);

  while (cdr->count) {
    lval_add(v, lval_pop(cdr, 0));
  }

  return v;
}

lval* builtin_init(lenv* e, lval* a) {
  LASSERT(a, a->count == 1, "Function 'init' passed too many arguments!");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Function 'init' passed incorrect type!");
  LASSERT(a, a->cell[0]->count != 0, "Function 'init' passed {}!");

  lval* x = lval_pop(a, 0);

  lval_pop(x, x->count - 1);

  lval_del(a);
  return x;
}

lval* builtin_lambda(lenv* e, lval* a) {
  LASSERT(a, a->count == 2, "'lambda' expects formals and a body.");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Formals");
  LASSERT(a, a->cell[1]->type == LVAL_QEXPR, "lambda expects a body");

  for (int i = 0; i != a->cell[0]->count; ++i) {
    LASSERT(a, a->cell[0]->cell[i]->type == LVAL_SYM,
      "Cannot define a non-symbol");
  }

  lval* formals = lval_pop(a, 0);
  lval* body = lval_pop(a, 0);
  lval_del(a);

  // TODO: I hate dynamic scoping
  return lval_lambda(formals, body);
}

lval* builtin_add(lenv* e, lval* a) {
  return builtin_op(e, a, "+");
}

lval* builtin_sub(lenv* e, lval* a) {
  return builtin_op(e, a, "-");
}

lval* builtin_mul(lenv* e, lval* a) {
  return builtin_op(e, a, "*");
}

lval* builtin_div(lenv* e, lval* a) {
  return builtin_op(e, a, "/");
}

lval* builtin_mod(lenv* e, lval* a) {
  return builtin_op(e, a, "%");
}

lval* builtin_pow(lenv* e, lval* a) {
  return builtin_op(e, a, "^");
}

void lenv_add_builtin(lenv* e, char* name, lbuiltin func) {
  lval* k = lval_sym(name);
  lval* v = lval_fun(func);
  lenv_put(e, k, v);
  lval_del(k); lval_del(v);
}

lval* builtin_def(lenv* e, lval* a) {
  return builtin_var(e, a, "def");
}

lval* builtin_put(lenv* e, lval* a) {
  return builtin_var(e, a, "=");
}

// TODO: Comparing symbols, lists, arbitrary number of things
lval* builtin_comp(lenv* e, lval* a, char* comp) {
// Make sure that a is 2 numbers
  LASSERT(a, a->count == 2, "Comparison expected 2 numbers.");

  for (int i = 0; i != a->count; ++i) {
    if (a->cell[i]->type != LVAL_NUM) {
      lval_del(a);
      return lval_err("Comparison cannot operate on a non-number!");
    }
  }

  lval* r = lval_bool(false);
  lval* x = lval_pop(a, 0);
  lval* y = lval_pop(a, 0);


  if (strcmp(comp, "<") == 0) { r->boolean = x->num < y->num; };
  if (strcmp(comp, ">") == 0) { r->boolean = x->num > y->num; };
  if (strcmp(comp, "=") == 0) { r->boolean = x->num == y->num; };
  if (strcmp(comp, ">=") == 0) { r->boolean = x->num >= y->num; };
  if (strcmp(comp, "<=") == 0) { r->boolean = x->num <= y->num; };
  if (strcmp(comp, "!=") == 0) { r->boolean = x->num != y->num; };
  
  lval_del(a);
  lval_del(x);
  lval_del(y);

  return r;
}

lval* builtin_lt(lenv* e, lval* a) {
  return builtin_comp(e, a, "<");
}

lval* builtin_gt(lenv* e, lval* a) {
  return builtin_comp(e, a, ">");
}
lval* builtin_eq(lenv* e, lval* a) {
  return builtin_comp(e, a, "=");
}
lval* builtin_neq(lenv* e, lval* a) {
  return builtin_comp(e, a, "!=");
}
lval* builtin_geq(lenv* e, lval* a) {
  return builtin_comp(e, a, ">=");
}
lval* builtin_leq(lenv* e, lval* a) {
  return builtin_comp(e, a, "<=");
}

lval* builtin_eqv(lenv* e, lval* a) {
  LASSERT(a, a->count, "'eqv?' expects 2 arguments");
  return lval_bool(lval_eqv(a->cell[0], a->cell[1]));
}

lval* builtin_if(lenv* e, lval* a) {
  LASSERT(a, a->count == 3, "Arity mismatch, 'if' expects 3 values but got %li", a->count);
  LASSERT(a, a->cell[0]->type == LVAL_BOOL, "First argument to 'if' should be a bool");
  LASSERT(a, a->cell[1]->type == LVAL_QEXPR, "'if' expected qexpr");
  LASSERT(a, a->cell[2]->type == LVAL_QEXPR, "'if' expected qexpr");

  /* Mark Both Expressions as evaluable */
  lval* x;
  a->cell[1]->type = LVAL_SEXPR;
  a->cell[2]->type = LVAL_SEXPR;

  if (a->cell[0]->boolean) {
    /* If condition is true evaluate first expression */
    x = lval_eval(e, lval_pop(a, 1));
  } else {
    /* Otherwise evaluate second expression */
    x = lval_eval(e, lval_pop(a, 2));
  }

  /* Delete argument list and return */
  lval_del(a);
  return x;
}

void lenv_add_builtins(lenv* e) {
  /* List Functions */
  lenv_add_builtin(e, "cons", builtin_cons);
  lenv_add_builtin(e, "list", builtin_list);
  lenv_add_builtin(e, "head", builtin_head);
  lenv_add_builtin(e, "tail", builtin_tail);
  lenv_add_builtin(e, "eval", builtin_eval);
  lenv_add_builtin(e, "join", builtin_join);
  lenv_add_builtin(e, "init", builtin_init);
  /* Variable functions */
  lenv_add_builtin(e, "def", builtin_def);
  lenv_add_builtin(e, "=",   builtin_put);
  lenv_add_builtin(e, "\\", builtin_lambda);
  lenv_add_builtin(e, "if", builtin_if);
  /* Mathematical Functions */
  lenv_add_builtin(e, "+", builtin_add);
  lenv_add_builtin(e, "-", builtin_sub);
  lenv_add_builtin(e, "*", builtin_mul);
  lenv_add_builtin(e, "/", builtin_div);
  lenv_add_builtin(e, "%", builtin_mod);
  lenv_add_builtin(e, "^", builtin_pow);
  /* Comparisons */
  lenv_add_builtin(e, "eqv?", builtin_eqv);
  lenv_add_builtin(e, "<", builtin_lt);
  lenv_add_builtin(e, ">", builtin_gt);
  lenv_add_builtin(e, "=", builtin_eq);
  lenv_add_builtin(e, "!=", builtin_neq);
  lenv_add_builtin(e, ">=", builtin_geq);
  lenv_add_builtin(e, "<=", builtin_leq);

lenv_add_builtin(e, "load",  builtin_load);

  // TODO: Boolean functions, and, or, not
}

lval* lval_eval_sexpr(lenv* e,lval* v) {

  /* Evaluate Children */
  for (int i = 0; i < v->count; i++) {
    v->cell[i] = lval_eval(e, v->cell[i]);
  }

  /* Error Checking */
  for (int i = 0; i < v->count; i++) {
    if (v->cell[i]->type == LVAL_ERR) { return lval_take(v, i); }
  }

  /* Empty Expression */
  if (v->count == 0) { return v; }

  /* Single Expression */
  if (v->count == 1) { return lval_take(v, 0); }

  /* Ensure First Element is Symbol */
  lval* f = lval_pop(v, 0);
  if (f->type != LVAL_FUN) {
    lval_del(f); lval_del(v);
    return lval_err("first element is not a function!");
  }

  /* Call builtin with operator */
  lval* result = lval_call(e, f, v);
  lval_del(f);
  return result;
}

lval* lval_eval(lenv* e,lval* v) {
  /* Evaluate symbols */
  if (v->type == LVAL_SYM) {
    lval* x = lenv_get(e, v);
    lval_del(v);
    return x;
  }
  /* Evaluate Sexpressions */
  if (v->type == LVAL_SEXPR) { return lval_eval_sexpr(e, v); }
  /* All other lval types remain the same */
  return v;
}


int main(int argc, char** argv) {
  /* Create Some Parsers */
  Number = mpc_new("number");
  String = mpc_new("string");
  Boolean = mpc_new("boolean");
  Comment = mpc_new("comment");
  Symbol = mpc_new("symbol");
  Sexpr = mpc_new("sexpr");
  Qexpr = mpc_new("qexpr");
  Expr = mpc_new("expr");
  Lispy = mpc_new("lispy");

  /* Define them with the following Language */
  mpca_lang(MPCA_LANG_DEFAULT,
	    "\
	    number   : /-?[0-9]+/ ; \
      string   : /\"(\\\\.|[^\"])*\"/ ;  \
      comment  : /;[^\\r\\n]*/ ; \
      boolean  : /#[tf]/  ; \
	    symbol   : /[\\^\\\%a-zA-Z0-9_+\\-*\\/\\\\=<>!&\\?]+/ ;         \
      sexpr    : '(' <expr>* ')' ; \
      qexpr    : '{' <expr>* '}' ; \
	    expr     : <number> | <string> | <comment> | <boolean> | <symbol> | <sexpr> | <qexpr> ; \
	    lispy    : /^/ <expr>* /$/ ; \
	    ",
	    Number, String, Comment, Boolean, Symbol, Sexpr, Qexpr, Expr, Lispy);
  
  puts("CLisp Version 0.0.0.0.9");
  puts("Press Ctrl+c to Exit\n");
   
  lenv* e = lenv_new();
  lenv_add_builtins(e);

  if (argc >= 2) {
    for (int i = 1; i != argc; ++i) {
      lval* args = lval_add(lval_sexpr(), lval_str(argv[i]));

      lval* x = builtin_load(e, args);

      if (x->type == LVAL_ERR) { lval_println(x); }
      lval_del(x);
    }
  }

  while (1) {
    
    /* Now in either case readline will be correctly defined */
    char* input = readline("clisp> ");
    add_history(input);

    /* Parse the user input */
    mpc_result_t r;
    if (mpc_parse("<stdin>", input, Lispy, &r)) {
      lval* x = lval_eval(e, lval_read(r.output));
      lval_println(x);
      lval_del(x);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }
	   
    free(input);
  }
  /* Undefine and Delete our Parsers */
  mpc_cleanup(9, Number, String, Comment, Boolean, Symbol, Sexpr, Qexpr, Expr, Lispy);
  
  return 0;
}
