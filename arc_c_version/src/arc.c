#include <arc.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#define MAX(a, b) ( (a) > (b) ? (a) : (b) )
#define MIN(a, b) ( (a) < (b) ? (a) : (b) )

static void __arc_hash_init(struct __arc *cache)
{
    cache->hash.size = 3079;
    cache->hash.bucket = malloc(cache->hash.size * sizeof(struct __arc_list));
    for (int i = 0; i < cache->hash.size; ++i) {
        __arc_list_init(&cache->hash.bucket[i]);
    }
}

static void __arc_hash_insert(struct __arc *cache, const void *key, struct __arc_object *obj)
{
    unsigned long hash = cache->ops->hash(key) % cache->hash.size;
    __arc_list_prepend(&obj->hash, &cache->hash.bucket[hash]);
}

static struct __arc_object *__arc_hash_lookup(struct __arc *cache, const void *key)
{
    struct __arc_list *iter;
    unsigned long hash = cache->ops->hash(key) % cache->hash.size;
    
    __arc_list_each(iter, &cache->hash.bucket[hash]) {
        struct __arc_object *obj = __arc_list_entry(iter, struct __arc_object, hash);
        if (cache->ops->cmp(obj, key) == 0)
            return obj;
    }
    
    return NULL;
}

static void __arc_hash_fini(struct __arc *cache)
{
    free(cache->hash.bucket);
}

void __arc_object_init(struct __arc_object *obj, unsigned long size)
{
    obj->state = NULL;
    obj->size = size;
    
    __arc_list_init(&obj->head);
    __arc_list_init(&obj->hash);
}

static void __arc_balance(struct __arc *cache, unsigned long size);

static struct __arc_object *__arc_move(struct __arc *cache, struct __arc_object *obj, struct __arc_state *state)
{
    if (obj->state) {
        obj->state->size -= obj->size;
        __arc_list_remove(&obj->head);
    }
    
    if (state == NULL) {
        __arc_list_remove(&obj->hash);
        cache->ops->destroy(obj);
        
        return NULL;
    } else {
        if (state == &cache->mrug || state == &cache->mfug) {
            cache->ops->evict(obj);
        } else if (obj->state != &cache->mru && obj->state != &cache->mfu) {
            __arc_balance(cache, obj->size);
            if (cache->ops->fetch(obj)) {
                obj->state->size += obj->size;
                __arc_list_prepend(&obj->head, &obj->state->head);
                
                return NULL;
            }
        }
        
        __arc_list_prepend(&obj->head, &state->head);
        
        obj->state = state;
        obj->state->size += obj->size;
    }
    
    return obj;
}

static struct __arc_object *__arc_state_lru(struct __arc_state *state)
{
    struct __arc_list *head = state->head.prev;
    return __arc_list_entry(head, struct __arc_object, head);
}

static void __arc_balance(struct __arc *cache, unsigned long size)
{
    while (cache->mru.size + cache->mfu.size + size > cache->c) {
        if (cache->mru.size > cache->p) {
            struct __arc_object *obj = __arc_state_lru(&cache->mru);
            __arc_move(cache, obj, &cache->mrug);
        } else if (cache->mfu.size > 0) {
            struct __arc_object *obj = __arc_state_lru(&cache->mfu);
            __arc_move(cache, obj, &cache->mfug);
        } else {
            break;
        }
    }
    
    while (cache->mrug.size + cache->mfug.size > cache->c) {
        if (cache->mfug.size > cache->p) {
            struct __arc_object *obj = __arc_state_lru(&cache->mfug);
            __arc_move(cache, obj, NULL);
        } else if (cache->mrug.size > 0) {
            struct __arc_object *obj = __arc_state_lru(&cache->mrug);
            __arc_move(cache, obj, NULL);
        } else {
            break;
        }
    }
}


/* Create a new cache. */
struct __arc *__arc_create(struct __arc_ops *ops, unsigned long c)
{
    struct __arc *cache = malloc(sizeof(struct __arc));
    memset(cache, 0, sizeof(struct __arc));
    
    cache->ops = ops;
    
    __arc_hash_init(cache);
    
    cache->c = c;
    cache->p = c >> 1;
    
    __arc_list_init(&cache->mrug.head);
    __arc_list_init(&cache->mru.head);
    __arc_list_init(&cache->mfu.head);
    __arc_list_init(&cache->mfug.head);
    
    return cache;
}

void __arc_destroy(struct __arc *cache)
{
    struct __arc_list *iter;
    
    __arc_hash_fini(cache);
    
    __arc_list_each(iter, &cache->mrug.head) {
        struct __arc_object *obj = __arc_list_entry(iter, struct __arc_object, head);
        __arc_move(cache, obj, NULL);
    }
    __arc_list_each(iter, &cache->mru.head) {
        struct __arc_object *obj = __arc_list_entry(iter, struct __arc_object, head);
        __arc_move(cache, obj, NULL);
    }
    __arc_list_each(iter, &cache->mfu.head) {
        struct __arc_object *obj = __arc_list_entry(iter, struct __arc_object, head);
        __arc_move(cache, obj, NULL);
    }
    __arc_list_each(iter, &cache->mfug.head) {
        struct __arc_object *obj = __arc_list_entry(iter, struct __arc_object, head);
        __arc_move(cache, obj, NULL);
    }
    
    free(cache);
}

struct __arc_object *__arc_lookup(struct __arc *cache, const void *key)
{
    struct __arc_object *obj = __arc_hash_lookup(cache, key);
    
    if (obj) {
        if (obj->state == &cache->mru || obj->state == &cache->mfu) {
            return __arc_move(cache, obj, &cache->mfu);
        } else if (obj->state == &cache->mrug) {
            cache->p = MIN(cache->c, cache->p + MAX(cache->mfug.size / cache->mrug.size, 1));
            return __arc_move(cache, obj, &cache->mfu);
        } else if (obj->state == &cache->mfug) {
            cache->p = MAX(0, cache->p - MAX(cache->mrug.size / cache->mfug.size, 1));
            return __arc_move(cache, obj, &cache->mfu);
        } else {
            assert(0);
        }
    } else {
        obj = cache->ops->create(key);
        if (!obj)
            return NULL;
        
        __arc_hash_insert(cache, key, obj);
        return __arc_move(cache, obj, &cache->mru);
    }
}