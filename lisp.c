/*
    Copyright 2017 Michael D. Lowis
    
    Permission to use, copy, modify, and/or distribute this software
    for any purpose with or without fee is hereby granted, provided
    that the above copyright notice and this permission notice appear
    in all copies.
    
    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
    WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
    AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
    DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA
    OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
    TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
    PERFORMANCE OF THIS SOFTWARE.
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <assert.h>

/* Type Definitions
 *****************************************************************************/
typedef struct Value* Value;

struct Value {
    enum { 
        CONSCELL, NUMBER, BOOLEAN, STRING, SYMBOL, PRIMITIVE, FUNCTION
    } type;
    Value data[];
};

typedef Value (*PrimOp)(Value val);

typedef struct Port {
    struct Port* next;
    enum { STRINGIO, FILEIO } type;
    union {
        char* string;
        FILE* file;
    } input;
} Port;

/* Globals
 *****************************************************************************/
static Port* Input = NULL;
static char TokenBuf[8192] = {0};
static size_t TokenIndex = 0;
static Value Globals = NULL;
static Value Symbols = NULL;
static Value Nil = NULL;
static Value Quote = NULL;
static Value If = NULL;
static Value Func = NULL;
static Value Def = NULL;
static Value Set = NULL;
static Value True = NULL;
static Value False = NULL;

/* Utility Functions
 *****************************************************************************/
static void die(char* str) {
    printf("Error: %s\n", str);
    exit(1);
}

static void* emalloc(size_t sz) {
    void* obj = malloc(sz);
    if (!obj) die("emalloc: out of memory");
    return obj;
}

/* Value Functions
 *****************************************************************************/
static Value mkval(int type, size_t count, ...) {
    Value val = emalloc(sizeof(struct Value) + (count * sizeof(Value)));
    val->type = type;
    va_list args;
    va_start(args, count);
    for(size_t i = 0; i < count; i++) 
        val->data[i] = va_arg(args, Value);
    va_end(args);
    return val;
}

static Value cons(Value a, Value b) {
    return mkval(CONSCELL, 2, a, b);
}

static Value car(Value val) {
    return val->data[0];
}

static Value cdr(Value val) {
    return val->data[1];
}

static void setcar(Value cell, Value val) {
    cell->data[0] = val;
}

static void setcdr(Value cell, Value val) {
    cell->data[1] = val;
}

static Value mksym(char* val) {
    return mkval(SYMBOL, 1, (Value)val);
}

static char* assym(Value val) {
    assert(val->type == SYMBOL);
    return (char*)(val->data[0]);
}

static Value lookup(char* name) {
    for(Value syms = Symbols; syms; syms = cdr(syms))
        if(!strcmp(name, assym(car(syms))))
            return syms;
    return NULL;
}

static Value intern(char* name) {
    Value entry = lookup(name);
    if (entry) return car(entry);
    entry = mksym(strdup(name));
    Symbols = cons(entry, Symbols);
    return entry;
}

static Value mknum(intptr_t val) {
    return mkval(NUMBER, 1, (Value)val);
}

static intptr_t asnum(Value val) {
    assert(val->type == NUMBER);
    return (intptr_t)(val->data[0]);
}

static Value mkbool(bool val) {
    return mkval(BOOLEAN, 1, (Value)val);
}

static bool asbool(Value val) {
    assert(val->type == BOOLEAN);
    return (bool)(val->data[0]);
}

static Value mkstr(char* val) {
    return mkval(STRING, 1, (Value)val);
}

static char* asstr(Value val) {
    assert(val->type == STRING);
    return (char*)(val->data[0]);
}

static Value mkprim(PrimOp val) {
    return mkval(PRIMITIVE, 1, (Value)val);
}

static PrimOp asprim(Value val) {
    assert(val->type == PRIMITIVE);
    return (PrimOp)(val->data[0]);
}

static Value mkfunc(Value args, Value code, Value env) {
    return mkval(FUNCTION, 3, args, code, env);
}

static Value funcargs(Value val) {
    assert(val->type == FUNCTION);
    return (val->data[0]);
}

