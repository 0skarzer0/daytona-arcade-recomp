/***************************************************************************
    NAOMI: read-only files stored in the NetDIMM (or cartridge) memory,
    mounted at /dimm. See dimmfs.h.

    Archive layout (little endian, written by naomi_pack.py):

        0x00  char     magic[8]     "CBDIMMFS"
        0x08  uint32   version      1
        0x0C  uint32   count        number of entries
        0x10  entry    entries[count]
                 char     name[56]  path inside /dimm, no leading '/'
                 uint32   offset    from the start of the archive
                 uint32   size      in bytes

    Reading the cartridge port (as libnaomi does): write the low 16 bits of
    the address to 0xA05F7004, then the high bits with bit 15 set (auto
    increment) to 0xA05F7000, then read 16-bit words from 0xA05F7008.
***************************************************************************/

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include <kos/fs.h>
#include <kos/irq.h>
#include <kos/mutex.h>
#include <kos/nmmgr.h>
#include <kos/dbglog.h>

#include "naomi_dimmfs.h"

#define CART_OFFSETH_REG (*(volatile uint16_t *)0xA05F7000)
#define CART_OFFSETL_REG (*(volatile uint16_t *)0xA05F7004)
#define CART_DATA_REG    (*(volatile uint16_t *)0xA05F7008)

#define DIMMFS_ALIGN     0x10000
#define DIMMFS_NAME_MAX  56
#define CHUNK            4096     /* words read with interrupts off at a time */

typedef struct {
    char     name[DIMMFS_NAME_MAX];
    uint32_t offset;
    uint32_t size;
} dimmfs_entry_t;

typedef struct {
    const dimmfs_entry_t *entry;
    uint32_t pos;
} dimmfs_file_t;

static mutex_t         lock = MUTEX_INITIALIZER;
static uint32_t        archive_base;          /* cartridge address of the archive */
static dimmfs_entry_t *entries;
static uint32_t        entry_count;
static int             mounted;
static uint16_t        bounce[CHUNK / 2 + 2];
/* Files copied into main RAM at boot (dimmfs_cache): their reads are a memcpy, not PIO through the cartridge
 * port. Index parallel to entries; NULL when the file is read from the NetDIMM. */
static uint8_t       **resident;

/* Read len bytes (even) from cartridge address src (even) into dst (2-byte
   aligned). Interrupts are off only for one chunk at a time. */
static void cart_read_words(uint16_t *dst, uint32_t src, uint32_t len)
{
    while (len > 0) {
        uint32_t n = len > CHUNK ? CHUNK : len;
        irq_mask_t old = irq_disable();
        CART_OFFSETL_REG = src & 0xFFFF;
        CART_OFFSETH_REG = 0x8000 | ((src >> 16) & 0x0FFF);
        for (uint32_t i = 0; i < n; i += 2)
            *dst++ = CART_DATA_REG;
        irq_restore(old);
        src += n;
        len -= n;
    }
}

/* Any address, any length, any destination: through the bounce buffer */
static void cart_read_any(void *dst, uint32_t src, uint32_t len)
{
    uint8_t *out = (uint8_t *)dst;
    while (len > 0) {
        uint32_t start = src & ~1u;
        uint32_t skip  = src - start;
        uint32_t n     = len > CHUNK - 2 ? CHUNK - 2 : len;
        uint32_t span  = (skip + n + 1) & ~1u;
        cart_read_words(bounce, start, span);
        memcpy(out, (uint8_t *)bounce + skip, n);
        out += n;
        src += n;
        len -= n;
    }
}

static const dimmfs_entry_t *find(const char *fn)
{
    while (*fn == '/')
        fn++;
    for (uint32_t i = 0; i < entry_count; i++)
        if (strncmp(entries[i].name, fn, DIMMFS_NAME_MAX) == 0)
            return &entries[i];
    return NULL;
}

static void *dimmfs_open(vfs_handler_t *vfs, const char *fn, int mode)
{
    (void)vfs;
    if ((mode & O_MODE_MASK) != O_RDONLY || (mode & O_DIR)) {
        errno = EROFS;
        return NULL;
    }
    const dimmfs_entry_t *e = find(fn);
    if (!e) {
        errno = ENOENT;
        return NULL;
    }
    dimmfs_file_t *f = (dimmfs_file_t *)malloc(sizeof(dimmfs_file_t));
    if (!f) {
        errno = ENOMEM;
        return NULL;
    }
    f->entry = e;
    f->pos = 0;
    return f;
}

static int dimmfs_close(void *h)
{
    free(h);
    return 0;
}

static ssize_t dimmfs_read(void *h, void *buf, size_t bytes)
{
    dimmfs_file_t *f = (dimmfs_file_t *)h;
    uint32_t left = f->entry->size - f->pos;
    if (bytes > left)
        bytes = left;
    if (bytes == 0)
        return 0;

    const uint8_t *ram = resident ? resident[f->entry - entries] : NULL;
    if (ram) {
        memcpy(buf, ram + f->pos, bytes);
    } else {
        mutex_lock(&lock);
        cart_read_any(buf, archive_base + f->entry->offset + f->pos, bytes);
        mutex_unlock(&lock);
    }

    f->pos += bytes;
    return (ssize_t)bytes;
}

