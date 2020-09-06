#ifndef _LIST_H_
#define _LIST_H_

#ifdef __cplusplus
extern "C" {
#endif

struct list_entry {
    struct list_entry *next;
    struct list_entry *prev;
};

inline void list_init(struct list_entry *head)
{
    head->next = head->prev = head;
}

inline bool list_empty(const struct list_entry *head)
{
    return (head->next == head);
}

inline bool list_remove_entry(struct list_entry *entry)
{
    struct list_entry *prev;
    struct list_entry *next;

    next = entry->next;
    prev = entry->prev;
    prev->next = next;
    next->prev = prev;

    return (next == prev);
}

inline struct list_entry *list_remove_head(struct list_entry *head)
{
    struct list_entry *next;
    struct list_entry *entry;

    entry = head->next;
    next = entry->next;
    head->next = next;
    next->prev = head;

    return entry;
}

inline struct list_entry *list_remove_tail(struct list_entry *head)
{
    struct list_entry *prev;
    struct list_entry *entry;

    entry = head->prev;
    prev = entry->prev;
    head->prev = prev;
    prev->next = head;

    return entry;
}

inline void list_insert_tail(struct list_entry *head, struct list_entry *entry)
{
    struct list_entry *prev;

    prev = head->prev;
    entry->next = head;
    entry->prev = prev;
    prev->next = entry;
    head->prev = entry;
}

inline void list_insert_head(struct list_entry *head, struct list_entry *entry)
{
    struct list_entry *next;

    next = head->next;
    entry->next = next;
    entry->prev = head;
    next->prev = entry;
    head->next = entry;
}

#define container_of(address, type, field) ((type *)( \
                                            (char*)(address) - \
                                            (char*)(&((type *)0)->field)))

#ifdef __cplusplus
}
#endif

#endif