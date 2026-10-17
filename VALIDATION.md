# Validation

The v1.0.1 Windows implementation previously passed isolated section and loader
lifecycle checks. Version 1.0.2 requires new checks after the portability changes.
The eight-platform CI workflow gates distribution on compilation, native fade
tests where executable, actual release symbols, library architecture, exports,
metadata agreement, ZIP integrity, and the unchanged shared AST hash.

Compilation/static checks do not establish in-game behavior on untested devices.
Windows runtime evidence will be delivered separately after testing the final
combined artifact in a disposable profile. Listening and full story traversal on
other operating systems remain manual acceptance checks.
