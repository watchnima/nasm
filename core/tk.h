#ifndef NASM_MODEL_H
#define NASM_MODEL_H

#include "hashtbl.h"
#include "wdtree.h"

/* TKmodel: testing knowledge model 
 *  a weighted set of instruction scenarios.
 */
typedef struct TKmodel {
    double initP;
    bool diffSrcDest;
    union {
        WDTree *wdtree;
        struct {
            WDTree *wdsrctree;
            WDTree *wddesttree;
        };
    };
} TKmodel;

TKmodel *tkmodel_create(void);
bool request_initialize(const char *instName);
constVal *request_constVal(const char *instName, bool isDest);

extern struct hash_table hash_tks;

#endif