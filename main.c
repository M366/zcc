#include "zcc.h"

bool opt_E;
char **include_paths;
static char *input_file;

static void usage(void) {
    fprintf(stderr, "zcc [ -I<path> ] [ -o <path> ]<file>\n");
    exit(1);
}

static void redirect_stdout(char *filename) {
    if (!strcmp(filename, "-"))
        return;
    
    FILE *fp = fopen(filename, "w");
    if (!fp)
        error("cannot open output file %s: %s", filename, strerror(errno));
    dup2(fileno(fp), STDOUT_FILENO); // create a copy of the file descriptor to stdout.
    fclose(fp); // close the old file descriptor.
}

static void parse_args(int argc, char **argv) {
    include_paths = malloc(sizeof(char *) * argc);
    int npaths = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help"))
            usage();

        if (!strncmp(argv[i], "-I", 2)) {
            include_paths[npaths++] = argv[i] + 2;
            continue;
        }

        if (!strcmp(argv[i], "-E")) {
            opt_E = true;
            continue;
        }

        if (!strcmp(argv[i], "-o")) { // e.g. "-o <path>"
            if (!argv[++i])
                usage();
            redirect_stdout(argv[i]);
            continue;
        }

        if (!strncmp(argv[i], "-o", 2)) { // e.g. "-o<path>"
            redirect_stdout(argv[i] + 2);
            continue;
        }

        if (argv[i][0] == '-' && argv[i][1] != '\0')
            error("unknown argument: %s", argv[i]);

        input_file = argv[i];
    }

    include_paths[npaths] = NULL;

    if (!input_file)
        error("no input files");
}

static void print_tokens(Token *tok) {
    int line = 1;
    for (; tok->kind != TK_EOF; tok = tok->next) {
        if (line > 1 && tok->at_bol)
            printf("\n");
        if (tok->has_space && !tok->at_bol)
            printf(" ");
        printf(" %.*s", tok->len, tok->loc);
        line++;
    }
    printf("\n");
}

int main(int argc, char **argv) {
    parse_args(argc, argv);

    // Tokenize and parse.
    Token *tok = tokenize_file(input_file);
    if (!tok)
        error("%s: %s", input_file, strerror(errno));

    tok = preprocess(tok);

    if (opt_E) {
        print_tokens(tok);
        exit(0);
    }

    Program *prog = parse(tok);

    // Assign offsets to local variables. The last declared lvar become the first lvar in the stack.
    for (Function *fn = prog->fns; fn; fn = fn->next) {
        // Besides local variables, callee-saved registers taken 32 bytes
        // and the variable-argument save area takes 96 bytes in the stack.
        int offset = fn->is_variadic ? 128 : 32;

        for (Var *var = fn->locals; var; var = var->next) {
            offset = align_to(offset, var->align);
            offset += size_of(var->ty);
            var->offset = offset;
        }
        fn->stack_size = align_to(offset, 16);
    }

    // Traverse the AST to emit assembly.
    codegen(prog);

    return 0;
}
