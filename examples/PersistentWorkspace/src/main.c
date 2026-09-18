#include "../include/nova.h"
int main(void) {
    struct Pair pair = {.tag = {.byte = 'Q'}, .history = {1, 2}};
    pair.left = factorial(4);
    pair.right = global_bias;
    putchar(NOVA_CONSOLE);
    return NOVA_ADD(pair.left, pair.right);
}
