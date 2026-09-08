/* pc_dvd.c — the disc drive, served from a disc image file.
 *
 * dvdlow.c is the bottom of the DVD stack: it programs the drive interface
 * registers and completes each request from an interrupt. Everything above it
 * -- dvd.c's state machine, dvdfs.c's path lookups, dvdqueue.c -- is ordinary
 * logic and compiles unmodified. So this replaces only the hardware layer, and
 * the rest of the SDK reaches the file without knowing anything changed.
 *
 * ---------------------------------------------------------------------------
 * The file system table
 *
 * A GameCube disc opens with a header. Three big-endian words at 0x420 name
 * where the rest of it lives:
 *
 *     0x420  offset of the executable (main.dol)
 *     0x424  offset of the file system table
 *     0x428  size of the file system table
 *
 * The FST is an array of 12-byte entries followed by a string table of names.
 * Entry zero is the root directory, and its third word is the total entry
 * count -- which is how the table's own length is discovered.
 *
 * On hardware the IPL copies this into memory and leaves a pointer in
 * OSBootInfo. Nothing does that here, so pc_dvd_mount does it: read the FST,
 * publish it, and dvdfs.c finds a populated file system where pc_bootinfo.c
 * had left an empty one.
 *
 * ---------------------------------------------------------------------------
 * Byte order
 *
 * The disc is big-endian, because the console is. A host is not, so every
 * multi-byte field read out of the image is backwards. This is the endianness
 * problem the port has been able to ignore until now, and it arrives the
 * moment real data does.
 *
 * The FST is the easy case: a fixed schema of three 32-bit words per entry, so
 * swapping it is a loop. The string table that follows is bytes and is left
 * alone. File *contents* are a different matter -- every .dat the game loads
 * is full of big-endian floats and pointers, and swapping those needs a schema
 * per format. That work starts where this file ends.
 */
#include "pc_sys.h"

#include <dolphin/dvd.h>
#include <dolphin/os.h>

/* dvd.h spells the low-level completion callback inline in every prototype
   rather than naming it; give it a name here. */
typedef void (*DVDLowCallback)(unsigned long);

#define GC_RAM_CACHED 0x80000000UL

/* Where the FST is published. Sits above OSBootInfo and below the arena, which
   pc_os.c starts at +0x00100000, leaving just under a megabyte. */
#define PC_FST_ADDR  (GC_RAM_CACHED + 0x00010000UL)
#define PC_FST_LIMIT 0x000F0000UL

#define DISC_HEADER_FST_OFFSET 0x424
#define DISC_HEADER_FST_SIZE   0x428

static int disc_fd = -1;

/* Candidate paths, in order. A port with a command line would take one. */
static const char* const disc_paths[] = {
    "game.iso", "melee.iso", "disc.iso", "game.gcm", "melee.gcm", 0
};

static unsigned int be32(const unsigned char* p)
{
    return ((unsigned int) p[0] << 24) | ((unsigned int) p[1] << 16) |
           ((unsigned int) p[2] << 8) | (unsigned int) p[3];
}

/* On-disk and low-memory layouts use explicit widths, never the SDK's u32.
   dolphin/types.h defines u32 as `unsigned long`, which is four bytes in the
   GameCube's ABI and eight on an LP64 host -- so a struct built from it
   silently doubles in size there, and a table walked with its sizeof reads
   garbage. These describe bytes on a disc, so they are pinned. */
typedef unsigned int pc_u32;

typedef struct {
    pc_u32 isDirAndStringOff;
    pc_u32 parentOrPosition;
    pc_u32 nextEntryOrLength;
} pc_fst_entry;

typedef struct {
    unsigned char diskID[0x20];
    pc_u32 magic, version, memorySize, consoleType;
    void* arenaLo;
    void* arenaHi;
    void* FSTLocation;
    pc_u32 FSTMaxLength;
} pc_boot_info;

/* Decimal, for the diagnostics below. Neither build can assume printf: the
   freestanding one links no libc at all. */
