/*
 * Copyright (c) 2017-2018 Bailey Thompson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdlib.h>
#include <memory.h>
#include <errno.h>
#include "unordered_map.h"

static const int STARTING_BUCKETS = 8;
static const double RESIZE_AT = 0.75;
static const double RESIZE_RATIO = 1.5;

struct _unordered_map {
    size_t key_size;
    size_t value_size;
    unsigned long (*hash)(const void *const key);
    int (*comparator)(const void *const one, const void *const two);
    int size;
    int capacity;
    struct node **buckets;
};

struct node {
    void *key;
    void *value;
    unsigned long hash;
    struct node *next;
};

/**
 * Initializes an unordered map, which is a collection of key-value pairs,
 * hashed by keys, keys are unique
 *
 * @param key_size   The size of each key in the unordered map.
 * @param value_size The size of each value in the unordered map.
 * @param hash       The hash function which computes the hash from the key.
 * @param comparator The comparator function which compares two keys.
 *
 * @return The newly-initialized unordered map, or NULL if memory allocation
 *         error.
 */
unordered_map unordered_map_init(const size_t key_size,
                                 const size_t value_size,
                                 unsigned long (*hash)(const void *const),
                                 int (*comparator)(const void *const,
                                                   const void *const))
{
    struct _unordered_map *const init = malloc(sizeof(struct _unordered_map));
    if (!init) {
        return NULL;
    }
    init->key_size = key_size;
    init->value_size = value_size;
    init->hash = hash;
    init->comparator = comparator;
    init->size = 0;
    init->capacity = STARTING_BUCKETS;
    init->buckets = calloc(STARTING_BUCKETS, sizeof(struct node *));
    if (!init->buckets) {
        free(init);
        return NULL;
    }
    return init;
}

/*
 * Adds the specified node to the map.
 */
static void unordered_map_add_item(unordered_map me, struct node *const add)
{
    const int index = (int) (add->hash % me->capacity);
    add->next = NULL;
    if (!me->buckets[index]) {
        me->buckets[index] = add;
        return;
    }
    struct node *traverse = me->buckets[index];
    while (traverse->next) {
        traverse = traverse->next;
    }
    traverse->next = add;
}

/**
 * Rehashes all the keys in the unordered map. Used when storing references and
 * changing the keys. This should rarely be used.
 *
 * @param me The unordered map to rehash.
 *
 * @return 0       No error.
 *         -ENOMEM Out of memory.
 */
int unordered_map_rehash(unordered_map me)
{
    struct node **old_buckets = me->buckets;
    me->buckets = calloc((size_t) me->capacity, sizeof(struct node *));
    if (!me->buckets) {
        me->buckets = old_buckets;
        return -ENOMEM;
    }
    for (int i = 0; i < me->capacity; i++) {
        struct node *traverse = old_buckets[i];
        while (traverse) {
            struct node *const backup = traverse->next;
            traverse->hash = me->hash(traverse->key);
            unordered_map_add_item(me, traverse);
            traverse = backup;
        }
    }
    free(old_buckets);
    return 0;
}

/**
 * Gets the size of the unordered map.
 *
 * @param me The unordered map to check.
 *
 * @return The size of the unordered map.
 */
int unordered_map_size(unordered_map me)
{
    return me->size;
}

/**
 * Determines whether or not the unordered map is empty.
 *
 * @param me The unordered map to check.
 *
 * @return If the unordered map is empty.
 */
bool unordered_map_is_empty(unordered_map me)
{
    return unordered_map_size(me) == 0;
}

/*
 * Increases the size of the map and redistributes the nodes.
 */
static int unordered_map_resize(unordered_map me)
{
    const int old_capacity = me->capacity;
    const int new_capacity = (int) (me->capacity * RESIZE_RATIO);
    struct node **old_buckets = me->buckets;
    me->buckets = calloc((size_t) new_capacity, sizeof(struct node *));
    if (!me->buckets) {
        me->buckets = old_buckets;
        return -ENOMEM;
    }
    me->capacity = new_capacity;
    for (int i = 0; i < old_capacity; i++) {
        struct node *traverse = old_buckets[i];
        while (traverse) {
            struct node *const backup = traverse->next;
            unordered_map_add_item(me, traverse);
            traverse = backup;
        }
    }
    free(old_buckets);
    return 0;
}

/*
 * Determines if an element is equal to the key.
 */
inline static bool unordered_map_is_equal(unordered_map me,
                                          const struct node *const item,
                                          const unsigned long hash,
                                          const void *const key)
{
    return item->hash == hash && me->comparator(item->key, key) == 0;
}

/*
 * Creates an element to add.
 */
static struct node *const unordered_map_create_element(unordered_map me,
                                                       const unsigned long hash,
                                                       const void *const key,
                                                       const void *const value)
{
    struct node *const init = malloc(sizeof(struct node));
    if (!init) {
        return NULL;
    }
    init->key = malloc(me->key_size);
    if (!init->key) {
        free(init);
        return NULL;
    }
    memcpy(init->key, key, me->key_size);
    init->value = malloc(me->value_size);
    if (!init->value) {
        free(init->key);
        free(init);
        return NULL;
    }
    memcpy(init->value, value, me->value_size);
    init->hash = hash;
    init->next = NULL;
    return init;
}

