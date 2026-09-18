#include "../include/nova.h"
int factorial(int n) {
    if (n <= 1)
        return NOVA_FACTOR_BASE;
    return n * factorial(n - 1);
}