static void log_uint(unsigned int n)
{
    char digits[12];
    char out[13];
    int d = 0, j = 0;

    if (n == 0) {
        pc_sys_log("0");
        return;
    }
    while (n > 0 && d < 12) {
        digits[d++] = (char) ('0' + (n % 10));
        n /= 10;
    }
    while (d > 0) {
        out[j++] = digits[--d];
    }
    out[j] = 0;
    pc_sys_log(out);
}

/* --- byte order, part two: file contents ---------------------------------
 *
 * The FST above is a fixed schema, so swapping it is a loop. Contents are not.
 * A .dat interleaves big-endian floats, pointers and byte arrays, and only the
 * format knows which word is which -- swapping all of it is as wrong as
 * swapping none.
 *
 * The swap also cannot go where the reads are consumed. synth.c and every
 * other file under src/ is decompiled game code that has to keep matching the
 * original, so it must see the data already in host order. That leaves this
 * layer, which has a problem of its own: DVDLowRead is handed an absolute disc
 * offset and no idea which file it belongs to.
 *
 * The FST closes that gap. It maps offsets to files, so a read can be resolved
 * back to a name and the name selects the schema. Formats with no schema yet
 * are passed through untouched, exactly as every file was until now.
 */

static unsigned int fst_count; /* entries; 0 until a disc is mounted */

/* Resolve an absolute disc offset to the file holding it. Returns the file's
   name and sets *rel to the offset within it, or NULL when the read lies
   outside every file -- the disc header and the FST itself do. */
static const char* fst_file_at(unsigned int offset, unsigned int* rel)
{
    const unsigned char* fst = (const unsigned char*) PC_FST_ADDR;
    const char* strings;
    unsigned int i;

    if (fst_count == 0) {
        return 0;
    }
    strings = (const char*) (fst + fst_count * sizeof(pc_fst_entry));

    /* Entry zero is the root directory, so the walk starts at one. A linear
       scan of ~1200 entries per read is not free, but correctness first: the
       alternative is an index built at mount, and nothing here is hot enough
       yet to have earned one. */
    for (i = 1; i < fst_count; i++) {
        const pc_fst_entry* e =
            (const pc_fst_entry*) (fst + i * sizeof(pc_fst_entry));
        unsigned int pos, len;

        if ((e->isDirAndStringOff >> 24) != 0) {
            continue; /* a directory: no extent of its own */
        }
        pos = e->parentOrPosition;
        len = e->nextEntryOrLength;
        /* Subtract rather than add: pos + len can wrap on a corrupt image and
           swallow offsets that belong to no file at all. */
        if (offset >= pos && offset - pos < len) {
            *rel = offset - pos;
            return strings + (e->isDirAndStringOff & 0x00FFFFFFU);
        }
    }
    return 0;
}

static int name_ends_with(const char* s, const char* suffix)
{
    unsigned int ls = 0, lx = 0, i;

    while (s[ls] != 0) {
        ls++;
    }
    while (suffix[lx] != 0) {
        lx++;
    }
    if (lx > ls) {
        return 0;
    }
    for (i = 0; i < lx; i++) {
        char a = s[ls - lx + i];
        if (a >= 'A' && a <= 'Z') {
            a = (char) (a - 'A' + 'a');
        }
        if (a != suffix[i]) {
            return 0;
        }
    }
    return 1;
}

/* Swap a run of 32-bit words in place. The SDK issues reads into 32-byte
   aligned buffers, so the word accesses below are aligned. */
static void swap_words(unsigned char* p, unsigned int words)
{
    unsigned int i;

    for (i = 0; i < words; i++) {
        unsigned char* w = p + i * 4;
        pc_u32 v = be32(w);
        *(pc_u32*) w = v;
    }
}

/* A sound sample map opens with a header the loader reads into a static array
   of eight words (hsd_SynthSFXLoadBuf, synth.static.h) and then indexes for
   sizes and counts. Those eight words are the entire schema for this read; the
   ADPCM samples that follow are bytes and must not be touched. */
#define SFX_HEADER_BYTES 0x20

/* Returns 1 when a schema claimed the read, 0 when the format is still
   unconverted. */
