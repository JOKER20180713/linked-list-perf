#include "link.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(LinkData) >= 32 && sizeof(LinkData) <= 512,
               "This assignment targets a 64-bit ABI with a 32..512-byte header");

static int valid_data(int size, const char *mem)
{
    return size > 0 && size <= LINK_MAX_DATA_SIZE && mem != NULL;
}

static LinkData *new_node(int size, const char *mem)
{
    if (!valid_data(size, mem)) {
        errno = EINVAL;
        return NULL;
    }
    /* Header and payload share one allocation, improving spatial locality. */
    LinkData *node = malloc(sizeof(*node) + (size_t)size);
    if (node == NULL)
        return NULL;
    node->prev = node;
    node->next = node;
    node->type = 0;
    node->dataSize = size;
    node->data = (char *)(node + 1);
    memcpy(node->data, mem, (size_t)size);
    return node;
}

LinkData *InitLink(int size, char *mem)
{
    return new_node(size, mem);
}

int AppendNodeChecked(LinkData *firstNode, int size, const char *mem)
{
    if (firstNode == NULL) {
        errno = EINVAL;
        return -1;
    }
    LinkData *node = new_node(size, mem);
    if (node == NULL)
        return -1;
    LinkData *tail = firstNode->prev;
    node->prev = tail;
    node->next = firstNode;
    tail->next = node;
    firstNode->prev = node;
    return 0;
}

void AppendNode(LinkData *firstNode, int size, char *mem)
{
    /* The required void interface reports errors through errno. */
    (void)AppendNodeChecked(firstNode, size, mem);
}

LinkData *FindNodeBaseline(LinkData *firstNode, int size, char *mem)
{
    if (firstNode == NULL || !valid_data(size, mem))
        return NULL;
    LinkData *node = firstNode;
    do {
        if (node->dataSize == size &&
            memcmp(node->data, mem, (size_t)size) == 0)
            return node;
        node = node->next;
    } while (node != firstNode);
    return NULL;
}

LinkData *FindNode(LinkData *firstNode, int size, char *mem)
{
    if (firstNode == NULL || !valid_data(size, mem))
        return NULL;

    /* Compare actual bytes, not a hash or a cached fingerprint.
       memcpy avoids unaligned accesses and strict-aliasing violations.
       Load the query prefix once, outside the pointer-chasing loop. */
    uint64_t prefix = 0;
    if (size >= (int)sizeof(prefix))
        memcpy(&prefix, mem, sizeof(prefix));
    LinkData *node = firstNode;
    do {
        if (node->dataSize == size) {
            if (size < (int)sizeof(prefix)) {
                if (memcmp(node->data, mem, (size_t)size) == 0)
                    return node;
            } else {
                uint64_t candidate;
                memcpy(&candidate, node->data, sizeof(candidate));
                if (candidate == prefix &&
                    (size == (int)sizeof(prefix) ||
                     memcmp(node->data + sizeof(prefix), mem + sizeof(prefix),
                            (size_t)size - sizeof(prefix)) == 0))
                    return node;
            }
        }
        node = node->next;
    } while (node != firstNode);
    return NULL;
}

void FreeLink(LinkData *firstNode)
{
    if (firstNode == NULL)
        return;
    LinkData *node = firstNode->next;
    while (node != firstNode) {
        LinkData *next = node->next;
        free(node);
        node = next;
    }
    free(firstNode);
}
