#include "elf_format.h"

typedef struct {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version, entry, phoff, shoff, flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} Header;
typedef struct {
    uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align;
} ProgramHeader;
_Static_assert(sizeof(Header) == 52, "ELF32 header size");
_Static_assert(sizeof(ProgramHeader) == 32, "ELF32 program header size");

/* Copy bytes instead of dereferencing potentially unaligned file structures. */
static void copy(void *out, const void *in, unsigned int n)
{
    unsigned char *d = out;
    const unsigned char *s = in;
    while (n--) *d++ = *s++;
}
const char *elf_validate(const void *data, unsigned int size, ElfPlan *plan)
{
    Header h;
    if (!plan) return "missing load plan";
    plan->count = 0; plan->phdr = 0; plan->legacy = 0;
    if (!data || size < sizeof(h)) return "truncated ELF header";
    copy(&h, data, sizeof(h));
    if (h.ident[0] != 0x7f || h.ident[1] != 'E' || h.ident[2] != 'L' || h.ident[3] != 'F')
        return "not an ELF file";
    if (h.ident[4] != 1) return "ELF64 is unsupported; use an ELF32 i386 program";
    if (h.ident[5] != 1) return "big-endian ELF is unsupported";
    if (h.type == 3) return "PIE/shared ELF requires a dynamic loader; use -static -no-pie";
    if (h.type != 2 || h.machine != 3 || h.ident[6] != 1 || h.version != 1 ||
        h.ehsize != sizeof(h) || h.flags)
        return "requires static ELF32 i386 ET_EXEC";
    if (!h.phnum || h.phnum > ELF_MAX_SEGMENTS || h.phentsize != sizeof(ProgramHeader) ||
        h.phoff > size || h.phnum > (size - h.phoff) / sizeof(ProgramHeader))
        return "invalid program header table";
    if (h.ident[7] == 255 && h.ident[8] == BARNIX_APP_ABI) plan->legacy = 1;
    else if ((h.ident[7] != 0 && h.ident[7] != 3) || h.ident[8])
        return "unsupported ELF OS/ABI or ABI version";
    uint32_t lowest = 0xffffffffU;
    for (unsigned int i = 0; i < h.phnum; i++) {
        ProgramHeader p;
        copy(&p, (const unsigned char *)data + h.phoff + i * sizeof(p), sizeof(p));
        if (p.type == 1 && p.memsz && p.vaddr < lowest) lowest = p.vaddr;
    }
    plan->base = plan->legacy ? APP_LOAD_MIN : lowest & ~0x003fffffU;
    if (plan->base != APP_LOAD_MIN &&
        (plan->base < 0x02000000U || plan->base > 0xbfc00000U))
        return "unsupported ELF virtual address window";
    plan->stack_top = plan->base + (APP_STACK_TOP - APP_LOAD_MIN);
    plan->phoff = h.phoff; plan->phnum = h.phnum;
    uint32_t load_end = plan->stack_top - APP_STACK_SIZE;
    int executable_entry = 0;
    for (unsigned int i = 0; i < h.phnum; i++)
    {
        ProgramHeader p;
        copy(&p, (const unsigned char *)data + h.phoff + i * sizeof(p), sizeof(p));
        if (p.type == 2 || p.type == 3)
            return "ELF interpreter/dynamic linking is not implemented";
        if (p.type == 7) return "ELF TLS is not implemented";
        if (p.type != 1)
        {
            if (p.type != 0 && p.type != 4 && p.type != 6 &&
                p.type != 0x6474e551 && p.type != 0x6474e552 && p.type != 0x6474e553)
                return "unsupported program header";
            continue;
        }
        if (p.filesz > p.memsz || p.offset > size || p.filesz > size - p.offset)
            return "invalid segment file range";
        if (p.align > 1 && ((p.align & (p.align - 1)) ||
            ((p.vaddr & (p.align - 1)) != (p.offset & (p.align - 1)))))
            return "invalid segment alignment";
        if (!p.memsz) continue;
        if (p.vaddr < plan->base || p.vaddr >= load_end ||
            p.memsz > load_end - p.vaddr)
            return "segment outside application memory";
        for (unsigned int j = 0; j < plan->count; j++)
        {
            const ElfSegment *s = &plan->segments[j];
            if (p.vaddr < s->address + s->memory_size && s->address < p.vaddr + p.memsz)
                return "overlapping load segments";
        }
        unsigned int table_size = h.phnum * sizeof(ProgramHeader);
        if (h.phoff >= p.offset && h.phoff - p.offset <= p.filesz &&
            table_size <= p.filesz - (h.phoff - p.offset))
            plan->phdr = p.vaddr + h.phoff - p.offset;
        ElfSegment *s = &plan->segments[plan->count++];
        s->offset = p.offset; s->address = p.vaddr;
        s->file_size = p.filesz; s->memory_size = p.memsz;
        if ((p.flags & 1) && h.entry >= p.vaddr && h.entry - p.vaddr < p.filesz)
            executable_entry = 1;
    }
    if (!plan->count || !executable_entry) return "entry is not inside executable file data";
    plan->entry = h.entry;
    return 0;
}