static int swap_contents(const char* name, unsigned int rel,
                         unsigned char* addr, unsigned int length)
{
    if (name_ends_with(name, ".ssm")) {
        if (rel == 0 && length >= SFX_HEADER_BYTES) {
            swap_words(addr, SFX_HEADER_BYTES / 4);
            return 1;
        }
        /* Past the header: the entry table and the samples after it. The
           table is big-endian too and still has no schema, so it reports as
           unconverted rather than claiming a swap that did not happen. */
        return 0;
    }
    return 0;
}

#ifdef PC_DVD_TRACE
static void trace_read(const char* name, unsigned int rel, unsigned int length,
                       int claimed)
{
    pc_sys_log(claimed ? "dvd: swap " : "dvd: pass ");
    pc_sys_log(name);
    pc_sys_log(" +");
    log_uint(rel);
    pc_sys_log(" ");
    log_uint(length);
    pc_sys_log("\n");
}
#endif

/* Opens the disc image and publishes its file system table. Returns 0 when no
   image is found, in which case pc_bootinfo.c's empty file system stands and
   the game boots to the point of asking for data, as before. */
int pc_dvd_mount(void)
{
    pc_boot_info* info = (pc_boot_info*) GC_RAM_CACHED;
    unsigned char header[0x440];
    unsigned char* fst = (unsigned char*) PC_FST_ADDR;
    unsigned int fst_offset, fst_size, entries, i;
    int p;

    for (p = 0; disc_paths[p] != 0; p++) {
        disc_fd = pc_sys_open_ro(disc_paths[p]);
        if (disc_fd >= 0) {
            break;
        }
    }
    if (disc_fd < 0) {
        pc_sys_log("pc_dvd: no disc image found; file system stays empty\n");
        return 0;
    }
    pc_sys_log("pc_dvd: mounted ");
    pc_sys_log(disc_paths[p]);
    pc_sys_log("\n");

    if (pc_sys_pread(disc_fd, header, sizeof(header), 0) !=
        (long) sizeof(header)) {
        pc_sys_log("pc_dvd: short read on the disc header\n");
        return 0;
    }

    fst_offset = be32(header + DISC_HEADER_FST_OFFSET);
    fst_size = be32(header + DISC_HEADER_FST_SIZE);

    if (fst_size == 0 || fst_size > PC_FST_LIMIT) {
        pc_sys_log("pc_dvd: file system table missing or too large\n");
        return 0;
    }
    if (pc_sys_pread(disc_fd, fst, fst_size, fst_offset) != (long) fst_size) {
        pc_sys_log("pc_dvd: short read on the file system table\n");
        return 0;
    }

    /* Entry zero's third word is the entry count, and it needs swapping before
       it can say how much of what follows is entries rather than names. */
    entries = be32(fst + 8);
    /* Divide rather than multiply: the entry count comes straight off the disc,
       so a corrupt or hostile image can pick a value whose product with the
       entry size wraps a 32-bit multiply and passes a bounds check it should
       fail. Dividing the known-good size cannot overflow. */
    if (entries == 0 || entries > fst_size / sizeof(pc_fst_entry)) {
        pc_sys_log("pc_dvd: file system table is malformed\n");
        return 0;
    }

    /* Swap the entries in place; the string table after them is bytes. */
    for (i = 0; i < entries * 3; i++) {
        unsigned char* w = fst + i * 4;
        pc_u32 v = be32(w);
        *(pc_u32*) w = v;
    }

    /* The disc ID the SDK checks lives in the first 0x20 bytes. */
    for (i = 0; i < 0x20; i++) {
        info->diskID[i] = header[i];
    }
    info->FSTLocation = fst;
    info->FSTMaxLength = fst_size;
    fst_count = entries;

    /* Report what was parsed. A wrong entry count is the first sign the header
       offsets or the byte swap are off, and it is otherwise invisible until a
       lookup fails much later for reasons that look unrelated. */
    pc_sys_log("pc_dvd: file system table, ");
    log_uint(entries);
    pc_sys_log(" entries\n");

    return 1;
}

/* --- the hardware layer, reading from the file --- */

