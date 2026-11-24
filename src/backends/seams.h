// OpenRFG (VOM, new code): seams, where a backend attaches to one of the game's functions without patching its code.
//
// A seam is a line at the top of the function. While a backend has a handler attached, the call goes to the handler,
// with the function's arguments (a method's object first). The handler may call the original, which runs the
// function's body. With nothing attached the function runs as it always did.
//
// A seam is known by the function's name in OpenRFG.pdb (and, for an overload, its parameters as rfgphys's names table
// writes them). rfgphys attaches its hooks to seams through its hooks::Binding (backends/physx/physx_backend.cpp), where
// the plugins attach the same handlers as MinHook detours; a hook whose function has no seam stays a detour.
//
//     OPENRFG_SEAM(Seam_explosion_create, &explosion_create, "explosion_create"); // namespace scope, before the body
//     void explosion_create(const explosion_info* info, object* source, ...)
//     {
//         OPENRFG_SEAM_ENTER(Seam_explosion_create, info, source, ...); // the function's arguments, in order
//         ...
//
// A method's seam passes `this` first: OPENRFG_SEAM_ENTER(Seam_world_destream_zone, this, wz). A virtual method needs
// its body called without the virtual call: OPENRFG_SEAM_VIRTUAL, with a function that calls it qualified.
#pragma once

#include <intrin.h>

#pragma intrinsic(_ReturnAddress)

struct openrfg_seam
{
    const char* name;         // the function's name in OpenRFG.pdb
    const char* signature;    // its parameters as rfgphys's names table writes them, for an overload; null otherwise
    void* original;           // runs the function's body: what a handler calls on
    void* volatile handler;   // the handler while it is on (openrfg_seams_enable)
    void* attached;           // the handler attached, on at the next openrfg_seams_enable
    openrfg_seam* next;       // the registry
};

void openrfg_seam_register(openrfg_seam* seam);
// The seam of a function by its name and, when the seam has one, its parameters; null when the game has none.
openrfg_seam* openrfg_seam_find(const char* name, const char* signature);
// Attaches a handler: `original` receives what the handler calls on (the function's body, or the handler attached
// before it: two backends on one function chain like two detours).
void openrfg_seam_attach(openrfg_seam* seam, void* handler, void** original);
// Turns the attached handlers on.
void openrfg_seams_enable();

// The thread's next seam lets the call through to the body (set by an original right before it calls the function).
extern thread_local bool openrfg_seam_skip;
// The game's call site of the call a seam passed to its handler (rfgphys's hooks::caller).
extern thread_local const void* openrfg_seam_caller;

struct openrfg_seam_caller_scope
{
    const void* saved;
    explicit openrfg_seam_caller_scope(const void* caller) : saved(openrfg_seam_caller) { openrfg_seam_caller = caller; }
    ~openrfg_seam_caller_scope() { openrfg_seam_caller = saved; }
};

// A seam whose handler has the type Fn (a method's: the object first) and whose original is `body`.
template <typename Fn>
struct openrfg_seam_t;

template <typename R, typename... A>
struct openrfg_seam_t<R (*)(A...)> : openrfg_seam
{
    openrfg_seam_t(const char* function, const char* params, R (*body)(A...)) : openrfg_seam{function, params, (void*)body, nullptr, nullptr, nullptr}
    {
        openrfg_seam_register(this);
    }

    // True when the call goes to the handler.
    bool enter() const
    {
        if (openrfg_seam_skip)
        {
            openrfg_seam_skip = false;
            return false;
        }
        return handler != nullptr;
    }

    R call(const void* caller, A... a) const
    {
        openrfg_seam_caller_scope scope(caller);
        return ((R(*)(A...))handler)(a...);
    }
};

// The original of a free function and of a non-virtual method: the function itself, its seam skipped once.
template <auto F, typename T = decltype(F)>
struct openrfg_seam_original;

template <auto F, typename R, typename... A>
struct openrfg_seam_original<F, R (*)(A...)>
{
    using handler = R (*)(A...);
    static R body(A... a)
    {
        openrfg_seam_skip = true;
        return F(a...);
    }
};

template <auto F, typename C, typename R, typename... A>
struct openrfg_seam_original<F, R (C::*)(A...)>
{
    using handler = R (*)(C*, A...);
    static R body(C* self, A... a)
    {
        openrfg_seam_skip = true;
        return (self->*F)(a...);
    }
};

template <auto F, typename C, typename R, typename... A>
struct openrfg_seam_original<F, R (C::*)(A...) const>
{
    using handler = R (*)(const C*, A...);
    static R body(const C* self, A... a)
    {
        openrfg_seam_skip = true;
        return (self->*F)(a...);
    }
};

// `function` is a pointer to the function or the (non-virtual) method; `pdb_name` its name in OpenRFG.pdb; an overload
// adds its parameters as rfgphys's names table writes them (OPENRFG_SEAM_OVERLOAD).
#define OPENRFG_SEAM(seam, function, pdb_name) \
    static openrfg_seam_t<openrfg_seam_original<function>::handler> seam(pdb_name, nullptr, &openrfg_seam_original<function>::body)
#define OPENRFG_SEAM_OVERLOAD(seam, function, pdb_name, params) \
    static openrfg_seam_t<openrfg_seam_original<function>::handler> seam(pdb_name, params, &openrfg_seam_original<function>::body)
// A virtual method: `handler_type` takes the object first; `body` sets openrfg_seam_skip and calls the method qualified.
#define OPENRFG_SEAM_VIRTUAL(seam, handler_type, pdb_name, body) static openrfg_seam_t<handler_type> seam(pdb_name, nullptr, body)

// The first line of the function: the call goes to the handler while one is on.
#define OPENRFG_SEAM_ENTER(seam, ...) \
    if (seam.enter()) \
    return seam.call(_ReturnAddress(), __VA_ARGS__)
// A function without parameters.
#define OPENRFG_SEAM_ENTER0(seam) \
    if (seam.enter()) \
    return seam.call(_ReturnAddress())
