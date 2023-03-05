# SuperBLT DLL Changelog

This lists the changes between different versions of the SuperBLT DLL,
the changes for the basemod are listed in their own changelog.
Contributors other than maintainers are listed in parenthesis after specific changes.

## Version 3.3.6

- Added a VR check for Wren
- Updated Wren to the latest version (test1)
- Added min and max audio distance for XAudio sources
- Fixed crash with VR version of the game

## Version 3.3.5

This is a hotfix for a missing function signature

- Updated OpenSSL, ZLib and OGG decoder (test1)
- Implemented SystemFS:delete_file (Der Muggemann)
- Disabled HRTF to improve audio quality (test1)

## Version 3.3.4

- Improved Linux specific code (Azer0s, whitequark)
- Improved XML parsing
- Updated Lua function signature (Cpone)

## Version 3.3.3

- Fixed AssetDB Idstring parsing
- Improved Linux specific code (CraftedCart)
- Updated to a less strict, custom version of mxml
- Reduced amount of http request logging

## Version 3.3.2

- Fixed crashes on computers with all.blb or any other files with names shorter than eight letters in the assets directory
- Fixed a compile issue on Linux (roberChen)

## Version 3.3.1 and below

These versions do not have nicely formatted changelogs, but you can see their Git commit history in GitLab if required.
