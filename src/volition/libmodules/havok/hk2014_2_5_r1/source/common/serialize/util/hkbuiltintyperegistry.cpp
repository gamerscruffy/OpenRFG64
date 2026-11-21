// Original: volition/libmodules/havok/hk2014_2_5_r1/source/common/serialize/util/hkbuiltintyperegistry.cpp
//
// Havok 2014.2.5 (stub_impl.md S4, serializer SD), from rfg.exe: the default builtin type registry, which fills the
// class name, type info and vtable registries from hkBuiltinTypeRegistry's lists, and the current version's static
// class name registry over the same class list.

#include "volition/libmodules/havok/hk2014_2_5_r1/source/common/serialize/util/hkbuiltintyperegistry.h"
#include "volition/libmodules/havok/hk2014_2_5_r1/source/common/base/reflection/hkclass.h"
#include "volition/libmodules/havok/hk2014_2_5_r1/source/common/base/reflection/hktypeinfo.h"
#include "volition/libmodules/havok/hk2014_2_5_r1/source/common/base/reflection/registry/hkdefaultclassnameregistry.h"
#include "volition/libmodules/havok/hk2014_2_5_r1/source/common/base/reflection/registry/hktypeinforegistry.h"
#include "volition/libmodules/havok/hk2014_2_5_r1/source/common/base/reflection/registry/hkvtableclassregistry.h"
#include "volition/libmodules/havok/hk2014_2_5_r1/source/common/serialize/util/hkstaticclassnameregistry.h"
#include <string>

template<> hkBuiltinTypeRegistry* hkSingleton<hkBuiltinTypeRegistry>::s_instance = nullptr; // 0x03A380D8

namespace hkHavokCurrentClasses
{
    // 0x03A380DC (:26): the current version's classes, merged into the default class name registry by init()
    hkStaticClassNameRegistry hkHavokDefaultClassRegistry(hkBuiltinTypeRegistry::StaticLinkedClasses, -1,
                                                          "hk_2014.2.5-r1");
}


#ifdef OPENRFG_X64
// ---- OpenRFG x64: the generated reflection data carries the 32-bit image's member offsets and object sizes. Here every
// class is laid out again for 64-bit (declaration order, natural alignment, an 8-byte vtable pointer for a root class
// that had one), before anything uses the class tables. A class that has a type info is checked against its sizeof.
#include "volition/libmodules/havok/hk2014_2_5_r1/source/common/base/reflection/hkclassmember.h"
#include "volition/libmodules/havok/hk2014_2_5_r1/source/common/base/reflection/hkinternalclassmember.h"
#include <unordered_map>
#include <string.h>
#include <stdio.h>
extern "C" __declspec(dllimport) int __stdcall VirtualProtect(void*, size_t, unsigned long, unsigned long*);
void openrfg_log(const char* text); // crash_early.cpp

static_assert(sizeof(hkClassMember) == sizeof(hkInternalClassMember), "hkClassMember size");

static std::unordered_map<const hkClass*, std::pair<int, int> > s_relayoutDone; // size, align
static std::unordered_map<const hkClass*, int> s_relayoutBusy;

static void relayoutWritable(const void* p, size_t n)
{
    unsigned long old;
    VirtualProtect(const_cast<void*>(p), n, 0x04 /* PAGE_READWRITE */, &old);
}

static int relayoutAlignUp(int v, int a)
{
    return a > 1 ? (v + a - 1) / a * a : v;
}

static void relayoutClass(const hkClass* c, int& outSize, int& outAlign);

// size and alignment of one member (-1 size: not laid out)
static void relayoutMember(const hkClassMember& m, int& sz, int& al)
{
    const hkClassMember::Type t = m.getType();
    int n = m.getCstyleArraySize();
    if (n == 0)
    {
        n = 1;
    }
    sz = -1;
    al = 1;
    if (t == hkClassMember::TYPE_STRUCT)
    {
        int csz = 0, cal = 1;
        if (m.getClass() != nullptr)
        {
            relayoutClass(m.getClass(), csz, cal);
        }
        sz = csz * n;
        al = cal;
    }
    else
    {
        const hkClassMember::Type bt = (t == hkClassMember::TYPE_ENUM || t == hkClassMember::TYPE_FLAGS) ? m.getSubType() : t;
        const hkClassMember::TypeProperties& pr = hkClassMember::getClassMemberTypeProperties(bt);
        if (pr.m_size > 0)
        {
            sz = pr.m_size * n;
            al = pr.m_align;
        }
    }
    if (t != hkClassMember::TYPE_POINTER && m.getFlags().anyIsSet(hkClassMember::ALIGN_8 | hkClassMember::ALIGN_16 | hkClassMember::ALIGN_32))
    {
        const int forced = m.getFlags().anyIsSet(hkClassMember::ALIGN_32) ? 32 : (m.getFlags().anyIsSet(hkClassMember::ALIGN_16) ? 16 : 8);
        if (al < forced)
        {
            al = forced;
        }
    }
}

