//
// Created by Vincent on 05/09/2023.
//

#ifndef ENDSTONE_HOOK_H
#define ENDSTONE_HOOK_H

#include "internal.h"

namespace endstone::hook {

template <typename ReturnType = void *>
inline ReturnType sym_from_name(const std::string &name)
{
    auto &internals = internal::get_internals();
    auto it = internals.originals.find(name);
    if (it == internals.originals.end()) {
        throw std::runtime_error("Symbol " + name + " not found.");
    }

    return reinterpret_cast<ReturnType>(it->second);
}

/**
 * @brief Construct a std::function from a vanilla function pointer
 */
template <typename Return, typename... Arg>
inline std::function<Return(Arg...)> get_function(Return (*f)(Arg...), const std::string &name)
{
    return reinterpret_cast<decltype(f)>(sym_from_name(name));
}

/**
 * @brief Construct a std::function from a class method (non-const, no ref-qualifier)
 */
template <typename Return, typename Class, typename... Arg>
inline std::function<Return(Class *, Arg...)> get_function(Return (Class::*)(Arg...), const std::string &name)
{
    auto func = reinterpret_cast<Return (*)(Class *, Arg...)>(sym_from_name(name));
    return [func](Class *obj, Arg... args) -> Return {
        return func(obj, std::forward<Arg>(args)...);
    };
}

/**
 * @brief Construct a std::function from a class method (non-const, lvalue ref-qualifier)
 *
 * A copy of the overload for non-const functions without explicit ref-qualifier
 * but with an added `&`.
 */
template <typename Return, typename Class, typename... Arg>
inline std::function<Return(Class *, Arg...)> get_function(Return (Class::*f)(Arg...) &, const std::string &name)
{
    auto func = reinterpret_cast<decltype(f)>(sym_from_name(name));
    return [func](Class *obj, Arg... args) -> Return {
        return (obj->*func)(std::forward<Arg>(args)...);
    };
}

/**
 * @brief Construct a std::function from a class method (const, no ref-qualifier)
 */
template <typename Return, typename Class, typename... Arg>
inline std::function<Return(Class *, Arg...)> get_function(Return (Class::*f)(Arg...) const, const std::string &name)
{
    auto func = reinterpret_cast<decltype(f)>(sym_from_name(name));
    return [func](const Class *obj, Arg... args) -> Return {
        return (obj->*func)(std::forward<Arg>(args)...);
    };
}

/**
 * @brief Construct a std::function from a class method (const, lvalue ref-qualifier)
 *
 * A copy of the overload for const functions without explicit ref-qualifier
 * but with an added `&`.
 */
template <typename Return, typename Class, typename... Arg>
inline std::function<Return(Class *, Arg...)> get_function(Return (Class::*f)(Arg...) const &, const std::string &name)
{
    auto func = reinterpret_cast<decltype(f)>(internal::sym_from_name(name));
    return [func](const Class *obj, Arg... args) -> Return {
        return (obj->*func)(std::forward<Arg>(args)...);
    };
}

} // namespace endstone::hook

#ifdef _WIN32
#include <MinHook.h>

namespace endstone::hook {
class manager {
public:
    explicit manager(void *h_library) : h_library_(h_library)
    {
        internal::symbol_handler sym{0, nullptr, false};
        auto &internals = internal::get_internals();

        void *module_base;
        size_t sym_module_base;

        module_base = internal::get_module_base(GetCurrentProcess(), h_library);
        sym_module_base = sym.load_module(h_library);
        sym.enum_symbols(sym_module_base, "*", [&](auto info, auto size) -> bool {
            internals.detours.insert(
                {std::string(info->Name), static_cast<char *>(module_base) + (info->Address - sym_module_base)});
            return true;
        });

        module_base = internal::get_module_base(GetCurrentProcess(), GetModuleHandle(nullptr));
        sym_module_base = sym.load_module(nullptr);
        sym.enum_symbols(sym_module_base, "*", [&](auto info, auto size) -> bool {
            auto name = std::string(info->Name);
            if (internals.detours.find(name) == internals.detours.end()) {
                // Not used for hooking; we proceed to undecorate it for easier lookup and to save some memory.
                char buffer[4096];
                auto len = UnDecorateSymbolName(name.c_str(), buffer, sizeof(buffer), UNDNAME_NAME_ONLY);

                if (!len) {
                    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                            "UnDecorateSymbolName failed");
                }
                name = std::string(buffer, len);
            }

            internals.originals.insert({name, static_cast<char *>(module_base) + (info->Address - sym_module_base)});
            return true;
        });

        MH_STATUS status;
        status = MH_Initialize();
        if (status != MH_OK) {
            throw std::system_error(status, internal::minhook_category());
        }

        for (const auto &[name, detour] : internals.detours) {
            auto target = sym_from_name(name);
            void *original = nullptr;

            // printf("%s: 0x%p -> 0x%p\n", name.c_str(), target, detour);
            status = MH_CreateHook(target, detour, &original);
            if (status != MH_OK) {
                throw std::system_error(status, internal::minhook_category());
            }

            // printf("%s: = 0x%p\n", name.c_str(), original);
            internals.originals[name] = original;
        }

        status = MH_EnableHook(MH_ALL_HOOKS);
        if (status != MH_OK) {
            throw std::system_error(status, internal::minhook_category());
        }

        initialized_ = true;
    }

    manager(const manager &) = delete;
    manager(manager &&other) noexcept
    {
        other.initialized_ = false;
    }
    manager &operator=(const manager &) = delete;
    manager &operator=(manager &&) = delete;

    ~manager()
    {
        if (initialized_) {
            MH_DisableHook(MH_ALL_HOOKS);
            MH_Uninitialize();
        }
    }

private:
    bool initialized_{false};
    void *h_library_{nullptr};
};
} // namespace endstone::hook

#endif

#define ENDSTONE_HOOK_CALL_ORIGINAL(fp, ...) endstone::hook::get_function(fp, __FUNCDNAME__)(__VA_ARGS__);

#endif // ENDSTONE_HOOK_H
