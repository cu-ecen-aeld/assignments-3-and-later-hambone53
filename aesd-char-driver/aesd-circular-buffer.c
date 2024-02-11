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
#include <stdio.h>
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
    /**
    * TODO: implement per description
    */
    // Starting at the in offset, loop through the buffer and find the entry that contains the char_offset
    struct aesd_buffer_entry *entry = NULL;
    int i = buffer->out_offs;
    size_t char_offset_byte = 0;
    int index = 0;

    for ( i=0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
        index = (buffer->out_offs + i) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        if ( buffer->entry[index].buffptr != NULL ) {
            if ( char_offset_byte + buffer->entry[index].size > char_offset ) {
                // We have found the entry that contains the char_offset
                *entry_offset_byte_rtn = char_offset - char_offset_byte;
                entry = &buffer->entry[index];
                break;
            }
            char_offset_byte += buffer->entry[index].size;
        }
    }
    return entry;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    /**
    * TODO: implement per description
    */
    if ( buffer->entry[buffer->in_offs].buffptr == NULL ) {
        // We have an entry available assign it and move pointers
        // Update the buffptr value with the value from add_entry
        buffer->entry[buffer->in_offs].buffptr = add_entry->buffptr;
        buffer->entry[buffer->in_offs].size = add_entry->size;
        buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        if ( buffer->in_offs == buffer->out_offs ) {
            buffer->full = 1;
            //buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        } else {
            buffer->full = 0;
        }
    } else {
        // We are overwriting an entry, move the out offset
        buffer->entry[buffer->in_offs].buffptr = add_entry->buffptr;
        buffer->entry[buffer->in_offs].size = add_entry->size;
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
