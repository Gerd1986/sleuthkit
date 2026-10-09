#include "qnx6_bitmap.h"
#include <stdlib.h>
int tsk_qnx6_bitmap_raw_bit(const TSK_QNX6_PROBE_INFO *sb,
                            const TSK_QNX6_ROOT *bitmap_tree,
                            uint64_t data_block_index,
                            TSK_QNX6_READ_BLOCK read_block, void *context,
                            int *bit_value) {
    uint64_t bitmap_logical, physical;
    uint8_t *buf;
    size_t byte_offset;
    if (!sb || !bitmap_tree || !read_block || !bit_value ||
        sb->block_size<512 || sb->block_size>65536 ||
        (sb->block_size & (sb->block_size-1)) ||
        data_block_index>=sb->block_count) return 0;
    bitmap_logical=(data_block_index/8)/sb->block_size;
    byte_offset=(size_t)((data_block_index/8)%sb->block_size);
    if (!tsk_qnx6_map_block(sb,bitmap_tree,bitmap_logical,read_block,
                            context,&physical)) return 0;
    buf=(uint8_t *)malloc(sb->block_size);
    if (!buf) return 0;
    if (!read_block(context,physical,buf,sb->block_size)) {
        free(buf);
        return 0;
    }
    *bit_value=(buf[byte_offset] >> (data_block_index%8)) & 1;
    free(buf);
    return 1;
}
