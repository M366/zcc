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

// Reports an error message in the following format.
//
// foo.c:10: x = y + 1;
//               ^ <error message here>
static void verror_at(char *filename, char *input, int line_no,
                      char *loc, char *fmt, va_list ap) {
    // Find a line containing `loc`.
    char *line = loc;
    while (input < line && line[-1] != '\n')
        line--;

    char *end = loc;
    while (*end && *end != '\n')
        end++;

    // Print out the line.
    int indent = fprintf(stderr, "%s:%d: ", filename, line_no);
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
    verror_at(current_filename, current_input, line_no, loc, fmt, ap);
    exit(1);
}

void error_tok(Token *tok, char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    verror_at(tok->filename, tok->input, tok->line_no, tok->loc, fmt, ap);
    exit(1);
}

void warn_tok(Token *tok, char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    verror_at(tok->filename, tok->input, tok->line_no, tok->loc, fmt, ap);
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
    tok->filename = current_filename;
    tok->input = current_input;
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
        "enum", "static", "break", "continue", "goto", "switch", "case",
        "default", "extern", "alignof", "_Alignas", "do", "signed",
        "unsigned", "const", "volatile", "float", "double",
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
    char *p = start + 1; // e.g. "foo" => *start = `"`, *p = f"
    char *end = p;

    // Find the closing double-quote.
    for (; *end != '"'; end++) {
        if (*end == '\0')
            error_at(start, "unclosed string literal");
        if (*end == '\\')
            end++;
    } // After the `for` statement, end = `"` 

    // Allocate a buffer that is large enough to hold the entire string.
    char *buf = malloc(end - p + 1); // sizeof (end-p+1) = 4
    int len = 0;

    while (*p != '"') {
        if (*p == '\\')
            buf[len++] = read_escaped_char(&p, p + 1);
        else
            buf[len++] = *p++;
    } // After the `for` statement, *p = `"`

    buf[len++] = '\0'; // After the line, buf = 'f' 'o' 'o' '\0', len = 4

    Token *tok = new_token(TK_STR, cur, start, p - start + 1); // *start = `"`, *(p-start+1)=5
    tok->contents = buf; // tok->contents = 'f' 'o' 'o' '\0'
    tok->cont_len = len; // tok->cont_len = 4
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
    tok->ty = ty_int;
    return tok;
}

static bool convert_pp_int(Token *tok) {
    char *p = tok->loc;

    // Read a binary, octal, decimal or hexadecimal number.
    int base = 10;
    if (!strncasecmp(p, "0x", 2) && is_hex(p[2])) {
        p += 2;
        base = 16;
    } else if (!strncasecmp(p, "0b", 2) && (p[2] == '0' || p[2] == '1')) { // 0b prefix is GCC extension
        p += 2;
        base = 2;
    } else if (*p == '0') {
        base = 8;
    }

    long val = strtoul(p, &p, base);

    // Read U, L or LL suffixes.
    bool l = false;
    bool u = false;

    if (startswith(p, "LLU") || startswith(p, "LLu") ||
        startswith(p, "llU") || startswith(p, "llu") ||
        startswith(p, "ULL") || startswith(p, "Ull") ||
        startswith(p, "uLL") || startswith(p, "ull")) {
        p += 3;
        l = u = true;
    } else if (!strncasecmp(p, "lu", 2) || !strncasecmp(p, "ul", 2)) {
        p += 2;
        l = u = true;
    } else if (startswith(p, "LL") || startswith(p, "ll")) {
        p += 2;
        l = true;
    } else if (*p == 'L' || *p == 'l') {
        p++;
        l = true;
    } else if (*p == 'U' || *p == 'u') {
        p++;
        u = true;
    }
    
    // Infer a type.
    Type *ty;
    if (base == 10) {
        if (l && u)
            ty = ty_ulong;
        else if (l)
            ty = ty_long;
        else if (u)
            ty = (val >> 32) ? ty_ulong : ty_uint;
        else
            ty = (val >> 31) ? ty_long : ty_int;
    } else {
        if (l && u)
            ty = ty_ulong;
        else if (l)
            ty = (val >> 63) ? ty_ulong : ty_long;
        else if (u)
            ty = (val >> 32) ? ty_ulong : ty_uint;
        else if (val >> 63)
            ty = ty_ulong;
        else if (val >> 32)
            ty = ty_long;
        else if (val >> 31)
            ty = ty_uint;
        else
            ty = ty_int;
    }

    if (p != tok->loc + tok->len)
        return false;

    tok->kind = TK_NUM;
    tok->val = val;
    tok->ty = ty;
    return true;
}

// The definition of the numeric literal at the preprocessing stage
// is more relaxed than the definition of that at the later stages.
// In order to handle that, a numeric literal is tokenized as a
// "pp-number" token first and then converted to a regular number
// token after preprocessing.
//
// This function converts a pp-number token to a regular number token.
static void convert_pp_number(Token *tok) {
    // Try to parse as an integer constant.
    if (convert_pp_int(tok))
        return;

    // If it's not an integer, it must be a floating point constant.
    char *end;
    double val = strtod(tok->loc, &end);

    Type *ty;
    if (*end == 'f' || *end == 'F') {
        ty = ty_float;
        end++;
    } else if (*end == 'l' || *end == 'L') {
        ty = ty_double;
        end++;
    } else {
        ty = ty_double;
    }

    if (tok->loc + tok->len != end)
        error_tok(tok, "invalid numeric constant");

    tok->kind = TK_NUM;
    tok->fval = val;
    tok->ty = ty;
}

