#include "zcc.h"

static int top; // = 0
static int labelseq = 1;
static int brkseq; // = 0
static int contseq; // = 0
static char *argreg8[] = {"dil", "sil", "dl", "cl", "r8b", "r9b"};
static char *argreg16[] = {"di", "si", "dx", "cx", "r8w", "r9w"};
static char *argreg32[] = {"edi", "esi", "edx", "ecx", "r8d", "r9d"};
static char *argreg64[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
static Function *current_fn; // = NULL

static char *reg(int idx) {
    static char *r[] = {"r10", "r11", "r12", "r13", "r14", "r15"};
    if (idx < 0 || sizeof(r) / sizeof(*r) <= idx)
        error("register out of range: %d", idx);
    return r[idx];
}

static char *xreg(Type *ty, int idx) {
    if (ty->base || size_of(ty) == 8)
        return reg(idx);
    
    static char *r[] = {"r10d", "r11d", "r12d", "r13d", "r14d", "r15d"};
    if (idx < 0 || sizeof(r) / sizeof(*r) <= idx)
        error("register out of range: %d", idx);
    return r[idx];
}

static char *freg(int idx) {
    static char *r[] = {"xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13"};
    if (idx < 0 || sizeof(r) / sizeof(*r) <= idx)
        error("register out of range: %d", idx);
    return r[idx];
}

static void gen_expr(Node *node);
static void gen_stmt(Node *node);

// Pushes the given node's address to the stack.
static void gen_addr(Node *node) {
    switch (node->kind) {
    case ND_VAR:
        if (node->var->is_local)
            printf("  lea %s, [rbp-%d]\n", reg(top++), node->var->offset);
        else
            printf("  mov %s, offset %s\n", reg(top++), node->var->name);
        return;
    case ND_DEREF:
        gen_expr(node->lhs);
        return;
    case ND_COMMA:
        gen_expr(node->lhs);
        top--;
        gen_addr(node->rhs);
        return;
    case ND_MEMBER:
        gen_addr(node->lhs);
        printf("  add %s, %d\n", reg(top - 1), node->member->offset);
        return;
    }

    error_tok(node->tok, "not an lvalue");
}

// Load a value from where the stack top is pointing to.
static void load(Type *ty) {
    if (ty->kind == TY_ARRAY || ty->kind == TY_STRUCT || ty->kind == TY_FUNC) {
        // If it is an array, do nothing because in general we can't load
        // an entire array to a register. As a result, the result of an
        // evaluation of an array becomes not the array itself but the
        // address of the array. In other words, this is where "array is
        // automatically converted to a pointer to the first element of
        // the array in C" occurs.
        return;
    }

    char *rs = reg(top - 1);
    char *rd = xreg(ty, top - 1);
    char *insn = ty->is_unsigned ? "movzx" : "movsx";

    // When we load a char or a short value to a register, we always
    // extend them to the size of int, so we can assume the lower half of
    // a register always contains a valid value. The upper half of a
    // register for char, short and int may contain garbage. When we load
    // a long value to a register, it simply occupies the entire register.
    int sz = size_of(ty);
    if (sz == 1)
        printf("  %s %s, byte ptr [%s]\n", insn, rd, rs);
    else if (sz == 2)
        printf("  %s %s, word ptr [%s]\n", insn, rd, rs);
    else if (sz == 4)
        printf("  mov %s, dword ptr [%s]\n", rd, rs);
    else
        printf("  mov %s, [%s]\n", rd, rs);
}

static void store(Type *ty) {
    char *rd = reg(top - 1); // rd: register dist
    char *rs = reg(top - 2); // rs: register src
    int sz = size_of(ty);

    if (ty->kind == TY_STRUCT) {
        for (int i = 0; i < sz; i++) {
            printf("  mov al, [%s+%d]\n", rs, i);
            printf("  mov [%s+%d], al\n", rd, i);
        }
    } else if (sz == 1) {
        printf("  mov [%s], %sb\n", rd, rs);
    } else if (sz == 2) {
        printf("  mov [%s], %sw\n", rd, rs);
    } else if (sz == 4) {
        printf("  mov [%s], %sd\n", rd, rs);
    } else {
        printf("  mov [%s], %s\n", rd, rs);
    }
    
    top--;
}

static void cmp_zero(Type *ty) {
    if (ty->kind == TY_FLOAT) {
        printf("  xorps xmm0, xmm0\n"); // Perform bitwise logical XOR of packed single-precision floating-point values.
        printf("  ucomiss %s, xmm0\n", freg(--top)); // Perform unordered comparison of scalar single-precision floating-point values and set flags in EFLAGS register.
    } else if (ty->kind == TY_DOUBLE) {
        printf("  xorpd xmm0, xmm0\n"); // Perform bitwise logical XOR of packed double-precision floating-point values.
        printf("  ucomisd %s, xmm0\n", freg(--top)); // Perform unordered comparison of scalar double-precision floating-point values and set flags in EFLAGS register.
    } else {
        printf("  cmp %s, 0\n", reg(--top));
    }
}

static void cast(Type *from, Type *to) {
    if (to->kind == TY_VOID)
        return;
    
    char *r = reg(top - 1);
    char *fr = freg(top - 1);

    if (to->kind == TY_BOOL) {
        cmp_zero(from);
        printf("  setne %sb\n", reg(top));
        printf("  movzx %s, %sb\n", reg(top), reg(top));
        top++;
        return;
    }

    if (from->kind == TY_FLOAT) {
        if(to->kind == TY_FLOAT)
            return;
        
        if (to->kind == TY_DOUBLE)
            printf("  cvtss2sd %s, %s\n", fr, fr); // Convert scalar single-precision floating-point values to scalar double-precision floating-point values.
        else
            printf("  cvttss2si %s, %s\n", r, fr); // Convert with truncation a scalar single-precision floating-point value to a scalar double-word integer.
        return;
    }

    if (from->kind == TY_DOUBLE) {
        if (to->kind == TY_DOUBLE)
            return;

        if (to->kind == TY_FLOAT)
            printf("  cvtsd2ss %s, %s\n", fr, fr); // Convert scalar double-precision floating-point values to scalar single-precision floating-point values.
        else
            printf("  cvttsd2si %s, %s\n", r, fr); // Convert with truncation scalar double-precision floating-point values to scalar doubleword integers.
        return;
    }
    
    if (to->kind == TY_FLOAT) {
        printf("  cvtsi2ss %s, %s\n", fr, r); // Convert (scalar) Doubleword Integer to Scalar Single-Precision Floating-Point Value
        return;
    }

    if (to->kind == TY_DOUBLE) {
        printf("  cvtsi2sd %s, %s\n", fr, r); // Convert (scalar) Doubleword Integer to Scalar Double-Precision Floating-Point Value
        return;
    }

    char *insn = to->is_unsigned ? "movzx" : "movsx";

    if (size_of(to) == 1)
        printf("  %s %s, %sb\n", insn, r, r);
    else if (size_of(to) == 2)
        printf("  %s %s, %sw\n", insn, r, r);
    else if (size_of(to) == 4)
        printf("  mov %sd, %sd\n", r, r);
    else if (is_integer(from) && size_of(from) < 8 && !from->is_unsigned)
        printf("  movsx %s, %sd\n", r, r);

}

static void divmod(Node *node, char *rd, char *rs, char *r64, char *r32) {
    if (size_of(node->ty) == 8) {
        printf("  mov rax, %s\n", rd);
        if (node->ty->is_unsigned) {
            printf("  mov rdx, 0\n");
            printf("  div %s\n", rs);
        } else {
            printf("  cqo\n");
            printf("  idiv %s\n", rs);
        }
        printf("  mov %s, %s\n", rd, r64);
    } else {
        printf("  mov eax, %s\n", rd);
        if (node->ty->is_unsigned) {
            printf("  mov edx, 0\n");
            printf("  div %s\n", rs);
        } else {
            printf("  cdq\n");
            printf("  idiv %s\n", rs);
        }
        printf("  mov %s, %s\n", rd, r32);
    }
}

static void builtin_va_start(Node *node) {
    int n = 0;
    for (Var *var = current_fn->params; var; var = var->next) 
        n++;

    printf("  mov rax, [rbp-%d]\n", node->args[0]->offset);
    printf("  mov dword ptr [rax], %d\n", n * 8);
    printf("  mov [rax+16], rbp\n");
    printf("  sub qword ptr [rax+16], 80\n");
    top++;
}

// Generate code for a given node.
static void gen_expr(Node *node) {
    printf(".loc 1 %d\n", node->tok->line_no);

    switch (node->kind) {
    case ND_NUM:
        if (node->ty->kind == TY_FLOAT) {
            float val = node->fval;
            printf("  mov rax, %u\n", *(int *)&val); // get 32bit bit pattern of fval
            printf("  push rax\n");                  // but using union is more better?
            printf("  movss %s, [rsp]\n", freg(top++));
            printf("  add rsp, 8\n");
        } else if (node->ty->kind == TY_DOUBLE) {
            printf("  movabs rax, %lu\n", *(long *)&node->fval); // get 64bit bit pattern of fval
            printf("  push rax\n");
            printf("  movsd %s, [rsp]\n", freg(top++));
            printf("  add rsp, 8\n");
        } else if (node->ty->kind == TY_LONG) {
            printf("  movabs %s, %lu\n", reg(top++), node->val);
        } else {
            printf("  mov %s, %lu\n", reg(top++), node->val);
        }
        return;
    case ND_VAR:
    case ND_MEMBER:
        gen_addr(node);
        load(node->ty);
        return;
    case ND_DEREF:
        gen_expr(node->lhs);
        load(node->ty);
        return;
    case ND_ADDR:
        gen_addr(node->lhs);
        return;
    case ND_ASSIGN:
        if (node->ty->kind == TY_ARRAY)
            error_tok(node->tok, "not an lvalue");
        if (node->lhs->ty->is_const && !node->is_init)
            error_tok(node->tok, "cannot assign to a const variable");

        gen_expr(node->rhs);
        gen_addr(node->lhs);
        store(node->ty);
        return;
    case ND_STMT_EXPR:
        for (Node *n = node->body; n; n = n->next)
            gen_stmt(n);
        top++;
        return;
    case ND_NULL_EXPR:
        top++;
        return;
    case ND_COMMA:
        gen_expr(node->lhs);
        top--;
        gen_expr(node->rhs);
        return;
    case ND_CAST:
        gen_expr(node->lhs);
        cast(node->lhs->ty, node->ty);
        return;
    case ND_COND: {
        int seq = labelseq++;
        gen_expr(node->cond);
        printf("  cmp %s, 0\n", reg(--top));
        printf("  je  .L.else.%d\n", seq);
        gen_expr(node->then);
        top--;
        printf("  jmp .L.end.%d\n", seq);
        printf(".L.else.%d:\n", seq);
        gen_expr(node->els);
        printf(".L.end.%d:\n", seq);
        return;
    }
    case ND_NOT:
        gen_expr(node->lhs);
        printf("  cmp %s, 0\n", reg(top - 1));
        printf("  sete %sb\n", reg(top - 1));
        printf("  movzx %s, %sb\n", reg(top - 1), reg(top - 1));
        return;
    case ND_BITNOT:
        gen_expr(node->lhs);
        printf("  not %s\n", reg(top - 1));
        return;
    case ND_LOGAND: {
        int seq = labelseq++; // The following comments describe the behavior in the case of a stack machine.
        gen_expr(node->lhs); // push rax in the gen_expr
        printf("  cmp %s, 0\n", reg(--top)); // pop rax
        printf("  je .L.false.%d\n", seq);
        gen_expr(node->rhs); // push rax in the gen_expr
        printf("  cmp %s, 0\n", reg(--top)); // pop rax
        printf("  je .L.false.%d\n", seq);
        printf("  mov %s, 1\n", reg(top)); // mov rax, 1 (no push/pop)
        printf("  jmp .L.end.%d\n", seq);
        printf(".L.false.%d:\n", seq);
        printf("  mov %s, 0\n", reg(top++)); // mov rax, 0 (no push/pop)
        printf(".L.end.%d:\n", seq); // ".L.end.seq:" then "push rax" (rax have the value of the expression)
        return; // In the end, a one value will be remain in the stack.
    }
    case ND_LOGOR: {
        int seq = labelseq++;
        gen_expr(node->lhs);
        printf("  cmp %s, 0\n", reg(--top));
        printf("  jne .L.true.%d\n", seq);
        gen_expr(node->rhs);
        printf("  cmp %s, 0\n", reg(--top));
        printf("  jne .L.true.%d\n", seq);
        printf("  mov %s, 0\n", reg(top));
        printf("  jmp .L.end.%d\n", seq);
        printf(".L.true.%d:\n", seq);
        printf("  mov %s, 1\n", reg(top++));
        printf(".L.end.%d:\n", seq);
        return;
    }
    case ND_FUNCALL: {
        if (node->lhs->kind == ND_VAR &&
            !strcmp(node->lhs->var->name, "__builtin_va_start")) {
            builtin_va_start(node);
            return;
        }

        // Save caller-saved registers
        printf("  push r10\n");
        printf("  push r11\n");

        gen_expr(node->lhs);

        // Load arguments from the stack.
        for (int i = 0; i < node->nargs; i++) {
            Var *arg = node->args[i];
            char *insn = arg->ty->is_unsigned ? "movzx" : "movsx";
            int sz = size_of(arg->ty);
            
            if (sz == 1)
                printf("  %s %s, byte ptr [rbp-%d]\n", insn, argreg32[i], arg->offset);
            else if (sz == 2)
                printf("  %s %s, word ptr [rbp-%d]\n", insn, argreg32[i], arg->offset);
            else if (sz == 4)
                printf("  mov %s, dword ptr [rbp-%d]\n", argreg32[i], arg->offset);
            else
                printf("  mov %s, [rbp-%d]\n", argreg64[i], arg->offset);
        }

        printf("  mov rax, 0\n");
        printf("  call %s\n", reg(--top));

        // The System V x86-64 ABI has a special rule regarding a boolean
        // return value that only the lower 8 bits are valid for it and
        // the upper 56 bit may contain garbage. Here, we clear the upper
        // 56 bits.
        if (node->ty->kind == TY_BOOL)
            printf("  movzx eax, al\n");

        printf("  pop r11\n");
        printf("  pop r10\n");
        printf("  mov %s, rax\n", reg(top++));
        return;
    }
    }

    // Binary expressions
    gen_expr(node->lhs);
    gen_expr(node->rhs);

    char *rd = xreg(node->lhs->ty, top - 2);
    char *rs = xreg(node->lhs->ty, top - 1);
    char *fd = freg(top - 2);
    char *fs = freg(top - 1);
    top--;

    switch (node->kind) {
    case ND_ADD:
        printf("  add %s, %s\n", rd, rs);
        return;
    case ND_SUB:
        printf("  sub %s, %s\n", rd, rs);
        return;
    case ND_MUL:
        printf("  imul %s, %s\n", rd, rs);
        return;
    case ND_DIV:
        divmod(node, rd, rs, "rax", "eax");
        return;
    case ND_MOD:
        divmod(node, rd, rs, "rdx", "edx");
        return;
    case ND_BITAND:
        printf("  and %s, %s\n", rd, rs); // and op1, op2 => op1 = op1 & op2
        return;
    case ND_BITOR:
        printf("  or %s, %s\n", rd, rs);
        return;
    case ND_BITXOR:
        printf("  xor %s, %s\n", rd, rs);
        return;
    case ND_EQ:
        if (node->lhs->ty->kind == TY_FLOAT)
            printf("  ucomiss %s, %s\n", fd, fs);
        else if (node->lhs->ty->kind == TY_DOUBLE)
            printf("  ucomisd %s, %s\n", fd, fs);
        else
            printf("  cmp %s, %s\n", rd, rs);
        printf("  sete al\n");
        printf("  movzx %s, al\n", rd);
        return;
    case ND_NE:
        if (node->lhs->ty->kind == TY_FLOAT)
            printf("  ucomiss %s, %s\n", fd, fs);
        else if (node->lhs->ty->kind == TY_DOUBLE)
            printf("  ucomisd %s, %s\n", fd, fs);
        else
            printf("  cmp %s, %s\n", rd, rs);
        printf("  setne al\n");
        printf("  movzx %s, al\n", rd);
        return;
    case ND_LT:
        if (node->lhs->ty->kind == TY_FLOAT) {
            printf("  ucomiss %s, %s\n", fd, fs);
            printf("  setb al\n");
        } else if (node->lhs->ty->kind == TY_DOUBLE) {
            printf("  ucomisd %s, %s\n", fd, fs);
            printf("  setb al\n");
        } else {
            printf("  cmp %s, %s\n", rd, rs);
            if (node->lhs->ty->is_unsigned)
                printf("  setb al\n"); // Set byte if below.
            else
                printf("  setl al\n"); // Set byte if less.
        }
        printf("  movzx %s, al\n", rd);
        return;
    case ND_LE:
        if (node->lhs->ty->kind == TY_FLOAT) {
            printf("  ucomiss %s, %s\n", fd, fs);
            printf("  setbe al\n");
        } else if (node->lhs->ty->kind == TY_DOUBLE) {
            printf("  ucomisd %s, %s\n", fd, fs);
            printf("  setbe al\n");
        } else {
            printf("  cmp %s, %s\n", rd, rs);
            if (node->lhs->ty->is_unsigned)
                printf("  setbe al\n"); // Set byte if below or equal.
            else
                printf("  setle al\n");
        }
        printf("  movzx %s, al\n", rd);
        return;
    case ND_SHL:
        printf("  mov rcx, %s\n", reg(top));
        printf("  shl %s, cl\n", rd);
        return;
    case ND_SHR:
        printf("  mov rcx, %s\n", reg(top));
        if (node->lhs->ty->is_unsigned)
            printf("  shr %s, cl\n", rd);
        else
            printf("  sar %s, cl\n", rd);
        return;
    default:
        error_tok(node->tok, "invalid expression");
    }
}

static void gen_stmt(Node *node) {
    printf(".loc 1 %d\n", node->tok->line_no);

    switch (node->kind) {
    case ND_IF: {
        int seq = labelseq++;
        if (node->els) {
            gen_expr(node->cond);
            printf("  cmp %s, 0\n", reg(--top));
            printf("  je  .L.else.%d\n", seq);
            gen_stmt(node->then);
            printf("  jmp .L.end.%d\n", seq);
            printf(".L.else.%d:\n", seq);
            gen_stmt(node->els);
            printf(".L.end.%d:\n", seq);
        } else {
            gen_expr(node->cond);
            printf("  cmp %s, 0\n", reg(--top));
            printf("  je  .L.end.%d\n", seq);
            gen_stmt(node->then);
            printf(".L.end.%d:\n", seq);
        }
        return;
    }
    case ND_FOR: {
        int seq = labelseq++;
        int brk = brkseq;
        int cont = contseq;
        brkseq = contseq = seq;

        if (node->init)
            gen_stmt(node->init);
        printf(".L.begin.%d:\n", seq);
        if (node->cond) {
            gen_expr(node->cond);
            printf("  cmp %s, 0\n", reg(--top));
            printf("  je  .L.break.%d\n", seq);
        }
        gen_stmt(node->then);
        printf(".L.continue.%d:\n", seq);
        if (node->inc)
            gen_stmt(node->inc);
        printf("  jmp .L.begin.%d\n", seq);
        printf(".L.break.%d:\n", seq);

        brkseq = brk;
        contseq = cont;
        return;
    }
    case ND_DO: {
        int seq = labelseq++;
        int brk = brkseq;
        int cont = contseq;
        brkseq = contseq = seq;

        printf(".L.begin.%d:\n", seq);
        gen_stmt(node->then);
        printf(".L.continue.%d:\n", seq);
        gen_expr(node->cond);
        printf("  cmp %s, 0\n", reg(--top));
        printf("  jne  .L.begin.%d\n", seq);
        printf(".L.break.%d:\n", seq);

        brkseq = brk;
        contseq = cont;
        return;
    }
    case ND_SWITCH: {
        int seq = labelseq++;
        int brk = brkseq;
        brkseq = seq;
        node->case_label = seq;

        gen_expr(node->cond);

        for (Node *n = node->case_next; n; n = n->case_next) {
            n->case_label = labelseq++;
            // n->case_end_label = seq; // is not used
            printf("  cmp %s, %ld\n", reg(top - 1), n->val);
            printf("  je .L.case.%d\n", n->case_label);
        }
        top--;

        if (node->default_case) {
            int i = labelseq++;
            // node->default_case->case_end_label = seq; // is not used.
            node->default_case->case_label = i;
            printf("  jmp .L.case.%d\n", i);
        }

        printf("  jmp .L.break.%d\n", seq);
        gen_stmt(node->then);
        printf(".L.break.%d:\n", seq);

        brkseq = brk;
        return;
    }
    case ND_CASE:
        printf(".L.case.%d:\n", node->case_label);
        gen_stmt(node->lhs);
        return;
    case ND_BLOCK:
        for (Node *n = node->body; n; n = n->next)
            gen_stmt(n); // If node is "empty statement", node->body is NULL, so for-then do nothing.
        return;
    case ND_BREAK:
        if (brkseq == 0)
            error_tok(node->tok, "stray break");
        printf("  jmp .L.break.%d\n", brkseq);
        return;
    case ND_CONTINUE:
        if (contseq == 0)
            error_tok(node->tok, "stray continue");
        printf("  jmp .L.continue.%d\n", contseq);
        return;
    case ND_GOTO:
        printf("  jmp .L.label.%s.%s\n", current_fn->name, node->label_name);
        return;
    case ND_LABEL:
        printf(".L.label.%s.%s:\n", current_fn->name, node->label_name);
        gen_stmt(node->lhs);
        return;
    case ND_RETURN:
        if (node->lhs) {
            gen_expr(node->lhs);
            printf("  mov rax, %s\n", reg(--top));
        }
        printf("  jmp .L.return.%s\n", current_fn->name);
        return;
    case ND_EXPR_STMT:
        gen_expr(node->lhs);
        top--;
        return;
    default:
        error_tok(node->tok, "invalid statement");
    }
}

static void emit_bss(Program *prog) {
    printf(".bss\n");

    for (Var *var = prog->globals; var; var = var->next) {
        if (var->init_data)
            continue;
        
        printf(".align %d\n", var->align);
        if (!var->is_static)
            printf(".globl %s\n", var->name);
        printf("%s:\n", var->name);
        printf("  .zero %d\n", size_of(var->ty));
    }
}

static void emit_data(Program *prog) {
    printf(".data\n");

    for (Var *var = prog->globals; var; var = var->next) {
        if (!var->init_data)
            continue;
            
        printf(".align %d\n", var->align);
        if (!var->is_static)
            printf(".globl %s\n", var->name);
        printf("%s:\n", var->name);

        Relocation *rel = var->rel;
        int pos = 0;
        while (pos < size_of(var->ty)) {
            if (rel && rel->offset == pos) {
                printf("  .quad %s%+ld\n", rel->label, rel->addend);
                rel = rel->next;
                pos += 8;
            } else {
                printf("  .byte %d\n", var->init_data[pos++]);
            }
        }
    }
}

static char *get_argreg(int sz, int idx) {
    if (sz == 1)
        return argreg8[idx];
    if (sz == 2)
        return argreg16[idx];
    if (sz == 4)
        return argreg32[idx];
    assert(sz == 8);
    return argreg64[idx];
}

static void emit_text(Program *prog) {
    printf(".text\n");

    for (Function *fn = prog->fns; fn; fn = fn->next) {
        if (!fn->is_static)
            printf(".globl %s\n", fn->name);
        printf("%s:\n", fn->name);
        current_fn = fn;

        // Prologue. r12-15 are callee-saved registers.
        printf("  push rbp\n");
        printf("  mov rbp, rsp\n");
        printf("  sub rsp, %d\n", fn->stack_size);
        printf("  mov [rbp-8], r12\n");
        printf("  mov [rbp-16], r13\n");
        printf("  mov [rbp-24], r14\n");
        printf("  mov [rbp-32], r15\n");

        // Save arg registers if function is variadic
        if (fn->is_variadic) {
            printf("  mov [rbp-80], rdi\n");
            printf("  mov [rbp-72], rsi\n");
            printf("  mov [rbp-64], rdx\n");
            printf("  mov [rbp-56], rcx\n");
            printf("  mov [rbp-48], r8\n");
            printf("  mov [rbp-40], r9\n");
        }
        
        // Push arguments to the stack
        int i = 0;
        for (Var *var = fn->params; var; var = var->next)
            i++;

        for (Var *var = fn->params; var; var = var->next) {
            char *r = get_argreg(size_of(var->ty), --i);
            printf("  mov [rbp-%d], %s\n", var->offset, r);
        }

        // Emit code
        for (Node *n = fn->node; n; n = n->next) {
            gen_stmt(n);
            assert(top == 0);
        }

        // Epilogue
        printf(".L.return.%s:\n", fn->name);
        printf("  mov r12, [rbp-8]\n");
        printf("  mov r13, [rbp-16]\n");
        printf("  mov r14, [rbp-24]\n");
        printf("  mov r15, [rbp-32]\n");
        printf("  mov rsp, rbp\n");
        printf("  pop rbp\n");
        printf("  ret\n");
    }
}

void codegen(Program *prog) {
    printf(".intel_syntax noprefix\n");
    emit_bss(prog);
    emit_data(prog);
    emit_text(prog);
}
