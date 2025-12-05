#ifndef FOS_INC_BST_H
#define FOS_INC_BST_H

#include <inc/types.h>

struct Node
{
    uint32 key;
    struct Node *left;
    struct Node *right;
    int height;
};

struct BST
{
    struct Node *root;
};

void bst_init(struct BST *tree);
void bst_append(struct BST *tree, uint32 key);
void bst_remove(struct BST *tree, uint32 key);
int bst_find_max(struct BST *tree);
int bst_find(struct BST *tree, uint32 key);
int bst_find_max_lt_value(struct BST *tree, uint32 key);
void bst_print(struct BST *tree); // Helper for debugging

#endif