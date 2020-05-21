int stack1() {
    int x; int y; char z;
    printf("int x; int y; char z;\n");
    printf("%ld\n", &x);
    printf("%ld\n", &y);
    printf("%ld\n", &z);
}

int stack2() {
    int x; char y; int z;
    printf("int x; char y; int z;\n");
    printf("%ld\n", &x);
    printf("%ld\n", &y);
    printf("%ld\n", &z);
}

int main() {
    stack1();
    stack2();
}