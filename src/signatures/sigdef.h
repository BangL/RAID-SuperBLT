#pragma once

// Pointer type of a member function with the given signature, taking the this pointer as an additional first
// parameter. Spelled as a function type, so an empty parameter list needs no __VA_OPT__ (MSVC only has it behind
// /Zc:preprocessor).
template<typename Signature>
struct ThiscallPtr;
template<typename Retn, typename... Args>
struct ThiscallPtr<Retn(Args...)>
{
	using type = Retn(__thiscall *)(void*, Args...);
};

#ifdef SIG_INCLUDE_MAIN
#include "signatures.h"

#define CREATE_NORMAL_CALLABLE_SIGNATURE(name, retn, signature, mask, offset, ...) \
	typedef retn(*name ## ptr)(__VA_ARGS__); \
	name ## ptr name = NULL; \
	SignatureSearch name ## search(#name, &name, signature, mask, offset);

#define CREATE_CALLABLE_CLASS_SIGNATURE(name, retn, signature, mask, offset, ...) \
	typedef ThiscallPtr<retn(__VA_ARGS__)>::type name ## ptr; \
	name ## ptr name = NULL; \
	SignatureSearch name ## search(#name, &name, signature, mask, offset);

#else

// If we're not being included directly from InitiateState.cpp, only declare, not define, variables
#define CREATE_NORMAL_CALLABLE_SIGNATURE(name, retn, signature, mask, offset, ...) \
	typedef retn(*name ## ptr)(__VA_ARGS__); \
	extern name ## ptr name;

#define CREATE_CALLABLE_CLASS_SIGNATURE(name, retn, signature, mask, offset, ...) \
	typedef ThiscallPtr<retn(__VA_ARGS__)>::type name ## ptr; \
	extern name ## ptr name;

#endif

#if defined(SIG_INCLUDE_MAIN) || defined(SIG_INCLUDE_LJ_INTERNAL)
#define CREATE_LUAJIT_CALLABLE_SIGNATURE(name, retn, signature, mask, offset, ...) \
	CREATE_NORMAL_CALLABLE_SIGNATURE(name, retn, signature, mask, offset, __VA_ARGS__)
#else
#define CREATE_LUAJIT_CALLABLE_SIGNATURE(...)
#endif

extern "C"
{
	#include <lauxlib.h>
	#include <lua.h>
}

#include "signatures/sigdef_game.h"

// The four different try_open functions, each one with a different resolver to filter files
// Unfortunately, the language, english, and funcptr resolvers are identical bar for a different function call
// address, so we can't hook them normally. They have special handling by means of the FindAssetLoadSignatures
// function. Their addresses then get filled out here:
// (note: ifdef'd off so we don't have to include vector everywhere)
#ifdef INCLUDE_TRY_OPEN_FUNCTIONS
extern std::vector<void*> try_open_functions;
#endif
