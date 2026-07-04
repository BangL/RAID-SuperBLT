# DLL Loader system

This is the code responsible for getting SuperBLT into the PAYDAY 2 address space.

The basic idea is that SBLT's DLL has the same name as a DLL that PAYDAY 2 links to - either WSOCK32.dll
or IPHLPAPI.dll - and due to the order in which Windows searches for DLLs, SBLT will be loaded instead
of the Microsoft networking DLLs of the same name.

At this point, the programme will start calling the networking functions that the original DLL exports,
and SuperBLT has to implement those. It does this by loading the original DLL using LoadLibrary and 'proxying'
the function calls along.

Taking the function SetIpTTL for example:

* `SetIpTTL` - this is the name of the symbol exported by both the SuperBLT and original DLL
* `SBLT_PROXY_IPHLPAPI_SetIpTTL` - this is the symbol name inside the SBLT DLL that (using a DEF file) `SetIpTTL` points
  to. This function is auto-generated.
* `SBLT_PROXY_STRUCT_PTR.fptr_IPHLPAPI_SetIpTTL` - this is a function pointer that points to the original `SetIpTTL`
  function. This struct is part of an auto-generated header.
* `SBLT_PROXY_IPHLPAPI_SetIpTTL_RESOLVE` - this is a stub function for use during startup (see below). This is also
  auto-generated.

`SBLT_PROXY_IPHLPAPI_SetIpTTL` is a very simple function, which loads the function pointer variable
from the `SBLT_PROXY_STRUCT_PTR` struct to RAX and jumps to it.

When the DLL is first loaded, this struct points to the `SBLT_PROXY_IPHLPAPI_SetIpTTL_RESOLVE` function, which sets a
couple of volatile registers to uniquely identify the function being called and then calls `SBLT_PROXY_LOADER_FN`.
This is done because the original DLL isn't loaded, and MSDN says you're not allowed to use LoadLibrary inside
DllMain. While we've done that in older versions of SBLT and it's been fine, it's not very nice.

`SBLT_PROXY_LOADER_FN` saves all the volatile registers and calls the C function `SBLT_PROXY_LOADER_FN_CXX`, which
will load the DLL and assign all the fields of SBLT_PROXY_STRUCT_PTR. It then returns through the `_RESOLVE` function,
which recovers the stored registers and calls `SBLT_PROXY_IPHLPAPI_SetIpTTL` a second time. This time the struct
points to the original DLL, and the original function is invoked.

Every subsequent time a function is called, `SBLT_PROXY_IPHLPAPI_SetIpTTL` jumps directly to the original function.

## Why WSOCK32 and IPHLPAPI?

Long, long ago, the original BLT only overrode IPHLPAPI.dll. At some point a Windows 10 update broke this for
some people, and the hook would never be loaded for whatever reason.

I switched SBLT over to WSOCK32 which seems to be fine, however I've heard that some people still need the original
IPHLPAPI version since WSOCK32 doesn't work on their machines. How widespread is this? Who knows.

If there comes some major issue with supporting the IPHLPAPI build, I'd be inclined to remove it completely.

The major disadvantage of supporting these two DLLs was that there were always two builds of SuperBLT, and the
update system had to be tested twice, two sets of DLLs had to be uploaded, etc. Here I'm exporting all the symbols
from both DLLs so you should be able to use the same file and just rename it.

The one catch is that the export ordinals for WSOCK32 and IPHLPAPI overlap, so if you're using the DLL as an IPHLPAPI
replacement and something imports that DLL using it's ordinal numbers (as opposed to symbol names) then you'll get a
crash.
