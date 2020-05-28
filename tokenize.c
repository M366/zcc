#include "zcc.h"

// Input filename
static char *current_filename;

// Input string
static char *current_input;

// Reports an error and exit.
void error(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}

// Reports an error message in the following.
//
// foo.c:10: x = y + 1;
//               ^ <error message here>
static void verror_at(int line_no, char *loc, char *fmt, va_list ap) {
    // Find a line containing `loc`.
    char *line = loc;
    while (current_input < line && line[-1] != '\n')
        line--;

    char *end = loc;
    while (*end != '\n')
        end++;

    // Print out the line.
    int indent = fprintf(stderr, "%s:%d: ", current_filename, line_no);
    fprintf(stderr, "%.*s\n", (int)(end - line), line);

    // Show the error message.
    int pos = loc - line + indent;

    fprintf(stderr, "%*s", pos, ""); // print pos spaces.
    fprintf(stderr, "^ ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
}

static void error_at(char *loc, char *fmt, ...) {
    int line_no = 1;
    for (char *p = current_input; p < loc; p++)
        if (*p == '\n')
            line_no++;
    
    va_list ap;
    va_start(ap, fmt);
    verror_at(line_no, loc, fmt, ap);
    exit(1);
}

void error_tok(Token *tok, char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    verror_at(tok->line_no, tok->loc, fmt, ap);
    exit(1);
}

void warn_tok(Token *tok, char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    verror_at(tok->line_no, tok->loc, fmt, ap);
}

// Consumes the current token if it matches `op`.
bool equal(Token *tok, char *op) {
    return strlen(op) == tok->len &&
            !strncmp(tok->loc, op, tok->len);
}

// Ensure that the current token is `op`.
Token *skip(Token *tok, char *op) {
    if (!equal(tok, op))
        error_tok(tok, "expected '%s'", op);
    return tok->next;
}

bool consume(Token **rest, Token *tok, char *str) {
    if (equal(tok, str)) {
        *rest = tok->next;
        return true;
    }
    *rest = tok;
    return false;
}

// Create a new token and add it as the next token of `cur`.
static Token *new_token(TokenKind kind, Token *cur, char *str, int len) {
    Token *tok = calloc(1, sizeof(Token));
    tok->kind = kind;
    tok->loc = str;
    tok->len = len;
    cur->next = tok;
    return tok;
}

static bool startswith(char *p, char *q) {
    return strncmp(p, q, strlen(q)) == 0;
}

static bool is_alpha(char c) {
    return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c == '_';
}

static bool is_alnum(char c) {
    return is_alpha(c) || ('0' <= c && c <= '9');
}

static bool is_hex(char c) {
    return ('0' <= c && c <= '9') ||
           ('a' <= c && c <= 'f') ||
           ('A' <= c && c <= 'F');
}

static int from_hex(char c) {
    if ('0' <= c && c <= '9')
        return c - '0';
    if ('a' <= c && c <= 'f')
        return c - 'a' + 10;
    return c - 'A' + 10;
}

static bool is_keyword(Token *tok) {
    static char *kw[] = {
        "return", "if", "else", "for", "while", "int", "sizeof", "char",
        "struct", "union", "short", "long", "void", "typedef", "_Bool",
        "enum", "static", "break", "continue", "goto",
    };

    for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
        if (equal(tok, kw[i]))
            return true;
    return false;
}

static char read_escaped_char(char **new_pos, char *p) {
    if ('0' <= *p && *p <= '7') {
        // Read an octal number.
        int c = *p++ - '0';
        if ('0' <= *p && *p <= '7') {
            c = (c << 3) | (*p++ - '0');
            if ('0' <= *p && *p <= '7')
                c = (c << 3) | (*p++ - '0');
        }
        *new_pos = p;
        return c;
    }

    if (*p == 'x') {
        // Read a hexadecimal number.
        p++;
        if (!is_hex(*p))
            error_at(p, "invalid hex escape sequence");

        int c = 0;
        for (; is_hex(*p); p++) {
            c = (c << 4) | from_hex(*p);
            if (c > 255)
                error_at(p, "hex escape sequence out of range");
        }
        *new_pos = p;
        return c;
    }

    *new_pos = p + 1;

    switch (*p) {
    case 'a': return '\a';
    case 'b': return '\b';
    case 't': return '\t';
    case 'n': return '\n';
    case 'v': return '\v';
    case 'f': return '\f';
    case 'r': return '\r';
    case 'e': return 27;
    default: return *p;
    }
}

static Token *read_string_literal(Token *cur, char *start) {
    char *p = start + 1;
    char *end = p;

    // Find the closing double-quote.
    for (; *end != '"'; end++) {
        if (*end == '\0')
            error_at(start, "unclosed string literal");
        if (*end == '\\')
            end++;
    }

    // Allocate a buffer that is large enough to hold the entire string.
    char *buf = malloc(end - p + 1);
    int len = 0;

    while (*p != '"') {
        if (*p == '\\')
            buf[len++] = read_escaped_char(&p, p + 1);
        else
            buf[len++] = *p++;
    }

    buf[len++] = '\0';

    Token *tok = new_token(TK_STR, cur, start, p - start + 1);
    tok->contents = buf;
    tok->cont_len = len;
    return tok;
}

static Token *read_char_literal(Token *cur, char *start) {
    char *p = start + 1;
    if (*p == '\0')
        error_at(start, "unclosed char literal");

    char c;
    if (*p == '\\')
        c = read_escaped_char(&p, p + 1);
    else
        c = *p++;

    if (*p != '\'')
        error_at(p, "char literal too long");
    p++;

    Token *tok = new_token(TK_NUM, cur, start, p - start);
    tok->val = c;
    return tok;
}

static Token *read_int_literal(Token *cur, char *start) {
    char *p = start;

    int base = 10;
    if (!strncasecmp(p, "0x", 2) && is_alnum(p[2])) {
        p += 2;
        base = 16;
    } else if (!strncasecmp(p, "0b", 2) && is_alnum(p[2])) {
        p += 2;
        base = 2;
    } else if (*p == '0') {
        base = 8;
    }

    long val = strtoul(p, &p, base);
    if (is_alnum(*p))
        error_at(p, "invalid digit");
    
    Token *tok = new_token(TK_NUM, cur, start, p - start);
    tok->val = val;
    return tok;
}

static void convert_keywords(Token *tok) {
    for (Token *t = tok; t->kind != TK_EOF; t = t->next)
        if (t->kind == TK_IDENT && is_keyword(t))
            t->kind = TK_RESERVED;
}

// Initialize line info for all tokens.
static void add_line_info(Token *tok) {
    char *p = current_input;
    int line_no = 1;

    do {
        if (p == tok->loc) {
            tok->line_no = line_no;
            tok = tok->next;
        }
        if (*p == '\n')
            line_no++;
    } while (*p++);
}

// Tokenize a given string and returns new tokens.
static Token *tokenize(char *filename, char *p) {
    current_filename = filename;
    current_input = p;
    Token head = {};
    Token *cur = &head;

    while (*p) {
        // Skip line comments.
        if (startswith(p, "//")) {
            p += 2;
            while (*p != '\n')
                p++;
            continue;
        }

        // Skip block comments.
        if (startswith(p, "/*")) {
            char *q = strstr(p + 2, "*/");
            if (!q)
                error_at(p, "unclosed block comment");
            p = q + 2;
            continue;
        }

        // Skip whitespace characters.
        if (isspace(*p)) {
            p++;
            continue;
        }

        // Numeric literal
        if (isdigit(*p)) {
            cur = read_int_literal(cur, p);
            p += cur->len;
            continue;
        }

        // String literal
        if (*p == '"') {
            cur = read_string_literal(cur, p);
            p += cur->len;
            continue;
        }

        // Character literal
        if (*p == '\'') {
            cur = read_char_literal(cur, p);
            p += cur->len;
            continue;
        }

        // Identifier
        if (is_alpha(*p)) {
            char *q = p++;
            while (is_alnum(*p))
                p++;
            cur = new_token(TK_IDENT, cur, q, p - q);
            continue;
        }

        // Multi-letter punctuators
        if (startswith(p, "==") || startswith(p, "!=") ||
            startswith(p, "<=") || startswith(p, ">=") ||
            startswith(p, "->") || startswith(p, "+=") ||
            startswith(p, "-=") || startswith(p, "*=") ||
            startswith(p, "/=") || startswith(p, "++") ||
            startswith(p, "--") || startswith(p, "%=") ||
            startswith(p, "&=") || startswith(p, "|=") ||
            startswith(p, "^=") || startswith(p, "&&") ||
            startswith(p, "||")) {
            cur = new_token(TK_RESERVED, cur, p, 2);
            p += 2;
            continue;
        }

        // Single-letter punctuators
        if (ispunct(*p)) {
            cur = new_token(TK_RESERVED, cur, p++, 1);
            continue;
        }

        error_at(p, "invalid token");
    }

    new_token(TK_EOF, cur, p, 0);
    add_line_info(head.next);
    convert_keywords(head.next);
    return head.next;
}

// Returns the contents of a given file.
static char *read_file(char *path) {
    FILE *fp;

    if (strcmp(path, "-") == 0) {
        // By convention, read from stdin if a given filename is "-".
        fp = stdin;
    } else {
        fp = fopen(path, "r");
        if (!fp)
            error("cannot open %s: %s", path, strerror(errno));
    }

    int buflen = 4096;
    int nread = 0;
    char *buf = malloc(buflen);

    // Read the entire file.
    for (;;) {
        int end = buflen - 2; // extra 2 bytes for the trailing "\n\0"
        int n = fread(buf + nread, 1, end - nread, fp);
        if (n == 0)
            break;
        nread += n;
        if (nread == end) {
            buflen *= 2;
            buf = realloc(buf, buflen);
        }
    }

    if (fp != stdin)
        fclose(fp);
    
    // Canonicalize the last line by appending "\n"
    // if it does not end with a newline.
    if (nread == 0 || buf[nread - 1] != '\n')
        buf[nread++] = '\n';
    buf[nread] = '\0';
    return buf;
}

Token *tokenize_file(char *path) {
    return tokenize(path, read_file(path));
}
