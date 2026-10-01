# 0.1.0 release notes — draft, unreleased

No tag, GitHub Release or package publication has been authorized or created.
See [RELEASE.md](RELEASE.md) for the declared scope and remaining work.

- Thin C++20 Fcitx5 frontend for full pinyin, double pinyin and Zhuyin using
  only the exported libpinyin C ABI. Dedicated ZRM double-pinyin composition
  coverage complements full-pinyin and Zhuyin tests.
- Client preedit, constrained candidate selection, Space/numeric selection,
  paging, editing and cancellation. Reset explicitly clears client preedit;
  page changes refresh the visible InputPanel.
- Prediction, Spell suggestions, punctuation delegation and conversion
  toggles; optional Cloud Pinyin and Lua candidate integrations. External
  suggestion rows commit without training the engine. Smart-quote delegation
  is stateless; live network Cloud behaviour is not an automated E2E claim.
- Real Fedora 44 KDE Plasma 6.7.5 Wayland acceptance on 2026-09-30 covered
  KWrite, Konsole Find and basic Chrome input. Verified intended addon and
  Rust engine committed `你好我爱中国` after a clean Plasma/Fcitx restart.
  Candidate differences followed identical persistent learned state in the
  pinned libpinyin reference; they required no ranking workaround.
- One unchanged addon binary passes the baseline suite under pinned
  libpinyin 2.11.92 and Rust oxpinyin with Tkrzw/checksum-verified model20;
  nine fresh-state parsing/candidate captures match. This is bounded
  evidence, not a full libpinyin/oxpinyin parity claim.
- Initial `zh_CN` and `zh_TW` translations (fcitx5 terminology) for config
  labels, status actions, addon comment and AppStream metadata; other
  locales fall back to English. The product name is not translated.
- A Fedora-native RPM spec (`packaging/fedora/`) builds cleanly in `mock` for
  Fedora 44. It is upstream reference material, not a Fedora submission. The Fedora-native RPM was
  also accepted on Fedora 44 KDE Wayland (KWrite, Konsole Find, basic Chrome)
  on 2026-10-04, using Fedora's libpinyin.
- Relocatable DESTDIR and `--prefix=` staging, CPack RPM/DEB/TXZ payloads,
  AppStream/config metadata, and native Fedora/Debian runtime dependencies
  validated in disposable environments. Model data remains a separately
  installed requirement at the libpinyin-compatible data location.

Declared desktop scope: Fedora 44 KDE Plasma Wayland. X11, GTK, GNOME,
Kubuntu/Debian desktops, other browsers, exhaustive input schemes/backends/
models, an automated desktop harness and Debian-policy packaging are
unvalidated. No general platform support or
package publication readiness is inferred from the tested scope.
