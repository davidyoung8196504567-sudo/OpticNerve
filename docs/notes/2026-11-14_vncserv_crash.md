# `vncserv` crash on the Pi, 2026-11-14

During the Windows → Pi input run (OCULIX_VNC_MATRIX.md, "Input, Windows host → the Pi"), QNX's VNC server
`vncserv` on the Raspberry Pi 5 (QNX 8.0 quick-start image 20260902) crashed once, at about 23:37 UTC, and wrote a
core dump. It did not happen again in the rest of the run.

- The client, OculiX, sent only complete, well-formed messages; the crash is in the server.
- The crash was analysed with the SDP's debugger. It is not listed in the QNX SDP 8.0 Known Issues document
  (edition 2026-11-12).
- The evidence (the core dump, the binaries, the debugger output, the system log and the full analysis) is kept
  privately and is not published here. Defects in QNX are reported to QNX privately, not in this public repository.

## Reported to QNX

| Date | Channel | Reference |
|---|---|---|
| to be filled in | to be filled in | to be filled in |

The reference lets QNX support and the QNX development team find the report. It is listed here so the judges can see
that the defect was reported.
