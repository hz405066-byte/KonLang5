#ifndef KON_AST_H
#define KON_AST_H

#include <stdio.h>

/* Виды выражений. EX_MATH с op == NULL — обёртка с типом (int/float/double). */
typedef enum {
    EX_INT, EX_FLOAT, EX_STR, EX_VAR, EX_LIST, EX_CONCAT,
    EX_REPEAT, EX_MATH, EX_LEN, EX_CALL, EX_BIN
} ExprKind;

typedef struct Expr Expr;
struct Expr {
    ExprKind kind;
    int line;
    union {
        long i;
        double f;
        char *s;
        struct { char *name; Expr *index; } var;
        struct { Expr **items; int n; } seq;          /* LIST, CONCAT */
        struct { Expr *text, *count; } rep;
        struct { char *type, *op; Expr *a, *b; } math;
        Expr *inner;                                  /* LEN */
        struct { char *name; Expr **args; int n; } call;
        struct { char *op; Expr *a, *b; } bin;
    } u;
};

typedef enum {
    ST_PRINT, ST_INPUT, ST_KEY, ST_CLEAR, ST_RANDOM, ST_SET, ST_EXEC,
    ST_CYCLE, ST_IF, ST_DEF, ST_RET, ST_BREAK, ST_EXIT, ST_TRY,
    ST_FILE, ST_SCREEN, ST_TOUCH, ST_TIME, ST_KON, ST_EXPR
} StmtKind;

typedef struct Stmt Stmt;
typedef struct Block { Stmt **items; int n; } Block;

typedef struct { Expr *cond; Block *body; } IfBranch;

struct Stmt {
    StmtKind kind;
    int line;
    union {
        struct { Expr **args; int n; int coords; } print;
        struct { char *name; Expr *prompt, *limit; int type; } input;
        struct { char *name; } key;
        struct { Expr *count; int infinite; Block *body; } cycle;
        struct { IfBranch *br; int n; Block *els; } cond;
        struct { char *name; Expr *value; } set;
        struct { char *name; Block *body; char **params; int nparams; } named;
        struct { Expr *value; } ret;
        struct { char *name; } exec;
        struct { char *name; Expr **items; int n; } rnd;
        struct { int op; Expr *a, *b; } file;
        struct { int op; Expr **args; int n; } io;   /* SCREEN, TOUCH */
        struct { Block *body; char *handler; Block *catchblk; } tryc;
        Block *block;
        Expr *expr;
    } u;
};

typedef struct { Block *body; } Program;

/* Коды операций для ST_FILE / ST_SCREEN / ST_TOUCH. */
enum { KF_OPEN=1, KF_WRITE, KF_APPEND, KF_SEARCH, KF_DEL };
enum { KS_CREATE=1, KS_SIZE, KS_BG, KS_PAINT, KS_BEEP };
enum { KT_CUR=1, KT_DOWN, KT_BUTTON, KT_HOLD, KT_SWIPE, KT_IDLE };

char *ast_strdup(const char *s);
Expr *ast_expr(ExprKind k, int line);
Stmt *ast_stmt(StmtKind k, int line);
Block *ast_block(void);
void ast_block_add(Block *b, Stmt *s);
void ast_free_expr(Expr *e);
void ast_free_block(Block *b);
void ast_free_program(Program *p);
void ast_dump(FILE *f, Program *p);

#endif
