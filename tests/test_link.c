#include "link.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void check_ring(LinkData *head, size_t count)
{
    LinkData *node = head;
    for (size_t i = 0; i < count; ++i) {
        assert(node != NULL);
        assert(node->next->prev == node);
        assert(node->prev->next == node);
        node = node->next;
        assert(i + 1 == count || node != head);
    }
    assert(node == head);
    for (size_t i = 0; i < count; ++i)
        node = node->prev;
    assert(node == head);
}

int main(void)
{
    char input[514], other[514];
    memset(input, 0, sizeof(input));
    memset(other, 0, sizeof(other));
    assert(FindNode(NULL, 32, input) == NULL);
    assert(InitLink(0, input) == NULL && errno == EINVAL);
    assert(InitLink(-1, input) == NULL);
    assert(InitLink(513, input) == NULL);
    assert(InitLink(32, NULL) == NULL);
    assert(AppendNodeChecked(NULL, 32, input) == -1);
    FreeLink(NULL);

    /* Every length, including short and unaligned byte sequences.
       Prefix collisions must still compare all remaining bytes. */
    for (int size = 1; size <= 512; ++size) {
        memset(input, 0x61, sizeof(input));
        memset(other, 0x61, sizeof(other));
        input[1] = other[1] = 0; /* binary data containing NUL */
        other[size] ^= 1;
        LinkData *head = InitLink(size, input + 1);
        assert(head != NULL && head->prev == head && head->next == head);
        assert(head->dataSize == size && head->type == 0);
        assert(head->data != input + 1);
        assert(AppendNodeChecked(head, size, other + 1) == 0);
        LinkData *tail = head->prev;
        AppendNode(head, size, input + 1); /* duplicate: return the first node */
        check_ring(head, 3);
        assert(FindNode(head, size, input + 1) == head);
        assert(FindNode(head, size, other + 1) == tail);
        assert(FindNodeBaseline(head, size, other + 1) == tail);
        other[1] ^= 0x40;
        assert(FindNode(head, size, other + 1) == NULL);
        assert(FindNodeBaseline(head, size, other + 1) == NULL);
        assert(FindNode(head, size == 512 ? 511 : size + 1, input + 1) == NULL);
        assert(FindNode(head, size, NULL) == NULL);
        errno = 0;
        AppendNode(head, 513, input);
        assert(errno == EINVAL);
        check_ring(head, 3);
        /* Caller edits its buffer; stored data must remain owned by the list. */
        char original = head->data[0];
        input[1] ^= 0x20;
        assert(head->data[0] == original);
        FreeLink(head);
    }

    memset(input, 0, sizeof(input));
    LinkData *head = InitLink(32, input);
    assert(head != NULL);
    for (uint64_t i = 1; i < 10000; ++i) {
        memcpy(input, &i, sizeof(i));
        assert(AppendNodeChecked(head, 32, input) == 0);
    }
    check_ring(head, 10000);
    assert(FindNode(head, 32, input) == head->prev);
    assert(FindNodeBaseline(head, 32, input) == head->prev);
    uint64_t missing = 10000;
    memcpy(input, &missing, sizeof(missing));
    assert(FindNode(head, 32, input) == NULL);
    FreeLink(head);

    LinkData *many[1000];
    for (size_t i = 0; i < 1000; ++i) {
        many[i] = InitLink(512, input);
        assert(many[i] != NULL);
        assert(FindNode(many[i], 512, input) == many[i]);
    }
    for (size_t i = 0; i < 1000; ++i)
        FreeLink(many[i]);
    puts("PASS: circular links, all 512 sizes, exact binary comparison, ownership, 10000 nodes, 1000 lists");
    return 0;
}
