#include<assert.h>
#include<stdlib.h>
#include "hashtable.h"

// n must be a power of 2 because we use bitwise AND to get the hash value

static void h_init(HTab *htab , size_t n){ 
    assert(n > 0 && ((n - 1) & n) == 0); // meaning n is a power of 2
    htab->tab = (HNode **)calloc(sizeof(HNode *), n); // we are allocating a pointer to an array of n HNode pointers
    htab->mask = n - 1; // mask is the same as n - 1
    htab->size = 0; // size is the number of nodes in the hashtable
}

// hashtable insertion
static void h_insert(HTab *htab, HNode *node){
    size_t pos = node->hcode & htab->mask; // meaning in simple language we are doing a bitwise AND to get the hash value
    HNode *next = htab->tab[pos]; // next is the pointer to the next node in the hashtable
    node->next = next; // node->next is the pointer to the next node in the hashtable
    htab->tab[pos] = node; // htab->tab[pos] is the pointer to the next node in the hashtable
    htab->size++; // htab->size is the number of nodes in the hashtable
}


// hashtable look up subroutine.
// Pay attention to the return value. It returns the address of
// the parent pointer that owns the target node,
// which can be used to delete the target node.
static HNode **h_lookup(HTab *htab, HNode *key, bool (*eq)(HNode *, HNode *)){
    if (!htab->tab){ // check whether the hashtable is initialized
        return NULL;
    }
    size_t pos = key->hcode & htab->mask; // we are doing a bitwise AND to get the hash value
    HNode **from = &htab->tab[pos]; // from is the pointer to the next node in the hashtable
    for (HNode *cur; (cur = *from)!= NULL; from = &cur->next){ // we are doing a loop because we need to check the next node in the hashtable
       if (cur->hcode == key->hcode && eq(cur, key)){ // we are doing a comparison between the hash value of the current node and the hash value of the key
           return from; // we are returning the pointer to the parent pointer that owns the target node
       }
    }
    return NULL;
}

// delete a node from the chain
static HNode *h_detach(HTab *htab, HNode **from){
    HNode *node = *from; // node is the pointer to the node that we want to delete
    *from = node->next; // *from is the pointer to the next node in the hashtable
    htab->size--; // htab->size is the number of nodes in the hashtable
    return node; // we are returning the pointer to the node that we want to delete
}

const size_t k_resizing_work = 128; // constant work because we are using it to resize the hashtable

static void hm_help_resizing(HMap *hmap) { // Assists in resizing the hashtable (HMap) by moving nodes from ht2 to ht1.
    size_t nwork = 0; // meaning nwork is the number of times we have done a work
    while (nwork < k_resizing_work && hmap->ht2.size > 0) {
        // scan for nodes from ht2 and move them to ht1
        HNode **from = &hmap->ht2.tab[hmap->resizing_pos];
        if (!*from) {
            hmap->resizing_pos++;
            continue;
        }

        h_insert(&hmap->ht1, h_detach(&hmap->ht2, from));
        nwork++;
    }

    if (hmap->ht2.size == 0 && hmap->ht2.tab) {
        // done
        free(hmap->ht2.tab);
        hmap->ht2 = HTab{};
    }
}

static void hm_start_resizing(HMap *hmap){ // Starts the resizing process for HMap by creating a larger hashtable (ht1) and swapping it with ht2
    assert(hmap->ht2.tab == NULL);
    // create a bigger hashtable and swap them
    hmap->ht2 = hmap->ht1;
    h_init(&hmap->ht1, (hmap->ht1.mask + 1) * 2);
    hmap->resizing_pos = 0;
}


HNode *hm_lookup(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *)) { //  Looks up a node (key) in the hashtable (HMap) across both ht1 and ht2.
    hm_help_resizing(hmap); // we are using help resizing because we need to help resize the hashtable
    HNode **from = h_lookup(&hmap->ht1, key, eq);
    from = from ? from : h_lookup(&hmap->ht2, key, eq);
    return from ? *from : NULL;
}

const size_t k_max_load_factor = 8; // k max load factor meaning the maximum load factor of the hashtable

void hm_insert(HMap *hmap, HNode *node) { // insert the node into the hashtable
    if (!hmap->ht1.tab) { 
        h_init(&hmap->ht1, 4); // Initializes ht1 with a small size (4) if it hasn't been initialized.
    }
    h_insert(&hmap->ht1, node); // alls h_insert to insert node into ht1.

    if (!hmap->ht2.tab) {
        // check whether we need to resize
        size_t load_factor = hmap->ht1.size / (hmap->ht1.mask + 1); // Checks the load factor (ht1.size / (ht1.mask + 1)) and starts resizing (hm_start_resizing) if exceeded.
        if (load_factor >= k_max_load_factor) {
            hm_start_resizing(hmap); // Calls hm_help_resizing to assist in moving nodes from ht2 to ht1 if resizing has begun.
        }
    }
    hm_help_resizing(hmap);
}

HNode *hm_pop(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *)) { //Removes and returns a node (key) from the hashtable (HMap).
    hm_help_resizing(hmap); // Calls hm_help_resizing to ensure nodes are moved from ht2 to ht1 if needed.
    if (HNode **from = h_lookup(&hmap->ht1, key, eq)) { // Attempts to find and remove key from ht1 using h_lookup and h_detach
        return h_detach(&hmap->ht1, from);
    }
    if (HNode **from = h_lookup(&hmap->ht2, key, eq)) { //If not found in ht1, attempts to find and remove from ht2.
        return h_detach(&hmap->ht2, from); //Returns the removed node or NULL if not found in either hashtable.
    }
    return NULL;
}


size_t hm_size(HMap *hmap) { // return the total number of nodes in the hashtable (HMap)
    return hmap->ht1.size + hmap->ht2.size;
}

void hm_destroy(HMap *hmap) { // Destroys (frees memory) allocated for both ht1 and ht2 hashtables in HMap.
    free(hmap->ht1.tab);
    free(hmap->ht2.tab);
    *hmap = HMap{};
}