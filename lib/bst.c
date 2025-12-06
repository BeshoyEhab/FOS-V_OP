
#include <inc/bst.h>
#include <inc/lib.h>
// #include <kern/mem/kheap.h>

void bst_init(struct BST *tree)
{
    tree->root = NULL;
}

static int height(struct Node *node)
{
    if (node == NULL)
        return 0;
    return node->height;
}

static int max(int a, int b)
{
    return (a > b) ? a : b;
}

static int get_balance(struct Node *node)
{
    if (node == NULL)
        return 0;
    return height(node->left) - height(node->right);
}

#ifdef FOS_KERNEL
#include <kern/mem/kheap.h>
#endif

static struct Node *create_node(uint32 key)
{
#ifdef FOS_KERNEL
    struct Node *node = (struct Node *)kmalloc(sizeof(struct Node));
#else
    struct Node *node = (struct Node *)malloc(sizeof(struct Node));
#endif
    if (node)
    {
        node->key = key;
        node->height = 1;
        node->left = NULL;
        node->right = NULL;
    }
    return node;
}

// Right rotation
static struct Node *rotate_right(struct Node *y)
{
    struct Node *x = y->left;
    struct Node *T2 = x->right;

    // Perform rotation
    x->right = y;
    y->left = T2;

    // Update heights
    y->height = max(height(y->left), height(y->right)) + 1;
    x->height = max(height(x->left), height(x->right)) + 1;

    return x;
}

// Left rotation
static struct Node *rotate_left(struct Node *x)
{
    struct Node *y = x->right;
    struct Node *T2 = y->left;

    // Perform rotation
    y->left = x;
    x->right = T2;

    // Update heights
    x->height = max(height(x->left), height(x->right)) + 1;
    y->height = max(height(y->left), height(y->right)) + 1;

    return y;
}

static struct Node *insert_node(struct Node *node, uint32 key)
{
    // Standard BST insertion
    if (node == NULL)
    {
        return create_node(key);
    }
    if (key < node->key)
    {
        node->left = insert_node(node->left, key);
    }
    else if (key > node->key)
    {
        node->right = insert_node(node->right, key);
    }
    else
    {
        return node; // Duplicate keys not allowed
    }

    // Update height of this ancestor node
    node->height = 1 + max(height(node->left), height(node->right));

    // Get balance factor to check if this node became unbalanced
    int balance = get_balance(node);

    // Left Left Case
    if (balance > 1 && key < node->left->key)
    {
        return rotate_right(node);
    }

    // Right Right Case
    if (balance < -1 && key > node->right->key)
    {
        return rotate_left(node);
    }

    // Left Right Case
    if (balance > 1 && key > node->left->key)
    {
        node->left = rotate_left(node->left);
        return rotate_right(node);
    }

    // Right Left Case
    if (balance < -1 && key < node->right->key)
    {
        node->right = rotate_right(node->right);
        return rotate_left(node);
    }

    return node;
}

void bst_append(struct BST *tree, uint32 key)
{
    tree->root = insert_node(tree->root, key);
}

static struct Node *find_min_node(struct Node *node)
{
    struct Node *current = node;
    while (current && current->left != NULL)
        current = current->left;
    return current;
}

static struct Node *delete_node(struct Node *root, uint32 key)
{
    // Standard BST delete
    if (root == NULL)
        return root;

    if (key < root->key)
    {
        root->left = delete_node(root->left, key);
    }
    else if (key > root->key)
    {
        root->right = delete_node(root->right, key);
    }
    else
    {
        // Node with only one child or no child
        if (root->left == NULL || root->right == NULL)
        {
            struct Node *temp = root->left ? root->left : root->right;

            if (temp == NULL)
            {
                // No child case
                temp = root;
                root = NULL;
            }
            else
            {
                // One child case
                *root = *temp; // Copy contents
            }
#ifdef FOS_KERNEL
            kfree(temp);
#else
            free(temp);
#endif
        }
        else
        {
            // Node with two children
            struct Node *temp = find_min_node(root->right);
            root->key = temp->key;
            root->right = delete_node(root->right, temp->key);
        }
    }

    // If the tree had only one node
    if (root == NULL)
        return root;

    // Update height
    root->height = 1 + max(height(root->left), height(root->right));

    // Get balance factor
    int balance = get_balance(root);

    // Left Left Case
    if (balance > 1 && get_balance(root->left) >= 0)
    {
        return rotate_right(root);
    }

    // Left Right Case
    if (balance > 1 && get_balance(root->left) < 0)
    {
        root->left = rotate_left(root->left);
        return rotate_right(root);
    }

    // Right Right Case
    if (balance < -1 && get_balance(root->right) <= 0)
    {
        return rotate_left(root);
    }

    // Right Left Case
    if (balance < -1 && get_balance(root->right) > 0)
    {
        root->right = rotate_right(root->right);
        return rotate_left(root);
    }

    return root;
}

void bst_remove(struct BST *tree, uint32 key)
{
    tree->root = delete_node(tree->root, key);
}

int bst_find_max(struct BST *tree)
{
    if (tree->root == NULL)
        return 0;
    struct Node *current = tree->root;
    while (current->right != NULL)
        current = current->right;
    return current->key;
}

int bst_find(struct BST *tree, uint32 key)
{
    struct Node *current = tree->root;
    while (current != NULL)
    {
        if (key == current->key)
            return 1;
        else if (key < current->key)
            current = current->left;
        else
            current = current->right;
    }
    return 0;
}

static void print_inorder(struct Node *node)
{
    if (node == NULL)
        return;
    print_inorder(node->left);
    cprintf("%d ", node->key);
    print_inorder(node->right);
}

int bst_find_max_lt_value(struct BST *tree, uint32 key)
{
    struct Node *current = tree->root;
    struct Node *candidate = NULL;

    while (current != NULL)
    {
        if (current->key == key)
        {
            // Node found. If it has a left subtree, max of that is the predecessor
            if (current->left != NULL)
            {
                current = current->left;
                while (current->right != NULL)
                    current = current->right;
                return current->key;
            }
            break;
        }
        else if (key < current->key)
        {
            current = current->left;
        }
        else
        {
            // Going right: current node is smaller than key, so it's a candidate
            candidate = current;
            current = current->right;
        }
    }

    if (candidate != NULL)
        return candidate->key;
    return 0;
}

void bst_print(struct BST *tree)
{
    print_inorder(tree->root);
    cprintf("\n");
}