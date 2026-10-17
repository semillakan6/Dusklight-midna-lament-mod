# Version 1.0.3 validation

Production source: 0970d44. Bundle SHA-256:
`c8f2f02347ab17c1df9185290faf441d4a8b65f0470cf76a06a4a8fcef4f407c`.

All eight platform builds, actual Dusklight 2.0.3 symbol/import checks, and native
metadata verification passed. Controller tests cover the cleared-battle-flags
regression, recovery continuity, genuine fade-outs, non-battle sub music and
reset/reload, alongside the existing 0.5/1.5-second fade and reversal tests.

The exact combined package passed 634 section checks with zero failures,
including 118 framework frames across two native recovery episodes. The native
main gain fell to 0.016667 while replacement gain stayed at its scene-adjusted
expected value of 1. Seven native combat entries and six exits retained one
continuous stream. All 2,680 observed native Lament mixes were silent, with the
replacement prepared at selection and cursor zero.

Six actual loader lifecycle checks passed with zero failures. Original saves,
configuration, installed mods and supplied GCI stayed unchanged. Validation
used copied saves and separate optional drivers; those drivers are not shipped.
The interactive test profile contains only the production mod and retains the
user's previous copied-save progress.

The audio and 0.5-second entry / 1.5-second exit fades are unchanged. Listening
acceptance is pending user testing. Other platforms have build/static checks,
not on-device runtime verification. Full story traversal and native loop
repetitions were not rerun for this targeted change. Local delivery includes
exact section, recovery and lifecycle evidence under validation/.
