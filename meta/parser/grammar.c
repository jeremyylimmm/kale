#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include "../common.h"
#include "grammar.h"

enum {
  TOK_EOF,
  TOK_IDENTIFIER = 256,
  TOK_CHAR_LITERAL,
  TOK_NAMED_TOKEN,
  TOK_ARROW
} TokenKind;

typedef struct {
  int kind;
  int length;
  int line;
  char* start;
} Token;

typedef struct {
  char* lexer_char;
  int lexer_line;
  Token lexer_cache;
} Parser;

static int isident(char c) {
  return isalnum(c) || c == '_';
}

static int lexer_match(Parser* p, char c) {
  if (*p->lexer_char == c) {
    p->lexer_char++;
    return 1;
  }

  return 0;
}

static void error(int line, char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);

  fprintf(stderr, "Error in grammar definition on line %d: ", line);
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");

  exit(1);
}

static int until(Parser* p, char c) {
  return *p->lexer_char != c && *p->lexer_char != '\0' && *p->lexer_char != '\n';
}

static Token lex(Parser* p) {
  if (p->lexer_cache.start) {
    Token token = p->lexer_cache;
    p->lexer_cache.start = NULL;
    return token;
  }

  while(isspace(*p->lexer_char) && *p->lexer_char != '\n') {
    ++p->lexer_char;
  }

  char* start = p->lexer_char++;
  int kind = *start;
  int line = p->lexer_line;

  switch (*start) {
    default:
      if (isident(*start)) {
        while (isident(*p->lexer_char)) {
          ++p->lexer_char;
        }
        kind = TOK_IDENTIFIER;
      }
      break;
    case '\0':
      --p->lexer_char;
      kind = TOK_EOF;
      break;
    case '\n':
      p->lexer_line++;
      break;
    case '-':
      if (lexer_match(p, '>'))
        kind = TOK_ARROW;
      break;
    case '<':
      while (until(p, '>')) {
        p->lexer_char++;
      }

      if (*p->lexer_char != '>') {
        error(line, "unterminated named token");
      }

      p->lexer_char++;

      kind = TOK_NAMED_TOKEN;

      break;
    case '\'':
      while (until(p, '\'')) {
        p->lexer_char++;
      }

      if (*p->lexer_char != '\'') {
        error(line, "unterminated character literal");
      }

      p->lexer_char++;

      if ((p->lexer_char - start) != 3) {
        error(line, "invalid character literal");
      }

      kind = TOK_CHAR_LITERAL;
      break;
  }

  return (Token) {
    .kind = kind,
    .line = line,
    .start = start,
    .length = (int)(p->lexer_char - start)
  };
}

static Token peek(Parser* p) {
  if (!p->lexer_cache.start) {
    p->lexer_cache = lex(p);
  }

  return p->lexer_cache;
}

static Token require(Parser* p, int kind, char* message) {
  if (peek(p).kind != kind) {
    error(peek(p).line, message);
    exit(1);
  }

  return lex(p);
}

int to_id(Grammar* grammar, char* string) {
  int i = fnv1a_hash(string, strlen(string)) % GRAMMAR_MAX_STRINGS;

  for (int j = 0; j < GRAMMAR_MAX_STRINGS; ++j) {
    if (!grammar->strings[i]) {
      grammar->strings[i] = string;
      return i;
    }
    
    if (strcmp(grammar->strings[i], string) == 0) {
      return i;
    }

    i = (i + 1) % GRAMMAR_MAX_STRINGS;
  }

  fprintf(stderr, "Too many strings\n");
  exit(1);
}

static char* tok_to_string(Token tok) {
  char* buf = malloc((tok.length + 1) * sizeof(char));
  memcpy(buf, tok.start, tok.length * sizeof(char));
  buf[tok.length] = '\0';
  return buf;
}

static Symbol* new_symbol(ProductionRHS* prod, SymbolKind kind, int line) {
  if (prod->num_symbols == GRAMMAR_MAX_SYMBOLS_PER_PRODUCTION) {
    error(line, "Maximum symbols in a production exceeded");
  }

  Symbol* sym = prod->symbols + prod->num_symbols++;
  sym->kind = kind;

  return sym;
}

static void parse_symbol(Grammar* grammar, ProductionRHS* prod, Token token) {
  switch (token.kind) {
    default:
      error(token.line, "'%.*s' is not a valid symbol", token.length, token.start);
      break;
    case TOK_IDENTIFIER: {
      Symbol* sym = new_symbol(prod, SYM_NON_TERMINAL, token.line);
      sym->as.non_terminal = to_id(grammar, tok_to_string(token));
    } break;
    case TOK_CHAR_LITERAL: {
      Symbol *sym = new_symbol(prod, SYM_CHAR, token.line);
      sym->as.chr = token.start[1];
    } break;
    case TOK_NAMED_TOKEN: {
      Token modified = token; // Get rid of brackets
      modified.start += 1;
      modified.length -= 2;

      Symbol* sym = new_symbol(prod, SYM_NAMED_TOKEN, token.line);
      sym->as.named_token = to_id(grammar, tok_to_string(modified));
    } break;
  }
}

