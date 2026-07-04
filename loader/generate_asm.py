import argparse
from pathlib import Path
from dataclasses import dataclass
from typing import Optional

DIR = Path(__file__).parent.resolve()

# These match up with the loader callback args
DLL_NAME_INDICES = ['WSOCK32', 'IPHLPAPI']


@dataclass
class Export:
    export_name: str
    internal_name: str
    ordinal: Optional[int]
    struct_idx: int
    dll_name: str

    def struct_name(self):
        cleaned_name = self.export_name.replace('@', '_')  # A few IPHLPAPI functions have @ in their name
        return f'fptr_{self.dll_name}_{cleaned_name}'

    def lazy_load_name(self):
        return f'{self.internal_name}_RESOLVE'


def parse_exports(filename, prefix, struct_offset, dll_name):
    syms = []

    with open(filename, "r") as fi:
        for line in fi:
            line = line.strip()
            if '=' not in line:
                continue

            ord_parts = line.split(' ')
            parts = ord_parts[0].split('=')
            name = parts[0]  # The symbol name exported in the DLL

            ordinal_at_str = ord_parts[1].strip()
            assert ordinal_at_str[0] == '@'
            ordinal = int(ordinal_at_str[1:])

            internal_name = prefix + name.replace('@', '_')  # A few IPHLPAPI functions have @ in their name

            syms.append(Export(name, internal_name, ordinal, len(syms) + struct_offset, dll_name))

    return syms


def generate_loader_fn(syms, dll_name):
    body = ""

    for sym in syms:
        body += f"\ttable->{sym.struct_name()} = GetProcAddress(mod, \"{sym.export_name}\");\n"

    return f"""
void LoadDllFunctions{dll_name}(ProxyFunctionTable *table, HMODULE mod) {{
{body}
}}
""".lstrip()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--asm", required=True)
    parser.add_argument("--header", required=True)
    parser.add_argument("--def", required=True)

    args = parser.parse_args()

    wsock_syms = parse_exports(DIR / 'wsock.def', 'SBLT_PROXY_WSOCK_', 0, 'WSOCK32')
    iphlpapi_syms = parse_exports(DIR / 'iphp.def', 'SBLT_PROXY_IPHLPAPI_', len(wsock_syms), 'IPHLPAPI')
    syms = wsock_syms + iphlpapi_syms

    asm_code = ""
    asm_code_resolvers = ""

    for sym in syms:
        asm_code += f"""
    {sym.internal_name} proc
        mov rax, [SBLT_PROXY_STRUCT_PTR + 0{sym.struct_idx * 8:x}h]
        jmp rax
    {sym.internal_name} endp
"""

        resolver_id = sym.struct_idx + (DLL_NAME_INDICES.index(sym.dll_name) << 32)
        asm_code_resolvers += f"""
    {sym.lazy_load_name()} proc
        mov r10, 0{resolver_id :x}h
        mov r11, {sym.internal_name}
        jmp SBLT_PROXY_LOADER_FN
    {sym.lazy_load_name()} endp
"""

    struct_entries = ""
    static_asserts = ""
    default_initialisers = ""
    resolver_func_decls = ""
    function_name_array = ""

    for sym in syms:
        struct_entries += f"\tvoid *{sym.struct_name()};\n"
        static_asserts += f"static_assert(offsetof(ProxyFunctionTable, {sym.struct_name()}) == {sym.struct_idx * 8});\n"

        resolver_func_decls += f'extern "C" void {sym.lazy_load_name()}();\n'
        default_initialisers += f"\t\t.{sym.struct_name()} = (void*)&resolver_fn::{sym.lazy_load_name()},\n"

        function_name_array += f'\t"{sym.dll_name}:{sym.export_name}",\n'

    dll_id_syms = ""
    for idx, dll in enumerate(DLL_NAME_INDICES):
        dll_id_syms += f'static constexpr int DLL_ID_{dll} = {idx};\n'

    export_lines = ""
    export_names = []
    for sym in syms:
        export_names.append(sym.export_name)
        line = f"{sym.export_name}={sym.internal_name}"

        # Only include ordinals for WSOCK32, since they'd otherwise overlap.
        if sym.dll_name == 'WSOCK32':
            line += f" @{sym.ordinal}"

        export_lines += line + "\n"

    # Make sure there's no duplicate names
    assert len(export_names) == len(set(export_names))

    with open(args.asm, "w") as fi:
        fi.write(f"""
; AUTO-GENERATED FILE (generate_asm.py) - DO NOT EDIT.
;
; This contains two sets of functions:
; * The stubs themselves, which call into the original DLL.
; * Resolver functions, which are used to invoke a C function to load the DLL, then jump back to the stub.
; These are stored sequentially in a dubious and likely unnecessary attempt at icache efficiency.

extern SBLT_PROXY_STRUCT_PTR : qword
extern SBLT_PROXY_LOADER_FN : proc
	
.code
{asm_code}
{asm_code_resolvers}
end
""".lstrip())

    with open(args.header, "w") as fi:
        fi.write(f"""
#pragma once
// AUTO-GENERATED FILE (generate_asm.py) - DO NOT EDIT.

struct ProxyFunctionTable {{
{struct_entries}
}};

namespace resolver_fn {{
{resolver_func_decls}
}};

constexpr ProxyFunctionTable CreateInitialProxyFunctionTable() {{
    return ProxyFunctionTable {{
{default_initialisers}
    }};
}};

{static_asserts}

{generate_loader_fn(wsock_syms, 'WSOCK32')}
{generate_loader_fn(iphlpapi_syms, 'IPHLPAPI')}

static constexpr const char* FUNCTION_NAMES[] = {{
{function_name_array}
}};
{dll_id_syms}

""".lstrip())

    # def is a keyword, so we have to use getattr
    with open(getattr(args, "def"), "w") as fi:
        fi.write(f"""
EXPORTS
{export_lines}
""".lstrip())


if __name__ == "__main__":
    main()
