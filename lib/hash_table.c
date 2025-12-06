#include <inc/hash_table.h>
#include <inc/assert.h>
#include <kern/mem/kheap.h>
#include <kern/cons/console.h>

static uint32 hash_func(uint32 key)
{
    key ^= key >> 16;
    key *= 0x7feb352d;
    key ^= key >> 15;
    key *= 0x846ca68b;
    key ^= key >> 16;
    return key % HASH_TABLE_SIZE;
}

void hash_init(struct HashTable *table)
{
    for (int i = 0; i < HASH_TABLE_SIZE; i++)
        table->buckets[i] = NULL;
}

void hash_insert(struct HashTable *table, uint32 key, void *value)
{
    uint32 idx = hash_func(key);

    struct HashEntry *entry = table->buckets[idx];
    while (entry != NULL)
    {
        if (entry->key == key)
        {
            struct ValueNode *new_val = (struct ValueNode *)kmalloc(sizeof(struct ValueNode));
            assert(new_val != NULL);
            new_val->value = value;
            new_val->next = entry->values;
            entry->values = new_val;
            return;
        }
        entry = entry->next;
    }

    struct HashEntry *new_entry = (struct HashEntry *)kmalloc(sizeof(struct HashEntry));
    assert(new_entry != NULL);
    new_entry->key = key;
    new_entry->values = NULL;
    new_entry->next = table->buckets[idx];
    table->buckets[idx] = new_entry;
    struct ValueNode *new_val = (struct ValueNode *)kmalloc(sizeof(struct ValueNode));
    assert(new_val != NULL);
    new_val->value = value;
    new_val->next = NULL;
    new_entry->values = new_val;
    new_entry->values = new_val;
}

void hash_insert_static(struct HashTable *table, struct HashEntry *entry, struct ValueNode *valNode)
{
    if (entry == NULL || valNode == NULL)
        return;

    uint32 key = entry->key; // Assume key is set in entry
    uint32 idx = hash_func(key);

    // Check if key exists
    struct HashEntry *existing = table->buckets[idx];
    while (existing != NULL)
    {
        if (existing->key == key)
        {
            // Key exists, just add value
            valNode->value = valNode->value; // Assume set
            valNode->next = existing->values;
            existing->values = valNode;

            // NOTE: The passed 'entry' is unused here because we found an existing one.
            // In a static pool scenario, the caller should probably check existence first
            // OR we accept this 'waste' (entry unused but consumed from pool).
            // However, for kheap (Allocated VA), keys are unique.
            // For Allocator (Free VA), keys are unique.
            // So we will likely ALWAYS use the new entry.
            return;
        }
        existing = existing->next;
    }

    // Key not found, insert new entry
    entry->values = valNode;
    valNode->next = NULL;

    entry->next = table->buckets[idx];
    table->buckets[idx] = entry;
}

struct ValueNode *hash_search(struct HashTable *table, uint32 key)
{
    uint32 idx = hash_func(key);

    struct HashEntry *entry = table->buckets[idx];
    while (entry != NULL)
    {
        if (entry->key == key)
            return entry->values;
        entry = entry->next;
    }
    return NULL;
}

void hash_delete_key(struct HashTable *table, uint32 key)
{
    uint32 idx = hash_func(key);

    struct HashEntry *entry = table->buckets[idx];
    struct HashEntry *prev = NULL;

    while (entry != NULL)
    {
        if (entry->key == key)
        {
            struct ValueNode *val = entry->values;
            while (val != NULL)
            {
                struct ValueNode *tmp = val;
                val = val->next;
                kfree(tmp);
            }
            if (prev == NULL)
                table->buckets[idx] = entry->next;
            else
                prev->next = entry->next;

            kfree(entry);
            return;
        }
        prev = entry;
        entry = entry->next;
    }
}

void hash_delete_value(struct HashTable *table, uint32 key, void *value)
{
    uint32 idx = hash_func(key);

    struct HashEntry *entry = table->buckets[idx];
    while (entry != NULL)
    {
        if (entry->key == key)
        {
            struct ValueNode *val = entry->values;
            struct ValueNode *prev = NULL;
            while (val != NULL)
            {
                if (val->value == value)
                {
                    if (prev == NULL)
                        entry->values = val->next;
                    else
                        prev->next = val->next;

                    kfree(val);
                    return;
                }
                prev = val;
                val = val->next;
            }
            return;
        }
        entry = entry->next;
    }
}
void hash_delete_value_first(struct HashTable *table, uint32 key)
{
    uint32 idx = hash_func(key);

    struct HashEntry *entry = table->buckets[idx];
    while (entry != NULL)
    {
        if (entry->key == key)
        {
            if (entry->values == NULL)
                return;

            struct ValueNode *tmp = entry->values;
            entry->values = entry->values->next;
            kfree(tmp);

            return;
        }
        entry = entry->next;
    }
}

int hash_is_empty(struct HashTable *table)
{
    for (int i = 0; i < HASH_TABLE_SIZE; i++)
        if (table->buckets[i] != NULL)
            return 0;
    return 1;
}

int hash_key_has_values(struct HashTable *table, uint32 key)
{
    uint32 idx = hash_func(key);

    struct HashEntry *entry = table->buckets[idx];
    while (entry != NULL)
    {
        if (entry->key == key)
        {
            if (entry->values != NULL)
                return 1;
            else
                return 0;
        }
        entry = entry->next;
    }

    return 0;
}

int hash_key_has_value(struct HashTable *table, uint32 key, void *value)
{
    if (!table)
        return 0;

    uint32 index = key % HASH_TABLE_SIZE;
    struct HashEntry *entry = table->buckets[index];

    while (entry != NULL)
    {
        if (entry->key == key)
        {
            struct ValueNode *node = entry->values;
            while (node != NULL)
            {
                if (node->value == value)
                    return 1;
                node = node->next;
            }

            return 0;
        }

        entry = entry->next;
    }

    return 0;
}

void *hash_get_first_value(struct HashTable *table, uint32 key)
{
    uint32 index = hash_func(key);

    struct HashEntry *entry = table->buckets[index];

    while (entry != NULL)
    {
        if (entry->key == key)
        {
            if (entry->values != NULL)
                return (void *)(entry->values->value);

            return NULL;
        }
        entry = entry->next;
    }

    return NULL;
}

void hash_print(struct HashTable *table)
{
    for (int i = 0; i < HASH_TABLE_SIZE; i++)
    {
        struct HashEntry *entry = table->buckets[i];
        if (entry != NULL)
        {
            cprintf("[%d]: ", i);
            while (entry != NULL)
            {
                cprintf("key=%u -> ", entry->key);
                struct ValueNode *val = entry->values;
                while (val != NULL)
                {
                    cprintf("%p -> ", val->value);
                    val = val->next;
                }
                cprintf("NULL | ");
                entry = entry->next;
            }
            cprintf("\n");
        }
    }
}
