#ifndef FOS_INC_BST_H
#define FOS_INC_BST_H

#include <inc/types.h>

struct Node {
    int key;
    struct Node *left;
    struct Node *right;
};

struct BST {
    struct Node *root;
};

void bst_init(struct BST *tree);
void bst_append(struct BST *tree, int key);
void bst_remove(struct BST *tree, int key);
int bst_find_max(struct BST *tree);
int bst_find(struct BST *tree, int key);
void bst_print(struct BST *tree); // Helper for debugging

#endif
