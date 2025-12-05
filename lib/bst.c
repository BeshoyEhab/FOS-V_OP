#include <inc/bst.h>
#include <inc/lib.h>

void bst_init(struct BST *tree) {
    tree->root = NULL;
}

static struct Node* create_node(int key) {
    struct Node *node = (struct Node*)malloc(sizeof(struct Node));
    if (node) {
        node->key = key;
        node->left = NULL;
        node->right = NULL;
    }
    return node;
}

static struct Node* insert_node(struct Node *node, int key) {
    if (node == NULL) {
        return create_node(key);
    }
    if (key < node->key) {
        node->left = insert_node(node->left, key);
    } else if (key > node->key) {
        node->right = insert_node(node->right, key);
    }
    return node;
}

void bst_append(struct BST *tree, int key) {
    tree->root = insert_node(tree->root, key);
}

static struct Node* find_min_node(struct Node *node) {
    struct Node *current = node;
    while (current && current->left != NULL)
        current = current->left;
    return current;
}

static struct Node* delete_node(struct Node *node, int key) {
    if (node == NULL) return node;

    if (key < node->key) {
        node->left = delete_node(node->left, key);
    } else if (key > node->key) {
        node->right = delete_node(node->right, key);
    } else {
        if (node->left == NULL) {
            struct Node *temp = node->right;
            free(node);
            return temp;
        } else if (node->right == NULL) {
            struct Node *temp = node->left;
            free(node);
            return temp;
        }

        struct Node *temp = find_min_node(node->right);
        node->key = temp->key;

        // Delete the inorder successor
        node->right = delete_node(node->right, temp->key);
    }
    return node;
}

void bst_remove(struct BST *tree, int key) {
    tree->root = delete_node(tree->root, key);
}

int bst_find_max(struct BST *tree) {
    if (tree->root == NULL) return 0;
    struct Node *current = tree->root;
    while (current->right != NULL) current = current->right;
    return current->key;
}

int bst_find(struct BST *tree, int key) {
    struct Node *current = tree->root;
    while (current != NULL) {
        if (key == current->key) return 1;
        else if (key < current->key) current = current->left;
        else current = current->right;
    }
    return 0;
}

static void print_inorder(struct Node *node) {
    if (node == NULL) return;
    print_inorder(node->left);
    cprintf("%d ", node->key);
    print_inorder(node->right);
}

void bst_print(struct BST *tree) {
    print_inorder(tree->root);
    cprintf("\n");
}