static void relayoutClass(const hkClass* c, int& outSize, int& outAlign)
{
    std::unordered_map<const hkClass*, std::pair<int, int> >::iterator it = s_relayoutDone.find(c);
    if (it != s_relayoutDone.end())
    {
        outSize = it->second.first;
        outAlign = it->second.second;
        return;
    }
    if (s_relayoutBusy.find(c) != s_relayoutBusy.end())
    {
        outSize = c->m_objectSize;
        outAlign = 8;
        return;
    }
    s_relayoutBusy[c] = 1;
    int cur = 0;
    int align = 1;
    const hkClass* parent = c->getParent();
    if (parent != nullptr)
    {
        relayoutClass(parent, cur, align);
        // each secondary base (interface) the class declares brings its own 8-byte vtable pointer, placed after the primary base
        if (c->getNumDeclaredInterfaces() > 0)
        {
            cur = relayoutAlignUp(cur, 8) + 8 * c->getNumDeclaredInterfaces();
            if (align < 8)
            {
                align = 8;
            }
        }
    }
    const int n = c->getNumDeclaredMembers();
    const hkClassMember* members = n > 0 ? &c->getDeclaredMember(0) : nullptr;
    if (n > 0)
    {
        relayoutWritable(members, sizeof(hkClassMember) * n);
    }
    if (parent == nullptr)
    {
        int minOff = 0x7FFFFFFF;
        for (int i = 0; i < n; i++)
        {
            if (members[i].getOffset() < minOff)
            {
                minOff = members[i].getOffset();
            }
        }
        if ((n > 0 && minOff > 0 && minOff < 0x7FFFFFFF) || (n == 0 && c->m_objectSize == 4))
        {
            cur = 8; // the vtable pointer
            align = 8;
        }
        else if (n == 0)
        {
            cur = c->m_objectSize;
        }
    }
    for (int i = 0; i < n; i++)
    {
        int sz, al;
        relayoutMember(members[i], sz, al);
        if (sz < 0)
        {
            continue;
        }
        const int off = relayoutAlignUp(cur, al);
        const_cast<hkClassMember&>(members[i]).setOffset(off);
        cur = off + sz;
        if (al > align)
        {
            align = al;
        }
    }
    const int size = relayoutAlignUp(cur, align);
    relayoutWritable(c, sizeof(hkClass));
    const_cast<hkClass*>(c)->m_objectSize = size;
    s_relayoutBusy.erase(c);
    s_relayoutDone[c] = std::make_pair(size, align);
    outSize = size;
    outAlign = align;
}

