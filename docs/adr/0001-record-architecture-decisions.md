# 0001. Record architecture decisions

- Status: Accepted
- Date: 2026-11-23

## Context

SkyRelay is a long-lived project with a handful of choices that are expensive
to reverse: the implementation language, the DDS vendor, the shape of the data
contract, the dependency strategy. The reasoning behind them is easy to lose.
A commit message records what changed, not which alternatives I rejected or
why. The system design document captures the overall picture, but it is a
snapshot and does not track decisions as they are revisited.

## Decision

I will record each significant architectural decision as a short Architecture
Decision Record in `docs/adr/`, using the format described by Michael Nygard:
Status, Context, Decision, Consequences. Files are numbered sequentially and
named `NNNN-short-title.md`; `0000-template.md` is the starting point.

An accepted ADR is not edited to change its meaning. If a decision is
reversed, I add a new ADR and mark the old one as superseded.

## Consequences

- The rationale for hard-to-reverse choices stays next to the code and is
  reviewable in the same pull request as the change that depends on it.
- Contributors can see which questions are settled and which are open.
- There is a small writing cost per decision. I accept it and keep the records
  short; trivial choices do not need an ADR.
