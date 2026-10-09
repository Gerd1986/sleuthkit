/* Experimental QNX6 TSK entry point.
 * Explicit -f qnx6 only. Inode/dir operations intentionally report
 * unsupported until the full TSK_FS_INFO callbacks are implemented.
 */
#include "tsk_fs_i.h"
#include "qnx6_probe.h"
#include <stdint.h>

static uint8_t qnx6_fsstat(TSK_FS_INFO *fs, FILE *out) {
    if (!fs || !out) return 1;
    tsk_fprintf(out, "FILE SYSTEM INFORMATION\\n");
    tsk_fprintf(out, "--------------------------------------------\\n");
    tsk_fprintf(out, "File System Type: QNX6 (experimental)\\n");
    tsk_fprintf(out, "Root Inode: %llu\\n", (unsigned long long)fs->root_inum);
    tsk_fprintf(out, "Inode Count: %" PRIuINUM "\\n", (unsigned long long)fs->inum_count);
    tsk_fprintf(out, "Block Size: %u\\n", fs->block_size);
    tsk_fprintf(out, "Block Count: %llu\\n", (unsigned long long)fs->block_count);
    tsk_fprintf(out, "NOTE: File and directory traversal not implemented in TSK.\\n");
    return 0;
}

TSK_FS_INFO *qnx6_open(TSK_IMG_INFO *img, TSK_OFF_T offset,
                       TSK_FS_TYPE_ENUM type, const char *password,
                       uint8_t test) {
    uint8_t sb[512];
    TSK_QNX6_PROBE_INFO probe;
    TSK_FS_INFO *fs;
    TSK_OFF_T sb_offset;
    (void)password;
    (void)test;
    tsk_error_reset();
    if (!img || type != TSK_FS_TYPE_QNX6 || offset < 0 ||
        offset > img->size || img->size - offset < 0x2000 + (TSK_OFF_T)sizeof(sb)) {
        tsk_error_set_errno(TSK_ERR_FS_ARG);
        tsk_error_set_errstr("qnx6_open: invalid image, type, or offset");
        return NULL;
    }
    sb_offset = offset + 0x2000;
    if (tsk_img_read(img, sb_offset, (char *)sb, sizeof(sb)) != (ssize_t)sizeof(sb) ||
        !tsk_qnx6_probe_superblock(sb, sizeof(sb), &probe)) {
        tsk_error_reset();
        tsk_error_set_errno(TSK_ERR_FS_MAGIC);
        tsk_error_set_errstr("qnx6_open: no valid QNX6 superblock at 0x2000");
        return NULL;
    }
    fs = tsk_fs_malloc(sizeof(TSK_FS_INFO));
    if (!fs) return NULL;
    fs->img_info = img;
    fs->offset = offset;
    fs->ftype = TSK_FS_TYPE_QNX6;
    fs->duname = "Block";
    fs->tag = TSK_FS_INFO_TAG;
    fs->block_size = probe.block_size;
    fs->block_count = probe.block_count;
    fs->first_block = 0;
    fs->last_block = fs->last_block_act = probe.block_count - 1;
    fs->dev_bsize = img->sector_size;
    fs->root_inum = 1;
    fs->first_inum = 1;
    fs->last_inum = probe.inode_count;
    fs->inum_count = probe.inode_count;
    fs->close = tsk_fs_nofs_close;
    fs->fsstat = qnx6_fsstat;
    fs->block_walk = tsk_fs_nofs_block_walk;
    fs->block_getflags = tsk_fs_nofs_block_getflags;
    fs->inode_walk = tsk_fs_nofs_inode_walk;
    fs->file_add_meta = tsk_fs_nofs_file_add_meta;
    fs->istat = tsk_fs_nofs_istat;
    fs->get_default_attr_type = tsk_fs_nofs_get_default_attr_type;
    fs->load_attrs = tsk_fs_nofs_make_data_run;
    fs->dir_open_meta = tsk_fs_nofs_dir_open_meta;
    fs->name_cmp = tsk_fs_nofs_name_cmp;
    fs->jblk_walk = tsk_fs_nofs_jblk_walk;
    fs->jentry_walk = tsk_fs_nofs_jentry_walk;
    fs->jopen = tsk_fs_nofs_jopen;
    return fs;
}
