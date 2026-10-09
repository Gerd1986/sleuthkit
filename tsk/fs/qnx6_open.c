/* Experimental QNX6 TSK entry point.
 * Explicit -f qnx6 only. Inode/dir operations intentionally report
 * unsupported until the full TSK_FS_INFO callbacks are implemented.
 */
#include "tsk_fs_i.h"
#include "qnx6_probe.h"
#include "qnx6_inode.h"
#include <limits.h>
#include <stdint.h>

typedef struct {
    TSK_IMG_INFO *image;
    TSK_OFF_T fs_offset;
    TSK_QNX6_PROBE_INFO probe;
} QNX6_READ_CONTEXT;

static int qnx6_read_block(void *opaque, uint64_t block,
                           uint8_t *dst, size_t length) {
    QNX6_READ_CONTEXT *ctx=(QNX6_READ_CONTEXT *)opaque;
    uint64_t image_offset;
    if (!ctx || !dst || length!=ctx->probe.block_size ||
        block > UINT64_MAX / length) return 0;
    image_offset=block*length;
    if ((uint64_t)ctx->fs_offset > UINT64_MAX-image_offset) return 0;
    image_offset+=(uint64_t)ctx->fs_offset;
    if (image_offset>(uint64_t)INT64_MAX ||
        image_offset>(uint64_t)ctx->image->size ||
        length>(uint64_t)ctx->image->size-image_offset) return 0;
    return tsk_img_read(ctx->image,(TSK_OFF_T)image_offset,
                        (char *)dst,length)==(ssize_t)length;
}

typedef struct {
    TSK_FS_INFO fs;
    QNX6_READ_CONTEXT io;
    TSK_QNX6_ROOT inode_tree;
} QNX6_FS_INFO;

static uint8_t qnx6_file_add_meta(TSK_FS_INFO *fs, TSK_FS_FILE *file,
                                   TSK_INUM_T addr) {
    QNX6_FS_INFO *qfs=(QNX6_FS_INFO *)fs;
    TSK_QNX6_INODE inode;
    TSK_FS_META *meta;
    uint16_t kind;
    if (!file || addr<1 || addr>fs->last_inum ||
        !tsk_qnx6_read_inode(&qfs->io.probe,&qfs->inode_tree,(uint32_t)addr,
                            qnx6_read_block,&qfs->io,&inode)) {
        tsk_error_reset();
        tsk_error_set_errno(TSK_ERR_FS_INODE_NUM);
        tsk_error_set_errstr("qnx6_file_add_meta: invalid or unreadable inode");
        return 1;
    }
    if (!file->meta) {
        file->meta=tsk_fs_meta_alloc(0);
        if (!file->meta) return 1;
    }
    meta=file->meta;
    tsk_fs_meta_reset(meta);
    kind=inode.mode & 0170000;
    switch(kind) {
    case 0040000: meta->type=TSK_FS_META_TYPE_DIR; break;
    case 0100000: meta->type=TSK_FS_META_TYPE_REG; break;
    case 0120000: meta->type=TSK_FS_META_TYPE_LNK; break;
    default: meta->type=TSK_FS_META_TYPE_UNDEF; break;
    }
    meta->addr=addr;
    meta->flags=(TSK_FS_META_FLAG_ENUM)(TSK_FS_META_FLAG_ALLOC|TSK_FS_META_FLAG_USED);
    meta->mode=(TSK_FS_META_MODE_ENUM)(inode.mode & 07777);
    meta->size=(TSK_OFF_T)inode.size;
    meta->mtime=(time_t)inode.mtime;
    meta->nlink=1;
    return 0;
}

static uint8_t qnx6_fsstat(TSK_FS_INFO *fs, FILE *out) {
    if (!fs || !out) return 1;
    tsk_fprintf(out, "FILE SYSTEM INFORMATION\\n");
    tsk_fprintf(out, "--------------------------------------------\\n");
    tsk_fprintf(out, "File System Type: QNX6 (experimental)\\n");
    tsk_fprintf(out, "Root Inode: %llu\\n", (unsigned long long)fs->root_inum);
    tsk_fprintf(out, "Inode Count: %llu\\n", (unsigned long long)fs->inum_count);
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
    QNX6_FS_INFO *qfs;
    TSK_OFF_T sb_offset;
    TSK_QNX6_ROOT inode_tree;
    TSK_QNX6_INODE root_inode;
    QNX6_READ_CONTEXT ctx;
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
    /* Validate the inode tree and root directory before accepting the image.
     * This is still not a complete TSK inode/dir driver. */
    if (!tsk_qnx6_parse_root(sb,sizeof(sb),72,&inode_tree)) {
        tsk_error_set_errno(TSK_ERR_FS_MAGIC);
        tsk_error_set_errstr("qnx6_open: invalid inode-tree root");
        return NULL;
    }
    ctx.image=img;
    ctx.fs_offset=offset;
    ctx.probe=probe;
    if (!tsk_qnx6_read_inode(&probe,&inode_tree,1,qnx6_read_block,&ctx,
                             &root_inode) ||
        (root_inode.mode & 0170000) != 0040000) {
        tsk_error_reset();
        tsk_error_set_errno(TSK_ERR_FS_MAGIC);
        tsk_error_set_errstr("qnx6_open: root inode is not a readable directory");
        return NULL;
    }
    fs = tsk_fs_malloc(sizeof(QNX6_FS_INFO));
    if (!fs) return NULL;
    qfs=(QNX6_FS_INFO *)fs;
    qfs->io=ctx;
    qfs->inode_tree=inode_tree;
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
    fs->file_add_meta = qnx6_file_add_meta;
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
