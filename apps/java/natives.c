/* Native method binding by class, name and descriptor. */
#include "jvm.h"

extern const NativeEntry natives_lwjgl_own[], natives_gl[];
static const NativeEntry *const tables[] = {natives_core, natives_io, natives_lwjgl_own, natives_gl};

Native native_lookup(Method *m)
{
    for (unsigned i = 0; i < sizeof(tables) / sizeof(tables[0]); i++)
        for (const NativeEntry *e = tables[i]; e->cls; e++)
            if (!strcmp(e->name, m->name) && !strcmp(e->desc, m->desc) && !strcmp(e->cls, m->owner->name)) return e->fn;
    return 0;
}

void natives_init(void) {}
