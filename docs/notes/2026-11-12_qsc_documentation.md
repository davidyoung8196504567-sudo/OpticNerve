# QNX Software Center 2.0.4: what the documentation says (2026-11-12)

Web research on how the QNX Software Center (QSC) is documented to work, compared with what we saw today. The pages
were read through a summarising tool, so check the exact wording at the links before quoting it. Most QSC doc pages
are dated February 4, 2025.

## Documentation

- User's guide: https://www.qnx.com/developers/docs/qsc/com.qnx.doc.qsc.nav/topic/bookset.html
- Command-line reference: https://qnx.com/developers/docs/qsc/com.qnx.doc.qsc.user_guide/topic/commandline_qsc.html
- QSC 2.0.4 release notes and known issues (May 29, 2025): https://www.qnx.com/developers/articles/rel_7160_5.html
- Export and import patch sets: https://qnx.com/developers/docs/qsc/com.qnx.doc.qsc.user_guide/topic/export_import_patch_sets.html

Terms, as documented:

- **Baseline:** "a package that serves as the basis of an installation". An installation is a baseline plus added
  packages or updates.
- **Patch set:** "a package that acts like a recipe for installing packages". It lists packages and exact versions
  and carries no files.
- **Status:** "identifies the maturity of the software". Stable and experimental are described; experimental
  packages need the installation property "Install experimental packages". "Validated" is not described.
- **Verify Installation:** detects and repairs missing, unexpected or modified files. On the CLI,
  `-verifyInstallation` exits with code 4 on failure.

## Our observations against the documentation

| Observation | Documentation |
|---|---|
| Base package says 8.0.0, packages carry GA8.0.x tags, separate `patchset80x` packages exist | Partly explained. The SDP 8.0.4 release notes say to update with "the provided patch set" or install 8.0.4 as a new baseline, and to search QSC for "GA8.0.4". So a named 8.0.x level is defined by its patch set; "newest of everything" is not a named level (inference). `com.qnx.marketingVersion` and qconfig staying at 8.0.0 are not documented. |
| CLI `-verifyInstallation` reports 562 terminfo "unexpected file" errors on Windows; the GUI reports clean | Not documented, not in the 2.0.4 known issues, no forum report found. A candidate bug report. |
| The CLI does not use the GUI's login | Consistent. CLI options that contact myQNX need `-myqnx.user` and `-myqnx.password`. The docs warn the password is logged in clear text and recommend an `@options_file`. |
| The SDP folder vanished while QSC still listed it | No explanation found. Documented ways QSC deletes an installation: `-cleanInstall`, and `-forceOverwrite` on a deploy path. Neither was run in our logs. |
| Export and import of patch sets | Documented as the way to standardise a team: export a text patch set, then on the other machine `-mirror` and `-importAndInstall`. The other machine needs its own entitlement and server access. No end-to-end offline CLI procedure was found. |

Known issues in 2.0.4 that affect us:

- **J2993519:** importing a patch set that names a missing or unentitled package shows no error in the GUI and
  creates an empty project. Workaround: use the CLI.
- **J2812561:** turning off "Install experimental packages" after installing one blocks install, uninstall and
  repair.
- **J2644173:** adding an installation while another is in progress can make it fail.

## Licence

The QNX Everywhere page says the licence is for "individual non-commercial users"
(https://qnx.software/en/developers/get-started/qnx-everywhere). The licence text itself was not read, so whether
copying or deploying an installation to a second machine is allowed is not verified. Licensing questions go to
licensing@qnx.com.

## Where to report problems

- No public bug tracker for QSC was found.
- The support portal is for registered support-plan customers
  (https://qnx.software/en/developers/support-and-community/support).
- Community channels named on the QNX Everywhere page: Discord (https://discord.gg/Jj4EkkrFTT), Reddit
  (https://www.reddit.com/r/QNX/), GitLab (https://gitlab.com/qnx), GitHub (https://github.com/qnx-ports, for the
  open-source ports, not QSC). Account and licence problems: myqnx@blackberry.com.
- Whether QNX staff answer on Discord or Reddit was not observed directly.

## Qualification

No public statement was found that QSC is safety-certified or tool-qualified, and none that it is not. QNX's
certification statements are about QNX OS for Safety and the qualified toolchains.