int DVDLowRead(void* addr, u32 length, u32 offset, DVDLowCallback callback)
{
    long got;

    if (disc_fd < 0) {
        if (callback != NULL) {
            callback(DVD_INTTYPE_DE); /* drive error: nothing to read from */
        }
        return 0;
    }

    got = pc_sys_pread(disc_fd, addr, length, offset);
    if (got != (long) length) {
        if (callback != NULL) {
            callback(DVD_INTTYPE_DE);
        }
        return 0;
    }

    /* Put the bytes into host order before anyone above sees them. Reads that
       resolve to no file -- the header, the FST -- are left alone. */
    {
        unsigned int rel = 0;
        const char* name = fst_file_at((unsigned int) offset, &rel);
        if (name != 0) {
            int claimed =
                swap_contents(name, rel, (unsigned char*) addr,
                              (unsigned int) length);
            (void) claimed;
#ifdef PC_DVD_TRACE
            trace_read(name, rel, (unsigned int) length, claimed);
#endif
        }
    }

    /* The drive would raise a transfer-complete interrupt. Reads here are
       synchronous, so the completion is delivered before returning -- callers
       above are written to tolerate it, since the SDK's own fast path can
       complete a cached read the same way. */
    if (callback != NULL) {
        callback(DVD_INTTYPE_TC);
    }
    return 1;
}

int DVDLowSeek(u32 offset, DVDLowCallback callback)
{
    (void) offset; /* positional reads make seeking meaningless */
    if (callback != NULL) {
        callback(DVD_INTTYPE_TC);
    }
    return 1;
}

int DVDLowReadDiskID(DVDDiskID* diskID, DVDLowCallback callback)
{
    unsigned char* dst = (unsigned char*) diskID;
    long got = -1;

    if (disc_fd >= 0) {
        got = pc_sys_pread(disc_fd, dst, 0x20, 0);
    }
    if (callback != NULL) {
        callback(got == 0x20 ? DVD_INTTYPE_TC : DVD_INTTYPE_DE);
    }
    return got == 0x20;
}

/* The drive is always present, spun up, and idle. */
int DVDLowWaitCoverClose(DVDLowCallback callback)
{
    if (callback != NULL) {
        callback(DVD_INTTYPE_CVR);
    }
    return 1;
}

u32 DVDLowGetCoverStatus(void) { return 2; /* closed */ }

int DVDLowStopMotor(DVDLowCallback callback)
{
    if (callback != NULL) {
        callback(DVD_INTTYPE_TC);
    }
    return 1;
}

int DVDLowRequestError(DVDLowCallback callback)
{
    if (callback != NULL) {
        callback(DVD_INTTYPE_TC);
    }
    return 1;
}

int DVDLowInquiry(DVDDriveInfo* info, DVDLowCallback callback)
{
    (void) info;
    if (callback != NULL) {
        callback(DVD_INTTYPE_TC);
    }
    return 1;
}

/* Streamed audio -- the disc's own ADPCM tracks, which Melee does not use. */
int DVDLowAudioStream(u32 subcmd, u32 length, u32 offset,
                      DVDLowCallback callback)
{
    (void) subcmd; (void) length; (void) offset;
    if (callback != NULL) {
        callback(DVD_INTTYPE_TC);
    }
    return 1;
}

int DVDLowRequestAudioStatus(u32 subcmd, DVDLowCallback callback)
{
    (void) subcmd;
    if (callback != NULL) {
        callback(DVD_INTTYPE_TC);
    }
    return 1;
}

int DVDLowAudioBufferConfig(int enable, u32 size, DVDLowCallback callback)
{
    (void) enable; (void) size;
    if (callback != NULL) {
        callback(DVD_INTTYPE_TC);
    }
    return 1;
}

void DVDLowReset(void) { }
int DVDLowBreak(void) { return 1; }

static DVDLowCallback reset_cover_callback;

DVDLowCallback DVDLowSetResetCoverCallback(DVDLowCallback callback)
{
    DVDLowCallback prev = reset_cover_callback;
    reset_cover_callback = callback;
    return prev;
}

DVDLowCallback DVDLowClearCallback(void) { return NULL; }

void __DVDInitWA(void) { }

/* Nothing raises a drive interrupt: every request completes inside its own
   call, so there is never a pending one to service. */
void __DVDInterruptHandler(s16 interrupt, OSContext* context)
{
    (void) interrupt;
    (void) context;
}
