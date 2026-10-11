# ADR-F-2026-10-11-first-slice-race-for-the-program-002: Stage research is ordinary research with a program prerequisite

**Date:** 2026-10-11
**Status:** Accepted
**Context:** Each program stage needs a research project (spec BR-004). It could be a new mechanism or part of the existing tree, which already has an order, a queue entry, a screen, notices and AI handling, and allows one project at a time per company.
**Decision:** The three stage projects are entries in `data/techs.json`. A tech gains one optional field, the program stage that must be complete before it can start. `data/program.json` names each stage's project by id. Starting one raises a public notice. No new order is added for research.
**Consequences:** Program research competes with mine research for the company's one research slot, which is a real decision for the player and the AI. The Research screen needs only to show the new prerequisite. The loader must check that each stage names a project that exists and that the projects' stage prerequisites agree with the program's order.
