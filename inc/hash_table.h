#ifndef FOS_INC_HASHTABLE_H
#define FOS_INC_HASHTABLE_H

#include <inc/types.h>

#ifndef HASH_TABLE_SIZE
#define HASH_TABLE_SIZE 1000000
#endif

struct ValueNode
{
    void *value;
    struct ValueNode *next;
};

struct HashEntry
{
    uint32 key;
    struct ValueNode *values;
    struct HashEntry *next;
};

struct HashTable
{
    struct HashEntry *buckets[HASH_TABLE_SIZE];
};

void hash_init(struct HashTable *table);
void hash_insert(struct HashTable *table, uint32 key, void *value);
void hash_insert_static(struct HashTable *table, struct HashEntry *entry, struct ValueNode *valNode);
struct ValueNode *hash_search(struct HashTable *table, uint32 key);
void hash_delete_key(struct HashTable *table, uint32 key);
void hash_delete_value_first(struct HashTable *table, uint32 key);
void hash_delete_value(struct HashTable *table, uint32 key, void *value);
int hash_is_empty(struct HashTable *table);
int hash_key_has_values(struct HashTable *table, uint32 key);
int hash_key_has_value(struct HashTable *table, uint32 key, void *value);
void *hash_get_first_value(struct HashTable *table, uint32 key);
void hash_print(struct HashTable *table);

#endif
