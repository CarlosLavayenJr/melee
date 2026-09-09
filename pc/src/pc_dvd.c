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
#include "pc_watch.h"

/* dvd.h spells the low-level completion callback inline in every prototype
   rather than naming it; give it a name here. */
typedef void (*DVDLowCallback)(unsigned long);

/* Every DVDLow* entry point below used to call its completion back inline,
   before returning -- reads are synchronous here, so nothing seemed lost by
   skipping the wait real hardware would need. It cost correctness instead:
   callers write `DVDReadAsyncPrio(...); busy = 1;`, relying on the real
   ordering where the transfer -- and its completion callback -- genuinely
   happens later, after that assignment. Here the whole transfer, including
   arbitrarily deep synchronous re-entry (the completion starting another
   read, which completes and starts another, ...), finishes inside the
   DVDReadAsyncPrio call itself, so `busy = 1` runs *last* and stomps a
   completion the callback already correctly cleared to 0 minutes -- well,
   instructions -- earlier. HSD_DevComDVDWakeUp's type-0x21 path hits exactly
   this: confirmed by tracing HSD_DevCom_804D77F5 with a hardware watchpoint,
   which is how this got found at all.
   The fix mirrors pc_ar.c's ARAM completions: do the actual work (the read,
   the byte-order pass) synchronously, since that part is genuinely safe, but
   defer *invoking the callback* to the same interrupt-enable edge pc_ar_poll
   uses. Only one DVD command is ever outstanding at a time -- it is a single
   serial device on real hardware too -- so one pending slot is enough. */
static DVDLowCallback dvd_pending_cb;
static unsigned long dvd_pending_arg;
static int dvd_pending;
static int dvd_draining;

static void pc_dvd_defer(DVDLowCallback callback, unsigned long arg)
{
    if (callback == NULL) {
        return;
    }
    dvd_pending_cb = callback;
    dvd_pending_arg = arg;
    dvd_pending = 1;
}

/* Called from the same interrupt-enable edge as pc_ar_poll(); see pc_os.c. */
void pc_dvd_poll(void)
{
    if (dvd_draining) {
        return;
    }
    dvd_draining = 1;
    while (dvd_pending) {
        DVDLowCallback cb = dvd_pending_cb;
        unsigned long arg = dvd_pending_arg;
        dvd_pending = 0;
        dvd_pending_cb = NULL;
        cb(arg);
    }
    dvd_draining = 0;
}

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

