# Version 1.0.2 validation

Production build: 484cab981229be2083f5f77beb14b84f220360fd.
Combined package SHA-256:
`22a69f3a6d7d04a908fedf4b2d1aab52ffee731de08870b0f55bfe39bf8b574c`.

All eight official platform targets compiled and passed architecture, exports,
actual Dusklight 2.0.3 symbol/import checks, and matching native metadata through
symgen. Native controller tests passed on Windows AMD64, both Linux targets,
and macOS Apple Silicon. The audio hash matches the previous release.

The exact combined bundle passed 637 observer checks in a copied Archive-08
story save with zero failures, seven native combat entries and seven exits,
one continuous shared stream, and base channels remaining audible. All 3,327
observed native Lament mixes were silent; replacement playback was prepared
at selection with its opening cursor at zero.

Six actual disable/restoration/enable/reload checks passed with zero failures.
During native restoration in combat, main Lament gain was 0 and native battle
gain was 1, as expected. The lifecycle driver now waits for real prepared SOS
playback and accepts the selected native battle sequence. A title-screen forcing
attempt and an older Lament-only restoration assertion were not accepted as
passing validation. These corrections change test drivers, not production code.

Original saves, configuration, installed mods and supplied GCI fingerprints were
unchanged. Other user mods and bundled mods were disabled in test profiles.
Validation drivers are separate artifacts and are not in the delivered mod.

Other platforms have compilation/static checks only, not device runtime tests.
Listening, full story traversal, actual death/reload and revival remain manual
acceptance checks. Full native loop repetitions were not rerun in 1.0.2; audio
and loop data are unchanged. Exact runtime evidence is included in the local
delivery's validation directory. No GitHub release or mod-page upload was made.
