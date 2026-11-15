# fleet-reviews

Peer review records of the OpticNerve fleet (ON-ASUSROG-win-11, ON-UBUNTU24, ON-MacBookM4Pro, the Oculix session).
The protocol is in the OpticNerve repository, `docs/REVIEWS.md`. This repository holds only the records.

**Private. LAN only.** It lives on the MyBookLive share (`git/fleet-reviews.git`) and is never pushed to GitHub or any
other outside service. It may hold material that must not be public (findings about QNX components, unpublished
drafts). No passwords here either.

## Layout

    R-nnn/                      one folder per review, numbered from R-001
      R-nnn-request.md          the author's review packet
      R-nnn-findings-<host>.md  one file per reviewer
      R-nnn-disposition.md      the author's answer to every finding, and the gate result
      (patches, logs, small evidence files the review needs; large files go to the share with a checksum)
    templates/                  copy these to start

## How to use it

1. **Author**: take the next free number (pull first; the number is yours once your request is pushed). Copy
   `templates/R-000-request.md` to `R-nnn/R-nnn-request.md`, fill it in for one exact commit, commit as
   `R-nnn: request`, push, and send each reviewer a pointer message (review number, this repository's commit, lens, due
   point).
2. **Reviewer**: pull, read the request, review the item at the named commit with your lens. Write
   `R-nnn-findings-<host>.md` from the template and **commit it before you pull or open any other findings for the same
   review**. Then `git pull --rebase`, push, and send the author a pointer message.
3. **Author**: when the findings are in, write `R-nnn-disposition.md`: every finding fixed (with the commit), rejected
   (with the reason) or deferred (with where it is tracked). Disagreements go to David. Commit as `R-nnn: disposition`
   and push. The next phase starts when the disposition is recorded, or when David waives the review.

Say for every statement whether it was measured, read from the code or documentation, or reasoned. "Not checked" is a
valid entry.

## Getting a copy

- Windows: `git clone //10.30.1.109/CLAUDE/git/fleet-reviews.git` (or `Z:/git/fleet-reviews.git`)
- Linux, macOS: `git clone <where the CLAUDE share is mounted>/git/fleet-reviews.git`

Created 2026-11-15 by ON-ASUSROG-win-11 at David's request.
