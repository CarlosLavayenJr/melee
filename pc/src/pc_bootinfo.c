/* pc_bootinfo.c — the low-memory block the console's IPL leaves behind.
 *
 * On hardware the boot loader writes OSBootInfo to physical address 0 before
 * the game runs, and the SDK reads it from there: OSInit takes the arena
 * bounds from it, DVDInit takes the disc's file system table.
 *
 * Nothing wrote it here, so the mapped page is zeroed and DVDInit picks up a
 * null FSTLocation. It guards that, but every later lookup then walks a null
 * table -- DVDConvertPathToEntrynum faults on the first probe, which is
 * gmMain_8015FDA4 looking for "/develop.ini" to decide the debug level.
 *
 * A zero FST is not the honest shape of a disc-less boot; an *empty* one is.
 * This builds a file system containing nothing but its root directory, so
 * lookups run the real search and correctly find nothing. The retail path
 * follows, which is what a console with no development disc would do.
 *
 * When a port grows a real DVD layer over a disc image, this is where the
 * genuine FST gets published instead -- the fourteen compiled CARD sources and
 * all of dvdfs.c are already written against it.
 */
#include "pc_sys.h"

#define GC_RAM_CACHED 0x80000000UL

/* Mirrors OSBootInfo in dolphin/os.h and struct FSTEntry in dvd/dvdfs.c, which
   cannot be included here: this file runs before the SDK headers are usable in
   a freestanding link.
   Widths are explicit rather than `unsigned long`, which is four bytes in the
   GameCube's ABI and eight on an LP64 host -- these describe a fixed memory
   layout the SDK reads back, so they cannot follow the host's word size. */
typedef unsigned int pc_u32;

typedef struct {
    unsigned char diskID[0x20];
    pc_u32 magic;
    pc_u32 version;
    pc_u32 memorySize;
    pc_u32 consoleType;
    void* arenaLo;
    void* arenaHi;
    void* FSTLocation;
    pc_u32 FSTMaxLength;
} pc_boot_info;

typedef struct {
    pc_u32 isDirAndStringOff;
    pc_u32 parentOrPosition;
    pc_u32 nextEntryOrLength;
} pc_fst_entry;

/* Somewhere in low memory above OSBootInfo and below the arena. */
#define PC_FST_ADDR (GC_RAM_CACHED + 0x00010000UL)

void pc_bootinfo_init(void)
{
    pc_boot_info* info = (pc_boot_info*) GC_RAM_CACHED;
    pc_fst_entry* fst = (pc_fst_entry*) PC_FST_ADDR;

    /* One entry: the root directory, marked as a directory by the high byte of
       isDirAndStringOff, ending immediately after itself. dvdfs.c reads
       MaxEntryNum from the root's nextEntryOrLength, so 1 means "root and
       nothing else" and the string table that follows is empty. */
    fst[0].isDirAndStringOff = 0x01000000u;
    fst[0].parentOrPosition = 0;
    fst[0].nextEntryOrLength = 1;

    info->magic = 0x0D15EA5Eu; /* what the IPL writes; the SDK checks it */
    info->version = 1;
    info->memorySize = 24u << 20;
    info->consoleType = 1; /* retail */
    info->FSTLocation = fst;
    info->FSTMaxLength = sizeof(pc_fst_entry);

    /* arenaLo/arenaHi stay zero: OSInit reads zero as "use the linker's
       bounds", and pc_os.c sets the real ones. */
}
