#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#include "grammar.h"

enum {
  TOK_EOF,
  TOK_IDENTIFIER = 256,
  TOK_CHAR_LITERAL,
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
    case '\'':
      while (*p->lexer_char != '\'' && *p->lexer_char != '\0' && *p->lexer_char != '\n') {
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

static uint64_t fnv1a_hash(void* data, size_t len) {
  uint64_t hash = 0xcbf29ce484222325;

  for (size_t i = 0; i < len; ++i) {
    uint8_t byte = ((uint8_t*)data)[i];
    hash ^= (uint64_t)byte;
    hash *= 0x100000001b3;
  }

  return hash;
}

static int to_id(Grammar* grammar, char* string) {
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

  return grammar;
}

static void dump_production_rhs(Grammar* grammar, ProductionRHS* prod) {
  for (int i = 0; i < prod->num_symbols; ++i) {
    if (i > 0) {
      printf(" ");
    }

    Symbol* sym = prod->symbols + i;

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
}

void dump_grammar(Grammar* grammar) {
  for (int nt = 0; nt < GRAMMAR_MAX_STRINGS; ++nt) {
    if (!grammar->first_prod[nt]) {
      continue;
    }

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
}