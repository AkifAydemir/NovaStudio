#ifndef NOVA_TYPES_H
#define NOVA_TYPES_H
union PairTag {
    int whole;
    char byte;
};
struct Pair {
    int left;
    int right;
    union PairTag tag;
    int history[2];
};
#endif
