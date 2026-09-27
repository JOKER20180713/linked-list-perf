#ifndef LINK_H
#define LINK_H

#include <stddef.h>

typedef struct LinkData {
    struct LinkData *prev;
    struct LinkData *next;
    int type;
    int dataSize;
    char *data;
} LinkData;

enum { LINK_MAX_DATA_SIZE = 512 };

LinkData *InitLink(int size, char *mem);
void AppendNode(LinkData *firstNode, int size, char *mem);
LinkData *FindNode(LinkData *firstNode, int size, char *mem);

/* Checked append for callers that must handle allocation failure. */
int AppendNodeChecked(LinkData *firstNode, int size, const char *mem);
LinkData *FindNodeBaseline(LinkData *firstNode, int size, char *mem);
void FreeLink(LinkData *firstNode);

#endif