static Value funccode(Value val) {
    assert(val->type == FUNCTION);
    return (val->data[1]);
}

static Value funcenv(Value val) {
    assert(val->type == FUNCTION);
    return (val->data[2]);
}

/* Environment
 *****************************************************************************/
static Value extend(Value env, Value sym, Value val) {
    return cons(cons(sym, val), env);
}

static Value addglobal(Value sym, Value val) {
    Globals = extend(Globals, sym, val);
    return val;
}

static Value assoc(Value key, Value alist) {
    for(; alist; alist = cdr(alist))
        if (car(car(alist)) == key)
            return car(alist);
    return NULL;
}

/* Reader
 *****************************************************************************/ 
static Value readval(void);
static Value readsym(void);

static int fetchchar(void) {
    int c;
    if (!Input) return EOF;
    
    if (Input->type == STRINGIO)
        c = *(Input->input.string++);
    else
        c = fgetc(Input->input.file);
    
    if (c == EOF || c == '\0') {
        Port* port = Input;
        Input = Input->next;
        free(port);
        return fetchchar();
    } else {
        return c;
    }
}

static void unfetchchar(int c) {
    if (!Input) return;
    if (Input->type == STRINGIO)
        Input->input.string--;
    else
        ungetc(c, Input->input.file);
}

static int nextchar(void) {
    int c = fetchchar();
    unfetchchar(c);
    return c;
}

static void takechar(void) {
    TokenBuf[TokenIndex++] = nextchar();
    TokenBuf[TokenIndex]   = '\0';
    fetchchar();
}

static void cleartok(void) {
    TokenIndex  = 0;
    TokenBuf[0] = '\0';
}

static bool oneof(int c, char* set) {
    for (; *set; set++)
        if (c == *set) return true;
    return false;
}

static void skipws(void) {
    while (isspace(nextchar()))
        fetchchar();
}

static Value readnum(void) {
    if (nextchar() == '+' || nextchar() == '-')
        takechar();
    if (!isdigit(nextchar()))
        return readsym();
    while (isdigit(nextchar()))
        takechar();
    long int val = strtol(TokenBuf, NULL, 0);
    cleartok();
    return mknum(val);
}

static Value readstring(void) {
    fetchchar();
    while (nextchar() != '"')
        takechar();
    fetchchar();
    Value val = mkstr( strdup(TokenBuf) );
    cleartok();
    return val;
}

static Value readquote(void) {
    fetchchar();
    return cons(intern("quote"), cons(readval(), NULL));
}

static Value readlist(void) {
    Value list = NULL, reversed = NULL;
    fetchchar();
    while (nextchar() != ')') {
        list = cons(readval(), list);
        skipws();
    }
    fetchchar();
    while (list) {
        reversed = cons(car(list), reversed);
        list = cdr(list);
    }
    return reversed;
}

static Value readsym(void) {
    while (nextchar() != EOF && !oneof(nextchar(), "()[]{}'\" \t\r\n"))
        takechar();
    if (TokenIndex == 0) exit(0);
    Value val = intern(TokenBuf);
    cleartok();
    return val;
}

static Value readval(void) {
    skipws();
    int ch = nextchar();
    if (ch == EOF) {
        exit(0);
    } else if (isdigit(ch) || ch == '-' || ch == '+') {
        return readnum();
    } else if (ch == '"') {
        return readstring();
    } else if (ch == '\'') {
        return readquote();
    } else if (ch == '(') {
        return readlist();
    } else if (!oneof(ch, "()[]{}'\"")) {
        return readsym();
    } else {
        puts("syntax error");
        while (nextchar() != '\n')
            fetchchar();
        return readval();
    }
    return NULL; // Impossible
}

/* Evaluator
 *****************************************************************************/
static Value apply(Value func, Value args, Value* env);
static Value applyargs(Value env, Value args, Value vals);
static Value applyfunc(Value body, Value env);
static Value evallist(Value list, Value* env);

