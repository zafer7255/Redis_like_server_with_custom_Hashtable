#pragma once // meaning that this file is only included once in a single source file

#include <stddef.h>
#include <stdint.h>

// hashtable node, should be embedded into the payload
// meaning that the node is part of the payload
struct HNode {
    HNode *next = NULL;
    uint64_t hcode = 0;
};

struct HTab {
    HNode **tab = NULL; // meaning that the tab is part of the payload
    size_t mask = 0;
    size_t size = 0;
};

// the real hashtable interface.
// it uses 2 hashtables for progressive resizing.
struct HMap {
    HTab ht1;   // newer hashtable
    HTab ht2;   // older hashtable
    size_t resizing_pos = 0;
};

HNode *hm_lookup(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *));
void hm_insert(HMap *hmap, HNode *node);
HNode *hm_pop(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *));
size_t hm_size(HMap *hmap);
void hm_destroy(HMap *hmap);

