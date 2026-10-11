# ADR-F-2026-10-11-mine-economics-003: A mine's whole-life projection is a pure sim function the AI calls

**Date:** 2026-10-11
**Status:** Accepted
**Context:** To choose a size or judge an enlargement the AI must compare a mine's revenue and running cost over its life, with output falling and cost rising. Until now the AI did its own arithmetic at one fixed rate with a hard-coded 1.3 margin, and spec 008's architecture review found it restating the sim's cost rule. Rule R-001 makes the sim the only authority on rules.
**Decision:** `sim::projectMine` takes content, the company's completed research, a size, tonnes per ore, a price per ore, a horizon and a step in days, and returns life in days, revenue and running cost. It is pure and reads no world state, so the AI can call it with survey estimates and its own valued prices. The step and the AI's margins are in `data/ai.json`; the 1.3 literal is removed. The tick never calls it.
**Consequences:** The AI cannot disagree with the rules about what a mine of a given size will cost, and a later interface hint can reuse the function. The sim carries a forecasting function outside the tick, which must stay cheap: at most horizon over step iterations per size per prospect. It is a forecast at a coarse step, so a test compares it with a mine actually run and bounds the difference.