Grammar* parse_grammar(char* grammar_def) {
  Parser parser = {
    .lexer_char = grammar_def,
    .lexer_line = 1,
  };

  Parser* p = &parser;

  Grammar* grammar = calloc(1, sizeof(Grammar));

  while (1) {
    while (peek(p).kind == '\n') {
      lex(p);
    }

    if (peek(p).kind == TOK_EOF) {
      break;
    }

    Token nt_name_tok = require(p, TOK_IDENTIFIER, "expected a non-terminal name");
    char* nt_name_string = tok_to_string(nt_name_tok);
    int nt = to_id(grammar, nt_name_string);

    if (grammar->first_prod[nt]) {
      error(nt_name_tok.line, "non-terminal '%s' re-definition", grammar->strings[nt]);
    }

    require(p, TOK_ARROW, "expected ->");

    ProductionRHS prod_head = {0};
    ProductionRHS* prod = &prod_head;

    while (1) {
      prod = prod->next = calloc(1, sizeof(ProductionRHS));

      while (peek(p).kind != TOK_EOF && peek(p).kind != '\n') {
        parse_symbol(grammar, prod, lex(p));
      }

      lex(p);

      if (peek(p).kind == '|') {
        lex(p);
      }
      else {
        break;
      }
    }

    grammar->first_prod[nt] = prod_head.next;
  }

  int goal = to_id(grammar, "Goal");

  if (!grammar->first_prod[goal]) {
    fprintf(stderr, "Grammar does not contain 'Goal' non-terminal\n");
    exit(1);
  }

  if (grammar->first_prod[goal]->next) {
    fprintf(stderr, "'Goal' non-terminal can only contain specify one production\n");
    exit(1);
  }

  if (grammar->first_prod[goal]->num_symbols != 1) {
    fprintf(stderr, "'Goal' production can only specify one symbol\n");
    exit(1);
  }

  for (int nt = 0; nt < GRAMMAR_MAX_STRINGS; ++nt) {
    ProductionRHS* head = grammar->first_prod[nt];

    if (!head) {
      continue;
    }

    for (ProductionRHS* prod = head; prod; prod = prod->next) {
      for (int i = 0; i < prod->num_symbols; ++i) {
        if (prod->symbols[i].kind == SYM_NON_TERMINAL) {
          int name = prod->symbols[i].as.non_terminal;
          if (!grammar->first_prod[name]) {
            fprintf(stderr, "Non-terminal '%s' contains a production referencing non-terminal '%s' that doesn't exist.\n", grammar->strings[nt], grammar->strings[name]);
            exit(1);
          }
        }
      }
    } 
  }

  return grammar;
}

void pretty_print_symbol(Grammar* grammar, Symbol* sym) {
  switch (sym->kind) {
    default:
      printf("INVALID");
      break;
    case SYM_CHAR:
      printf("'%c'", sym->as.chr);
      break;
    case SYM_EOF:
      printf("eof");
      break;
    case SYM_NON_TERMINAL:
      printf("%s", grammar->strings[sym->as.non_terminal]);
      break;
  }
}

void dump_symbol(Grammar* grammar, Symbol* sym) {
  switch (sym->kind) {
    default:
      printf("INVALID");
      break;
    case SYM_CHAR:
      printf("[char: '%c']", sym->as.chr);
      break;
    case SYM_EOF:
      printf("[eof]");
      break;
    case SYM_NON_TERMINAL:
      printf("[nt: %s]", grammar->strings[sym->as.non_terminal]);
      break;
  }
}

void dump_production_rhs(Grammar* grammar, ProductionRHS* prod) {
  for (int i = 0; i < prod->num_symbols; ++i) {
    if (i > 0) {
      printf(" ");
    }

    Symbol* sym = prod->symbols + i;
    dump_symbol(grammar, sym);
  }
}

void dump_non_terminal(Grammar* grammar, int nt) {
  assert(grammar->first_prod[nt]);

  char* nt_name = grammar->strings[nt];

  printf("%s -> ", nt_name);

  for (ProductionRHS* prod = grammar->first_prod[nt]; prod; prod = prod->next) {
    if (prod != grammar->first_prod[nt]) {
      printf("%*s| ", (int)strlen(nt_name) + 2, "");
    } 

    dump_production_rhs(grammar, prod);

    printf("\n");
  }

  printf("\n");
}

void dump_grammar(Grammar* grammar) {
  for (int nt = 0; nt < GRAMMAR_MAX_STRINGS; ++nt) {
    if (!grammar->first_prod[nt]) {
      continue;
    }

    dump_non_terminal(grammar, nt);
  }
}

uint64_t production_rhs_hash(ProductionRHS* prod) {
  uint64_t hashes[] = {
    fnv1a_hash(&prod->num_symbols, sizeof(prod->num_symbols)),
    fnv1a_hash(prod->symbols, prod->num_symbols * sizeof(Symbol))
  };
  return fnv1a_hash(hashes, sizeof(hashes));
}

int production_rhs_equal(ProductionRHS* a, ProductionRHS* b) {
  if (a->num_symbols != b->num_symbols) {
    return 0;
  }

  for (int i = 0; i < a->num_symbols; ++i) {
    if (!symbol_equal(&a->symbols[i], &b->symbols[i])) {
      return 0;
    }
  }

  return 1;
}