/**
 * Adds a key-value pair to the unordered map. If the unordered map already
 * contains the key, the value is updated to the new value.
 *
 * @param me    The unordered map to add to.
 * @param key   The key to add.
 * @param value The value to add.
 *
 * @return 0       No error.
 *         -ENOMEM Out of memory.
 */
int unordered_map_put(unordered_map me, void *const key, void *const value)
{

    const unsigned long hash = me->hash(key);
    const int index = (int) (hash % me->capacity);
    if (!me->buckets[index]) {
        me->buckets[index] = unordered_map_create_element(me, hash, key, value);
        if (!me->buckets[index]) {
            return -ENOMEM;
        }
    } else {
        struct node *traverse = me->buckets[index];
        if (unordered_map_is_equal(me, traverse, hash, key)) {
            memcpy(traverse->value, value, me->value_size);
            return 0;
        }
        while (traverse->next) {
            traverse = traverse->next;
            if (unordered_map_is_equal(me, traverse, hash, key)) {
                memcpy(traverse->value, value, me->value_size);
                return 0;
            }
        }
        traverse->next = unordered_map_create_element(me, hash, key, value);
        if (!traverse->next) {
            return -ENOMEM;
        }
    }
    me->size++;
    if (me->size >= RESIZE_AT * me->capacity) {
        return unordered_map_resize(me);
    }
    return 0;
}

/**
 * Gets the value associated with a key in the unordered map.
 *
 * @param value The value to copy to.
 * @param me    The unordered map to get from.
 * @param key   The key to search for.
 *
 * @return If the unordered map contained the key-value pair.
 */
bool unordered_map_get(void *const value, unordered_map me, void *const key)
{
    const unsigned long hash = me->hash(key);
    const int index = (int) (hash % me->capacity);
    struct node *traverse = me->buckets[index];
    while (traverse) {
        if (unordered_map_is_equal(me, traverse, hash, key)) {
            memcpy(value, traverse->value, me->value_size);
            return true;
        }
        traverse = traverse->next;
    }
    return false;
}

/**
 * Determines if the unordered map contains the specified key.
 *
 * @param me  The unordered map to check for the key.
 * @param key The key to check.
 *
 * @return If the unordered map contained the key.
 */
bool unordered_map_contains(unordered_map me, void *const key)
{
    const unsigned long hash = me->hash(key);
    const int index = (int) (hash % me->capacity);
    const struct node *traverse = me->buckets[index];
    while (traverse) {
        if (unordered_map_is_equal(me, traverse, hash, key)) {
            return true;
        }
        traverse = traverse->next;
    }
    return false;
}

/**
 * Removes the key-value pair from the unordered map if it contains it.
 *
 * @param me  The unordered map to remove an key from.
 * @param key The key to remove.
 *
 * @return If the unordered map contained the key.
 */
bool unordered_map_remove(unordered_map me, void *const key)
{
    const unsigned long hash = me->hash(key);
    const int index = (int) (hash % me->capacity);
    if (!me->buckets[index]) {
        return false;
    }
    struct node *traverse = me->buckets[index];
    if (unordered_map_is_equal(me, traverse, hash, key)) {
        me->buckets[index] = traverse->next;
        free(traverse->key);
        free(traverse->value);
        free(traverse);
        me->size--;
        return true;
    }
    while (traverse->next) {
        if (unordered_map_is_equal(me, traverse->next, hash, key)) {
            struct node *const backup = traverse->next;
            traverse->next = traverse->next->next;
            free(backup->key);
            free(backup->value);
            free(backup);
            me->size--;
            return true;
        }
        traverse = traverse->next;
    }
    return false;
}

/**
 * Clears the key-value pairs from the unordered map.
 *
 * @param me The unordered map to clear.
 *
 * @return 0       No error.
 *         -ENOMEM Out of memory.
 */
int unordered_map_clear(unordered_map me)
{
    struct node **temp =
            calloc((size_t) STARTING_BUCKETS, sizeof(struct node *));
    if (!temp) {
        return -ENOMEM;
    }
    for (int i = 0; i < me->capacity; i++) {
        struct node *traverse = me->buckets[i];
        while (traverse) {
            struct node *const backup = traverse;
            traverse = traverse->next;
            free(backup->key);
            free(backup->value);
            free(backup);
        }
        me->buckets[i] = NULL;
    }
    me->size = 0;
    me->capacity = STARTING_BUCKETS;
    free(me->buckets);
    me->buckets = temp;
    return 0;
}

/**
 * Frees the unordered map memory.
 *
 * @param me The unordered map to free from memory.
 *
 * @return NULL
 */
unordered_map unordered_map_destroy(unordered_map me)
{
    unordered_map_clear(me);
    free(me->buckets);
    free(me);
    return NULL;
}