static int name_eq(const char* a, const char* b)
{
    unsigned int i;

    for (i = 0; a[i] != 0 && b[i] != 0; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return a[i] == b[i];
}

static void name_copy(char* dst, const char* src, unsigned int cap)
{
    unsigned int i;

    for (i = 0; i + 1 < cap && src[i] != 0; i++) {
        dst[i] = src[i];
    }
    dst[i] = 0;
}

/* A sound sample map opens with a header the loader reads into a static array
   of eight words (hsd_SynthSFXLoadBuf, synth.static.h) and then indexes for
   sizes and counts. Those eight words are the entire schema for this read; the
   ADPCM samples that follow are bytes and must not be touched. */
#define SFX_HEADER_BYTES 0x20

/* The table after the header is a run of groups: a count word, a second word,
   then that many 0x40 records. synth.c walks it by reading the count and
   striding count * 0x40 + 8 bytes to the next group, so a big-endian count
   sends the walk somewhere arbitrary -- which is the shape of a hang rather
   than a crash.
 *
 * Swapping the counts alone makes that stride correct without touching the
 * records, whose field widths are not known: three words per record are u32
 * offsets that synth.c relocates, and the rest could be 16-bit or bytes.
 * Swapping them blind would produce a run that goes further and is quietly
 * wrong, which is worse than one that stops.
 *
 * Returns the number of groups walked, which the trace reports: short of the
 * expected count means a stride left the buffer, and the assumption is wrong.
 */
#define SFX_PREFIX_BYTES 0x10

static unsigned int swap_group_counts(unsigned char* addr, unsigned int length,
                                      unsigned int groups,
                                      const pc_u32* prefix)
{
    /* Stream coordinates, not buffer ones. The loader does not start its walk
       at this buffer: it copies four header words out ahead of the data and
       begins there (synth.c, where HSD_Synth_804D7734 is set), so the first
       0x10 bytes of the stream are those words and this buffer supplies
       everything from 0x10 on. Walking the buffer as if it began with a count
       reads eight bytes into a record instead. */
    unsigned int total = SFX_PREFIX_BYTES + length;
    unsigned int pos = 0, g;

    for (g = 0; g < groups; g++) {
        unsigned int n;

        if (pos + 8 <= SFX_PREFIX_BYTES) {
            /* Still in the header words, which were swapped with the header
               itself and need no second pass. */
            n = prefix[pos / 4];
        } else if (pos >= SFX_PREFIX_BYTES && pos + 8 <= total) {
            unsigned char* w = addr + (pos - SFX_PREFIX_BYTES);
            n = be32(w);
            *(pc_u32*) w = n;
        } else {
            break; /* straddles the join, or runs off the end */
        }
        /* Divide rather than multiply: a count straight off the disc can pick
           a value whose product with the record size wraps. */
        if (n > (total - pos - 8) / 0x40) {
            g++; /* this count was read; the stride past it is what fails */
            break;
        }
        pos += n * 0x40 + 8;
    }
    return g;
}

/* The header read and the table read arrive as separate requests, so the group
   count from the header is carried across to the read that needs it. synth.c
   loads one map at a time, so a single slot is enough -- and it is keyed by
   name so a mismatched pairing converts nothing. */
static char sfx_name[64];
static unsigned int sfx_groups;
static pc_u32 sfx_prefix[4];

/* Returns 1 when a schema claimed the read, 0 when the format is still
   unconverted. */
static int swap_contents(const char* name, unsigned int rel,
                         unsigned char* addr, unsigned int length)
{
    unsigned int i;

    if (name_ends_with(name, ".ssm")) {
        if (rel == 0 && length >= SFX_HEADER_BYTES) {
            swap_words(addr, SFX_HEADER_BYTES / 4);
            /* Word two is the group count. Words four to seven are the head
               of the group stream, which the loader copies out ahead of the
               table; the walk over that table has to start from them. */
            sfx_groups = ((const pc_u32*) addr)[2];
            for (i = 0; i < 4; i++) {
                sfx_prefix[i] = ((const pc_u32*) addr)[4 + i];
            }
            name_copy(sfx_name, name, sizeof sfx_name);
            return 1;
        }
        if (rel == SFX_HEADER_BYTES && sfx_groups != 0 &&
            name_eq(name, sfx_name)) {
            unsigned int walked =
                swap_group_counts(addr, length, sfx_groups, sfx_prefix);
#ifdef PC_DVD_TRACE
            pc_sys_log("dvd: groups ");
            log_uint(walked);
            pc_sys_log("/");
            log_uint(sfx_groups);
            pc_sys_log("\n");
#endif
            return 1;
        }
        /* The samples. ADPCM bytes -- nothing to swap. */
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

    /* The boot is still moving; anything the detector has counted was ordinary
       work between two reads, not a stall. */
    pc_watch_progress();

    if (disc_fd < 0) {
        if (callback != NULL) {
            pc_dvd_defer(callback, DVD_INTTYPE_DE); /* drive error: nothing to read from */
        }
        return 0;
    }

    got = pc_sys_pread(disc_fd, addr, length, offset);
    if (got != (long) length) {
        if (callback != NULL) {
            pc_dvd_defer(callback, DVD_INTTYPE_DE);
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
        pc_dvd_defer(callback, DVD_INTTYPE_TC);
    }
    return 1;
}

int DVDLowSeek(u32 offset, DVDLowCallback callback)
{
    (void) offset; /* positional reads make seeking meaningless */
    if (callback != NULL) {
        pc_dvd_defer(callback, DVD_INTTYPE_TC);
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
    pc_dvd_defer(callback, got == 0x20 ? DVD_INTTYPE_TC : DVD_INTTYPE_DE);
    return got == 0x20;
}

/* The drive is always present, spun up, and idle. */
int DVDLowWaitCoverClose(DVDLowCallback callback)
{
    if (callback != NULL) {
        pc_dvd_defer(callback, DVD_INTTYPE_CVR);
    }
    return 1;
}

u32 DVDLowGetCoverStatus(void) { return 2; /* closed */ }

int DVDLowStopMotor(DVDLowCallback callback)
{
    if (callback != NULL) {
        pc_dvd_defer(callback, DVD_INTTYPE_TC);
    }
    return 1;
}

int DVDLowRequestError(DVDLowCallback callback)
{
    if (callback != NULL) {
        pc_dvd_defer(callback, DVD_INTTYPE_TC);
    }
    return 1;
}

int DVDLowInquiry(DVDDriveInfo* info, DVDLowCallback callback)
{
    (void) info;
    if (callback != NULL) {
        pc_dvd_defer(callback, DVD_INTTYPE_TC);
    }
    return 1;
}

/* Streamed audio -- the disc's own ADPCM tracks, which Melee does not use. */
int DVDLowAudioStream(u32 subcmd, u32 length, u32 offset,
                      DVDLowCallback callback)
{
    (void) subcmd; (void) length; (void) offset;
    if (callback != NULL) {
        pc_dvd_defer(callback, DVD_INTTYPE_TC);
    }
    return 1;
}

int DVDLowRequestAudioStatus(u32 subcmd, DVDLowCallback callback)
{
    (void) subcmd;
    if (callback != NULL) {
        pc_dvd_defer(callback, DVD_INTTYPE_TC);
    }
    return 1;
}

int DVDLowAudioBufferConfig(int enable, u32 size, DVDLowCallback callback)
{
    (void) enable; (void) size;
    if (callback != NULL) {
        pc_dvd_defer(callback, DVD_INTTYPE_TC);
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
