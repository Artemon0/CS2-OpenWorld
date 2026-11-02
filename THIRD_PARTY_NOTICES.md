# Third-party notices

Original CS2 Open World contributions are subject to the proprietary notice in
the repository root, not an open-source license. That notice cannot relicense
third-party material or establish that a commercial CS2 modification is
permitted. This inventory covers the components identified in the current
build configuration; it is not a complete legal or supply-chain audit.

The proprietary notice is intended to reserve rights in the rights holder's
original expression. It does not claim ownership of ideas, methods, protocols,
or independently created implementations. A prior repository revision used
MIT text; replacing it does not retroactively revoke rights already granted
for copies distributed under that license.

## Commercial distribution warning

Valve's official [Source Engine mod distribution guidance](https://partner.steamgames.com/doc/sdk/uploading/distributing_source_engine)
states that mods are expected by default to be non-commercial, defines paid
access and donations as commercial, and says that Valve very rarely permits
mods to be sold. A paid CS2 server plugin may fall within restrictions that
the project's own license cannot override. Obtain written confirmation from
Valve (and qualified legal advice) before advertising, selling, or otherwise
commercially distributing this plugin. Do not treat this repository or its
license files as that permission.

## Components used by the plugin

- **Metamod:Source** — zlib/libpng license. The upstream notice is in
  `metamod-source/LICENSE.txt`; a copy is shipped as
  `licenses/Metamod-zlib.txt`. The plugin uses its SDK interfaces and KHook
  service; the plugin package does not include Metamod itself.
- **KHook** — zlib license, as stated in
  `metamod-source/third_party/khook/include/khook.hpp`.
- **tinyformat** — Boost Software License 1.0, stated in
  `src/vendor/tinyformat.h`; a copy is shipped as
  `licenses/Boost-Software-License-1.0.txt`.
- **Protocol Buffers 3.21.8** — BSD 3-Clause license, from
  `hl2sdk-cs2/thirdparty/protobuf-3.21.8/LICENSE`; a copy is shipped as
  `licenses/Protocol-Buffers-BSD-3-Clause.txt`. The generated protocol code is
  built into the plugin.
- **PostgreSQL libpq** — loaded from the server installation at runtime and
  not bundled by this project. PostgreSQL uses its permissive PostgreSQL
  License; follow the license for the installed client library distribution.

## Vendored but not currently built

- **picosha2** — MIT License, stated in `src/vendor/picosha2.h`.
- **miniz** — its source contains permissive license notices and an Unlicense
  public-domain dedication. Preserve the notices in `src/vendor/miniz.c` and
  `src/vendor/miniz.h` if that code is used or redistributed.

## SDK and reference material

- **HL2SDK for CS2** — an external Git submodule used to build against the
  game's interfaces. It is not relicensed by this project. Review the
  applicable upstream and Valve terms for the SDK and for binaries built with
  it; this project does not package the SDK checkout.
- **SwiftParticleMenuDemo** — the `temp_swift_menu` checkout is reference
  material only and is not included in the server package. Its upstream MIT
  license is retained in that checkout. Any future redistribution of its
  contents must retain the upstream copyright and license notice.

The package staging step includes this file alongside the project's notice.
It also stages the license texts listed above. When adding a dependency,
review its license before adoption and update this inventory and the packaged
notices. Have counsel review the repository and distribution history before
relying on exclusivity.