static void relayoutAllClasses()
{
    static bool done = false;
    if (done)
    {
        return;
    }
    done = true;
    struct P { hkClassMember::Type t; short s; short a; };
    static const P props[] = {
        { hkClassMember::TYPE_POINTER, 8, 8 }, { hkClassMember::TYPE_FUNCTIONPOINTER, 8, 8 },
        { hkClassMember::TYPE_ARRAY, 16, 8 }, { hkClassMember::TYPE_SIMPLEARRAY, 16, 8 },
        { hkClassMember::TYPE_HOMOGENEOUSARRAY, 24, 8 }, { hkClassMember::TYPE_VARIANT, 16, 8 },
        { hkClassMember::TYPE_CSTRING, 8, 8 }, { hkClassMember::TYPE_ULONG, 8, 8 },
        { hkClassMember::TYPE_STRINGPTR, 8, 8 },
    };
    for (size_t i = 0; i < sizeof(props) / sizeof(props[0]); i++)
    {
        hkClassMember::TypeProperties& pr = const_cast<hkClassMember::TypeProperties&>(hkClassMember::getClassMemberTypeProperties(props[i].t));
        pr.m_size = props[i].s;
        pr.m_align = props[i].a;
    }
    std::unordered_map<std::string, int> realSize;
    for (int i = 0; hkBuiltinTypeRegistry::StaticLinkedTypeInfos[i] != nullptr; i++)
    {
        const hkTypeInfo* ti = hkBuiltinTypeRegistry::StaticLinkedTypeInfos[i];
        if (ti->getTypeName() != nullptr)
        {
            realSize[ti->getTypeName()] = (int)ti->getSize();
        }
    }
    int count = 0, mismatches = 0;
    for (int i = 0; hkBuiltinTypeRegistry::StaticLinkedClasses[i] != nullptr; i++)
    {
        const hkClass* c = hkBuiltinTypeRegistry::StaticLinkedClasses[i];
        int sz, al;
        relayoutClass(c, sz, al);
        count++;
        std::unordered_map<std::string, int>::iterator r = realSize.find(c->getName());
        if (r != realSize.end() && r->second != sz)
        {
            char buf[200];
            sprintf(buf, "havok relayout MISMATCH %s: computed %d, sizeof %d", c->getName(), sz, r->second);
            openrfg_log(buf);
            mismatches++;
        }
    }
    char buf[120];
    sprintf(buf, "havok relayout: %d classes, %d size mismatches", count, mismatches);
    openrfg_log(buf);
}
#endif // OPENRFG_X64

// 0x00F3C0C0 (:30)
void hkBuiltinTypeRegistry::addType(const hkTypeInfo* info, const hkClass* klass)
{
    hkDefaultClassNameRegistry::getInstance().registerClass(klass, klass->getName());
    hkTypeInfoRegistry::getInstance().registerTypeInfo(info);
    if (klass->getNumInterfaces() > 0)
    {
        hkVtableClassRegistry::getInstance().registerVtable(info->getVtable(), klass);
    }
}

// size 0x8
class hkDefaultBuiltinTypeRegistry : public hkBuiltinTypeRegistry
{
public:
    // 0x00F3C250 (:43)
    hkDefaultBuiltinTypeRegistry()
    {
        init();
    }

    // 0x00F3C180 (:49)
    void init()
    {
#ifdef OPENRFG_X64
        relayoutAllClasses();
#endif
        hkDefaultClassNameRegistry::getInstance().merge(hkHavokCurrentClasses::hkHavokDefaultClassRegistry);
        hkTypeInfoRegistry::getInstance().registerList(StaticLinkedTypeInfos);
        hkVtableClassRegistry::getInstance().registerList(StaticLinkedTypeInfos, StaticLinkedClasses);
    }

    // 0x00F3C570 (:56)
    hkTypeInfoRegistry* getTypeInfoRegistry() override
    {
        return &hkTypeInfoRegistry::getInstance();
    }

    // 0x00F3C580 (:61)
    hkClassNameRegistry* getClassNameRegistry() override
    {
        return &hkDefaultClassNameRegistry::getInstance();
    }

    // 0x00F3C590 (:66)
    hkVtableClassRegistry* getVtableClassRegistry() override
    {
        return &hkVtableClassRegistry::getInstance();
    }

    // 0x00F3D100 (:71): fresh registries, then filled again
    void reinitialize() override
    {
        hkDefaultClassNameRegistry::replaceInstance(new hkDefaultClassNameRegistry());
        hkTypeInfoRegistry::replaceInstance(new hkTypeInfoRegistry());
        hkVtableClassRegistry::replaceInstance(new hkVtableClassRegistry());
        init();
    }
    // the destructor (vtbl+0x00 0x00F3C290) is compiler-generated
};

// 0x00F3C120 (:81): the singleton's creation function; null until the three registries it fills exist
// (hkBaseSystem::initSingletons retries it after the others)
static hkReferencedObject* hkCreateBuiltInTypeRegistry()
{
    if (hkSingleton<hkVtableClassRegistry>::s_instance != nullptr &&
        hkSingleton<hkDefaultClassNameRegistry>::s_instance != nullptr &&
        hkSingleton<hkTypeInfoRegistry>::s_instance != nullptr)
    {
        return new hkDefaultBuiltinTypeRegistry();
    }
    return nullptr;
}

// 0x016FA178
static hkSingletonInitNode hkSingletonRegisterhkBuiltinTypeRegistry("hkBuiltinTypeRegistry",
    hkCreateBuiltInTypeRegistry, (void**)&hkSingleton<hkBuiltinTypeRegistry>::s_instance);
