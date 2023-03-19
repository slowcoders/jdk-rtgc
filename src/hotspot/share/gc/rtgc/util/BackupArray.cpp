#if 0
#include "precompiled.hpp"

#include "oops/oop.inline.hpp"
#include "memory/iterator.inline.hpp"
#include "oops/instanceKlass.inline.hpp"
#include "oops/fieldStreams.inline.hpp"
#include "runtime/globals.hpp"
#include "runtime/fieldDescriptor.inline.hpp"

#include "gc/rtgc/rtgcGlobals.hpp"
#include "gc/rtgc/impl/GCRuntime.hpp"

typedef narrowOop BACKUP_ITEM_PTR;
static const int ITEMS_PER_PART = 256;
class BackupPart {
    int top;
    int bottom;
    BACKUP_ITEM_PTR items[];
};

class BackupArray {
    BackupPart* _parts[];
    BACKUP_ITEM_PTR* _activeArray;

    void erase(int idx);
    void erase(int idx, int len);
    void shift(int start, int len, int offset);
    BackupPart* getMutablePartByIndex(int idx)
};

BackupPart* BackupArray::getMutablePartByIndex(int idx) {
    int slot = idx / ITEMS_PER_PART;
    BackupPart* part = _parts[idx / ITEMS_PER_PART];
    if (part == NULL) {
        part = _parts[slot] = new BackupPart();
        part->top = part->_bottom = idx;
    }
    return part;
}

void BackupArray::erase(int idx) {
    BackupPart* part = getMutablePartByIndex(idx);
    if (part->top > idx) {
        part->top = idx;
    } else if (part->_bottom <= idx) {
        part->_bottom = idx + 1;
    } else {
        return;
    }
    part[idx % ITEMS_PER_PART] = activeArray[idx];
}

void BackupArray::erase(int idx, int len) {
    int end = idx + len;
    int last_slot = (end - 1) / ITEMS_PER_PART;
    int cur_slot = idx / ITEMS_PER_PART;

    while (true) {
        BackupPart* part = getMutablePartByIndex(idx);
        int head_len = part->top - idx;
        if (head_len > 0) {
            if (head_len >= len) {
                memcpy(&part[idx % ITEMS_PER_PART], &activeArray[idx], len);
                return;
            }
            memcpy(&part[idx % ITEMS_PER_PART], &activeArray[idx], head_len);
        }

        idx = part->_bottom;
        if (cur_slot < last_slot) {
            int foot_len = ITEMS_PER_PART - (idx % ITEMS_PER_PART);
            int offset = idx % ITEMS_PER_PART;
            memcpy(&part[offset], &activeArray[idx], foot_len);
            idx = (idx - offset) + ITEMS_PER_PART;
        } else {
            int foot_len = end - idx;
            if (foot_len > 0) {
                rt_assert(foot_len <= ITEMS_PER_PART);
                memcpy(&part[idx % ITEMS_PER_PART], &activeArray[idx], foot_len);
            }
        }
    }
}

#endif