# Validation for 1.0.4

The first live-install crash is reproduced on 1.0.3 and fixed on 1.0.4.
The exact combined package tested has SHA-256:
`90cfbbf89b1006ad78454f4c00ee1d2d177aa638a0e321f62ea700034b1cb100`.

## Build and static checks

All eight targets passed architecture, lifecycle export, native metadata, and
Dusklight 2.0.3 compatibility checks (15 resolved native symbols and seven hooks).
The official symgen merge verified ABI/import/export declarations. Native fade,
reversal, and gain-guard controller tests passed on Windows AMD64, Linux x86_64,
Linux aarch64, and macOS arm64; remaining targets were compile checked.

Production source: `de997fdb520584f33e651cadad3032aa487cb3ce`.
Build: https://github.com/semillakan6/Dusklight-midna-lament-mod/actions/runs/42348150244
Later commits change test drivers, validation tools, and documentation only.

## Windows runtime checks

- Reproduced 1.0.3's allocation-failure abort using the browser's staged-install
  entry point after the native two-channel reader had initialized.
- Four successful first-install runs of the exact combined 1.0.4 package:
  23 install/buffer/playback checks, zero failures, and normal game exits.
- Verified live installation both before and during native Lament playback.
- Checked 32-byte alignment and the same published buffer address across
  disable, enable, and reload. The process allocation stays valid after unload.
- Six additional loader/music lifecycle checks passed, including stopping the
  replacement, restoring native selected music, enabling, and reloading.
- 581 section checks passed with two native combat entries and two exits.
  The same four-channel stream advanced continuously and retained matched gains.
- Cutscene handoff: prepared at music selection with cursor zero; 2,926 native
  mixes checked with zero audible original Lament mixes.
- All 24 real save/config/mod/supplied-GCI files retained their initial hashes.
  Graphics caches are outside this comparison. Tests used copied saves and
  explicit disposable profiles; other installed mods were disabled in the copies.

Optional validation drivers are separate packages, default OFF, and absent from
all eight shipping libraries. Test-only A/Start input advanced the copied-save
intro immediately before PADRead; virtual input ended at native Lament selection.
Audio observation did not override music selections or battle states.

## Limits

The install test uses the browser's real staging/install function with a local
package; it does not test the network download or submit to the mod website.
Other platforms' in-game behavior and subjective listening remain unverified.
This run did not exercise native main-volume recovery episodes; it is not a new
runtime recovery regression pass. The unchanged guard passed native unit tests.
Full story traversal, death/reload, revival, and long loop repetitions were not
repeated for this allocation-only fix. Earlier setup attempts that did not reach
Lament produced zero checks and are excluded from the passing counts.

The supplied audio and prepared AST are unchanged from 1.0.3. The AST SHA-256 is
`40475629963d17f40db783512d6dc92a0e77181b0d593c5cf14f79cf81d0adfb`.
Detailed evidence is in the delivery's `validation/` directory.
