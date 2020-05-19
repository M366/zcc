// N queen solver
// Reference: http://www.cc.kyoto-su.ac.jp/~yamada/ap/backtrack.html

int cnt;

int abs(int x) {
    if (x >= 0)
        return x;
    return -x;
}

int printQueen(int *qn, int bd) {
    int i;
    cnt = cnt + 1;
    
    printf("%4d:", cnt);
    for (i = 0; i < bd; i=i+1)
        printf(" %d", qn[i]+1);
    printf("\n");
}

int check(int *qn, int bd) {
    int i, j;
    
    for (i = 0; i < bd-1; i=i+1)
        for (j = i+1; j < bd; j=j+1)
            if ((qn[i] == qn[j]) + (abs(qn[i] - qn[j]) == j - i))
                return 0;
    return 1;
}

int setQueen(int *qn, int i, int bd) {
    int j;
    
    if (i == bd) {
        if (check(qn, bd))
            printQueen(qn, bd);
        return 0;
    }
    
    for (j = 0; j < bd; j=j+1) {
        qn[i] = j;
        setQueen(qn, i+1, bd);
    }
}

int main() {
    int board = 8;
    int queen[8];
    int cnt = 0;

    printf(" num: a b c d e f g h\n");
    printf("----------------------\n");
    setQueen(queen, 0, board);
}