void convert_pp_tokens(Token *tok) {
    for (Token *t = tok; t->kind != TK_EOF; t = t->next) {
        switch (t->kind) {
        case TK_IDENT:
            if (is_keyword(t))
                t->kind = TK_RESERVED;
            continue;
        case TK_PP_NUM:
            convert_pp_number(t);
            continue;
        }
    }
}

// Initialize token position info for all tokens.
static void add_line_info(Token *tok) {
    char *p = current_input;
    int line_no = 1;
    bool at_bol = true;
    bool has_space = false;

    do {
        if (p == tok->loc) {
            tok->line_no = line_no;
            tok->at_bol = at_bol; 
            tok->has_space = has_space;
            tok = tok->next;
        }

        if (*p == '\n') {
            line_no++;
            at_bol = true; // Set at_bol of next token to true if *p is `\n`.
        } else if (isspace(*p)) {
            has_space = true; // Set has_space of next token to true if *p is space.
        } else {
            at_bol = false; // Set at_bol of next token to false if *p isn't space.
            has_space = false; // Set has_space of next token to false if *p isn't space.
        }
    } while (*p++);
}

// Tokenize a given string and returns new tokens.
Token *tokenize(char *filename, int file_no, char *p) {
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
        if (isdigit(*p) || (p[0] == '.' && isdigit(p[1]))) {
            char *q = p++;
            for (;;) {
                if (p[0] && p[1] && strchr("eEpP", p[0]) && strchr("+-", p[1]))
                    p += 2;
                else if (isalnum(*p) || *p == '.')
                    p++;
                else
                    break;
            }
            cur = new_token(TK_PP_NUM, cur, q, p - q);
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
        if (is_alpha(*p) || (*p & 0x80)) {
            char *q = p++;
            while (is_alnum(*p) || (*p & 0x80))
                p++;
            cur = new_token(TK_IDENT, cur, q, p - q);
            continue;
        }

        // Three-letter punctuators
        if (startswith(p, "<<=") || startswith(p, ">>=") ||
            startswith(p, "...")) {
            cur = new_token(TK_RESERVED, cur, p, 3);
            p += 3;
            continue;
        }

        // Two-letter punctuators
        if (startswith(p, "==") || startswith(p, "!=") ||
            startswith(p, "<=") || startswith(p, ">=") ||
            startswith(p, "->") || startswith(p, "+=") ||
            startswith(p, "-=") || startswith(p, "*=") ||
            startswith(p, "/=") || startswith(p, "++") ||
            startswith(p, "--") || startswith(p, "%=") ||
            startswith(p, "&=") || startswith(p, "|=") ||
            startswith(p, "^=") || startswith(p, "&&") ||
            startswith(p, "||") || startswith(p, "<<") ||
            startswith(p, ">>") || startswith(p, "##")) {
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

    for (Token *t = head.next; t; t = t->next)
        t->file_no = file_no;
    add_line_info(head.next);
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
            return NULL;
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

// Removes backslashes followed by a newline.
static void remove_backslash_newline(char *p) {
    char *q = p;

    // We want to keep the number of newline characters so that
    // the logical line number matches the physical one.
    // This counter maintain the number of newlines we have removed.
    int cnt = 0;

    while (*p) {
        if (startswith(p, "\\\n")) {
            p += 2;
            cnt++;
        } else if (*p == '\n') {
            *q++ = *p++;
            for (; cnt > 0; cnt--)
                *q++ = '\n';
        } else {
            *q++ = *p++;
        }
    }

    *q = '\0';
}

// Encode a given character in UTF-8.
static int encode_utf8(char *buf, int c) {
    if (c <= 0x7F) {
        buf[0] = c;
        return 1;
    }

    if (c <= 0x7FF) {
        buf[0] = 0b11000000 | (c >> 6);
        buf[1] = 0b10000000 | (c & 0b00111111);
        return 2;
    }

    if (c <= 0xFFFF) {
        buf[0] = 0b11100000 | (c >> 12);
        buf[1] = 0b10000000 | ((c >> 6) & 0b00111111);
        buf[2] = 0b10000000 | (c & 0b00111111);
        return 3;
    }

    buf[0] = 0b11110000 | (c >> 18);
    buf[1] = 0b10000000 | ((c >> 12) & 0b00111111);
    buf[2] = 0b10000000 | ((c >> 6) & 0b00111111);
    buf[3] = 0b10000000 | (c & 0b00111111);
    return 4;
}

static int read_universal_char(char *p, int len) {
    long c = 0;
    for (int i = 0; i < len; i++) {
        if (!is_hex(p[i]))
            return 0;
        c = (c << 4) | from_hex(p[i]);
    }

    // Unicode code-space is limited to 21 bits.
    // U+10FFFF is the largest valid code-point.
    return (c <= 0x10FFFF) ? c : 0;
}

// Replace \u or \U escape sequences with corresponding UTF-8 bytes.
static void convert_universal_chars(char *p) {
    char *q = p;

    while (*p) {
        int c;
        if (startswith(p, "\\u") && (c = read_universal_char(p + 2, 4))) {
            q += encode_utf8(q, c);
            p += 6;
        } else if (startswith(p, "\\U") && (c = read_universal_char(p + 2, 8))) {
            q += encode_utf8(q, c);
            p += 10;
        } else if (p[0] == '\\') {
            *q++ = *p++;
            *q++ = *p++;
        } else {
            *q++ = *p++;
        }
    }

    *q = '\0';
}

Token *tokenize_file(char *path) {
    char *p = read_file(path);
    if (!p)
        return NULL;
    
    remove_backslash_newline(p);
    convert_universal_chars(p);

    // Emit a .file directive for the assembler.
    static int file_no;
    if (!opt_E)
        printf(".file %d \"%s\"\n", ++file_no, path);

    return tokenize(path, file_no, p);
}