static off_t dimmfs_seek(void *h, off_t offset, int whence)
{
    dimmfs_file_t *f = (dimmfs_file_t *)h;
    off_t pos;
    switch (whence) {
        case SEEK_SET: pos = offset; break;
        case SEEK_CUR: pos = (off_t)f->pos + offset; break;
        case SEEK_END: pos = (off_t)f->entry->size + offset; break;
        default: errno = EINVAL; return -1;
    }
    if (pos < 0) { errno = EINVAL; return -1; }
    if (pos > (off_t)f->entry->size) pos = f->entry->size;
    f->pos = (uint32_t)pos;
    return pos;
}

static off_t dimmfs_tell(void *h)
{
    return ((dimmfs_file_t *)h)->pos;
}

static size_t dimmfs_total(void *h)
{
    return ((dimmfs_file_t *)h)->entry->size;
}

static void fill_stat(struct stat *st, const dimmfs_entry_t *e)
{
    memset(st, 0, sizeof(struct stat));
    st->st_mode  = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH;
    st->st_size  = e->size;
    st->st_nlink = 1;
}

static int dimmfs_stat(vfs_handler_t *vfs, const char *path, struct stat *st, int flag)
{
    (void)vfs; (void)flag;
    const dimmfs_entry_t *e = find(path);
    if (!e) {
        errno = ENOENT;
        return -1;
    }
    fill_stat(st, e);
    return 0;
}

static int dimmfs_fstat(void *h, struct stat *st)
{
    fill_stat(st, ((dimmfs_file_t *)h)->entry);
    return 0;
}

static vfs_handler_t vh = {
    .nmmgr = {
        .pathname = "/dimm",
        .pid      = 0,
        .version  = 0x00010000,
        .flags    = 0,
        .type     = NMMGR_TYPE_VFS,
        .list_ent = NMMGR_LIST_INIT
    },
    .open  = dimmfs_open,
    .close = dimmfs_close,
    .read  = dimmfs_read,
    .seek  = dimmfs_seek,
    .tell  = dimmfs_tell,
    .total = dimmfs_total,
    .stat  = dimmfs_stat,
    .fstat = dimmfs_fstat
};

int dimmfs_init(void)
{
    uint8_t hdr[0x500];
    uint32_t seg_offset, seg_length;
    char magic[8];
    uint32_t version, count;

    if (mounted)
        return (int)entry_count;

    /* NAOMI header: first program segment at 0x360 (offset, address, length) */
    cart_read_any(hdr, 0, sizeof(hdr));
    if (memcmp(hdr, "NAOMI", 5) != 0) {
        dbglog(DBG_INFO, "dimmfs: no NAOMI header in the cartridge space\n");
        return -1;
    }
    memcpy(&seg_offset, &hdr[0x360], 4);
    memcpy(&seg_length, &hdr[0x368], 4);
    archive_base = (seg_offset + seg_length + DIMMFS_ALIGN - 1) & ~(DIMMFS_ALIGN - 1);

    cart_read_any(magic, archive_base, 8);
    if (memcmp(magic, "CBDIMMFS", 8) != 0) {
        dbglog(DBG_INFO, "dimmfs: no archive at 0x%08lx\n", (unsigned long)archive_base);
        return -1;
    }
    cart_read_any(&version, archive_base + 8, 4);
    cart_read_any(&count, archive_base + 12, 4);
    if (version != 1 || count == 0 || count > 4096) {
        dbglog(DBG_ERROR, "dimmfs: bad archive (version %lu, %lu files)\n",
               (unsigned long)version, (unsigned long)count);
        return -1;
    }

    entries = (dimmfs_entry_t *)malloc(count * sizeof(dimmfs_entry_t));
    if (!entries)
        return -1;
    cart_read_any(entries, archive_base + 16, count * sizeof(dimmfs_entry_t));
    entry_count = count;

    if (nmmgr_handler_add(&vh.nmmgr) != 0) {
        free(entries);
        entries = NULL;
        entry_count = 0;
        return -1;
    }
    mounted = 1;
    dbglog(DBG_INFO, "dimmfs: %lu files at 0x%08lx mounted on /dimm\n",
           (unsigned long)count, (unsigned long)archive_base);
    return (int)count;
}

int dimmfs_mounted(void)
{
    return mounted;
}

int dimmfs_cache(const char *name)
{
    if (!mounted)
        return -1;
    const dimmfs_entry_t *e = find(name);
    if (!e)
        return -1;
    if (!resident && !(resident = (uint8_t **)calloc(entry_count, sizeof(uint8_t *))))
        return -1;
    const uint32_t i = (uint32_t)(e - entries);
    if (resident[i])
        return (int)e->size;
    uint8_t *ram = (uint8_t *)malloc(e->size ? e->size : 1);
    if (!ram)
        return -1;
    mutex_lock(&lock);
    cart_read_any(ram, archive_base + e->offset, e->size);
    mutex_unlock(&lock);
    resident[i] = ram;
    return (int)e->size;
}

int dimmfs_size(const char *name)
{
    const dimmfs_entry_t *e = mounted ? find(name) : NULL;
    return e ? (int)e->size : -1;
}

void dimmfs_lock(void) { mutex_lock(&lock); }
void dimmfs_unlock(void) { mutex_unlock(&lock); }
