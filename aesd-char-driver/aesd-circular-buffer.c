/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    if ((buffer->in_offs == buffer->out_offs) && !buffer->full) {
        return NULL;
    }

    uint8_t aux = buffer->out_offs;
    do {
        if (char_offset < buffer->entry[aux].size) {
            *entry_offset_byte_rtn = char_offset;
            return &buffer->entry[aux];
        }
        char_offset -= buffer->entry[aux].size;
        aux++;
        if (aux == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
            aux = 0;
        }
    } while (aux != buffer->in_offs);
    return NULL;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* @return NULL or, if an existing entry at out_offs was replaced,
* the value of buffptr for the entry which was replaced (for use with dynamic allocation/free)
*/
const char *aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    const char *replaced_entry = NULL;

    // Check for null pointers
    if (!buffer || !add_entry) {
        return NULL;
    }

    if (buffer->full) {
        replaced_entry = buffer->entry[buffer->out_offs].buffptr;
        buffer->out_offs++;
        if (buffer->out_offs == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
            buffer->out_offs = 0;
        }
    }

    buffer->entry[buffer->in_offs] = *add_entry;

    buffer->in_offs++;
    if (buffer->in_offs == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
        buffer->in_offs = 0;
    }

    if (buffer->in_offs == buffer->out_offs) {
        buffer->full = true;
    } else {
        buffer->full = false;
    }

    return replaced_entry;
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
