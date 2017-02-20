#define INCLUDE_DEFS
#include <atf.h>
#include "lisp.c"

static void settext(char* str) {
    Input = emalloc(sizeof(Port));
    *Input = (Port){ .next = NULL, .type = STRINGIO, .input.string = str };
    TokenIndex = 0;
}

TEST_SUITE(UnitTests) {
    TEST(Read a positive integer) {
        settext("123");
        Value val = readval();
        CHECK(val->type == NUMBER);
        CHECK(asnum(val) == 123);
    }
    
    TEST(Read a symbol) {
        settext("foo");
        Value val = readval();
        CHECK(val->type == SYMBOL);
        CHECK(!strcmp(assym(val), "foo"));
    }
    
    TEST(Read a string) {
        settext("\"\"");
        Value val = readval();
        CHECK(val->type == STRING);
        CHECK(!strcmp(asstr(val), ""));
    }
    
    TEST(Read a quoted symbol) {
        settext("'foo");
        Value val = readval();
        CHECK(val->type == CONSCELL);
        Value unquoted = car(cdr(val));
        CHECK(unquoted->type == SYMBOL);
        CHECK(!strcmp(assym(unquoted), "foo"));
    }
}

int main(int argc, char** argv) {
    initialize();
    atf_init(argc,argv);
    RUN_TEST_SUITE(UnitTests);
    return atf_print_results();
}

