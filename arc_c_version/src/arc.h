#ifndef __ARC_H__
#define __ARC_H__

#include <memory.h>
#include <stddef.h>

struct __arc_list {
    struct __arc_list *prev, *next;
};

#define __arc_list_entry(ptr, type, field) \
((type*) (((char*)ptr) - offsetof(type,field)))

#define __arc_list_each(pos, head) \
for (pos = (head)->next; pos != (head); pos = pos->next)

#define __arc_list_each_prev(pos, head) \
for (pos = (head)->prev; pos != (head); pos = pos->prev)

static inline void
__arc_list_init( struct __arc_list * head )
{
    head->next = head->prev = head;
}

static inline void
__arc_list_insert(struct __arc_list *list, struct __arc_list *prev, struct __arc_list *next)
{
    next->prev = list;
    list->next = next;
    list->prev = prev;
    prev->next = list;
}

static inline void
__arc_list_splice(struct __arc_list *prev, struct __arc_list *next)
{
    next->prev = prev;
    prev->next = next;
}


static inline void
__arc_list_remove(struct __arc_list *head)
{
    __arc_list_splice(head->prev, head->next);
    head->next = head->prev = NULL;
}

static inline void
__arc_list_prepend(struct __arc_list *head, struct __arc_list *list)
{
    __arc_list_insert(head, list, list->next);
}


struct __arc_hash {
    unsigned long size;
    struct __arc_list *bucket;
};

struct __arc_state {
    unsigned long size;
    struct __arc_list head;
};

struct __arc_object {
    struct __arc_state *state;
    struct __arc_list head, hash;
    unsigned long size;
};

struct __arc_ops {
    unsigned long (*hash) (const void *key);
    
    int (*cmp) (struct __arc_object *obj, const void *key);
    
    struct __arc_object *(*create) (const void *key);
    
    int (*fetch) (struct __arc_object *obj);
    
    void (*evict) (struct __arc_object *obj);
    
    void (*destroy) (struct __arc_object *obj);
};

struct __arc {
    struct __arc_ops *ops;
    struct __arc_hash hash;
    
    unsigned long c, p;
    struct __arc_state mrug, mru, mfu, mfug;
};

struct __arc *__arc_create(struct __arc_ops *ops, unsigned long c);
void __arc_destroy(struct __arc *cache);

void __arc_object_init(struct __arc_object *obj, unsigned long size);

struct __arc_object *__arc_lookup(struct __arc *cache, const void *key);


#endif /* __ARC_H__ */