static Value eval(Value val, Value* env) {
    if (val->type == CONSCELL) {
        Value first = car(val);
        if (first == Func) {
            return mkfunc(car(cdr(val)), cdr(cdr(val)), *env);
        } else if (first == Quote) {
            return car(cdr(val));
        } else if (first == Def) {
            Value name   = car(cdr(val));
            Value newval = eval(car(cdr(cdr(val))), env);
            *env = extend(*env, name, newval);
            return newval;
        } else if (first == Set) {
            Value name   = car(cdr(val));
            Value var    = assoc(name, *env);
            Value newval = eval(car(cdr(cdr(val))), env);
            if (var)
                setcdr(var, newval);
            else
                addglobal(name, newval);
            return newval;
        } else if (first == If) {
            Value cond = eval(car(cdr(val)), env);
            if (cond != False)
                return eval(car(cdr(cdr(val))), env);
            else
                return eval(car(cdr(cdr(cdr(val)))), env);
        } else {
            return apply(eval(first, env), evallist(cdr(val), env), env);
        }
    } else if (val->type == SYMBOL) {
        Value var = assoc(val, *env);
        if (!var) die("unbound symbol");
        return cdr(var);
    } else {
        return val;
    }
}

static Value apply(Value func, Value args, Value* env) {
    if (func->type == PRIMITIVE) {
        return (asprim(func))(args);
    } else if (func->type == FUNCTION) {
        return applyfunc(funccode(func), applyargs(funcenv(func), funcargs(func), args));
    } else {
        puts("non-function used in function application");
        return NULL;
    }
}

static Value applyargs(Value env, Value args, Value vals) {
    while (args) {
        env  = extend(env, car(args), car(vals));
        args = cdr(args);
        vals = cdr(vals);
    }
    return env;
}

static Value applyfunc(Value body, Value env) {
    if (!body) return NULL;
    while (true) {
        if (!cdr(body))
            return eval(car(body), &env);
        (void)eval(car(body), &env);
        body = cdr(body);
    }
}

static Value evallist(Value list, Value* env) {
    if (!list) return list;
    return cons(eval(car(list), env), evallist(cdr(list), env));
}

/* Printer
 *****************************************************************************/
static void print(FILE* f, Value val) {
    if (val == NULL) { fprintf(f, "nil"); return; }
    switch (val->type) {
        case CONSCELL:
            fprintf(f, "<conscell:%p>", (void*)val);
            break;
        case NUMBER:
            fprintf(f, "%ld", asnum(val));
            break;
        case BOOLEAN:
            fprintf(f, "%s", (asbool(val) ? "true" : "false"));
            break;
        case STRING:
            fprintf(f, "\"%s\"", asstr(val));
            break;
        case SYMBOL:
            fprintf(f, "%s", assym(val));
            break;
        case PRIMITIVE:
            fprintf(f, "<prim:%p>", (void*)val);
            break;
        case FUNCTION:
            fprintf(f, "<func:%p>", (void*)val);
            break;
    }
}

/* Primitives
 *****************************************************************************/
static Value num_add(Value args) {
    Value a = car(args);
    Value b = car(cdr(args));
    return mknum(asnum(a) + asnum(b));
}

static Value load(Value args) {
    char* fname = asstr(car(args));
    Port* port = emalloc(sizeof(Port));
    port->type = FILEIO;
    port->input.file = fopen(fname, "r");
    port->next = Input;
    Input = port;
    return NULL;
}
 
/* Main Routines
 *****************************************************************************/
static void initialize(void) {
    Quote = intern("quote");
    If    = intern("if");
    Def   = intern("def");
    Set   = intern("set!");
    Func  = intern("fn");
    True  = addglobal(intern("true"),  mkbool(true));
    False = addglobal(intern("false"), mkbool(false));
    addglobal(intern("+"), mkprim(num_add));
    addglobal(intern("load"), mkprim(load));
}

#ifndef TEST
int main(int argc, char** argv) {
    Input = emalloc(sizeof(Port));
    Input->type = FILEIO;
    Input->input.file = stdin;
    Input->next = NULL;
    initialize();    
    while (true) {
        print(stdout, eval(readval(), &Globals));
        fprintf(stdout, "\n");
    }
    return 0;
}
#endif

