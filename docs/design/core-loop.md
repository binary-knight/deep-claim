# Core loop: why the player plays

Status: direction approved by the owner on 2026-10-11 (see Decisions at the
end). It records the owner's story for the game (dictated after playtest
4), what that story settles and what it changes in the existing plan. The
first slice still gets its own spec, and its approval, before any build.

## Why this document exists

Playtest 4 (`docs/playtests/2026-10-11.md`) showed spec 008 turning a game
that could not be lost into one that could not be won, and the sweeps
showed that deepening the market only swings it back. The owner's verdict:
"there is no reason if I am profitable, that I can't just sit there for 200
years and collect money and then research everything and win the game.
There is no tension, no real reason to play or complexity." The Earth
strategy (`docs/design/earth-strategy.md`) put the goal last of seven
systems. This document puts it first.

## The premise (the owner's)

The world is dying, but there are still resources to mine: common ones,
rare ones, and beyond them "super metals", real or invented. The player is
an entrepreneur starting a mining company. Across the world, old mining
companies are retooling and new ones are appearing, and all of them are in
a race.

Humanity has a deadline. Either an artificial intelligence has taken over
and this is the last effort to survive it, or something is on its way to
Earth and there are a few hundred years to find an answer. The companies
race to build the capital and the specific materials for a space program:
light, medium and heavy launchers, then mining equipment beyond Earth, then
the research that offers a way out.

There is more than one way out, and which one the player reaches depends on
how they have played. Two the owner named: carry biological humanity to
another world, which needs deep-space research to find and probe one; or
understand consciousness well enough to digitise a mind and transmit it.
Others may exist, grounded in real physics and cosmology.

Running through it is a question the game should play with and not answer
for the player: what is human consciousness, what is an AI's, are they
separate, and does humanity treat AI as a subjugated species or as a
partner.

## What the premise settles

These were open in the Earth strategy and are answered by the story.

- **Why not wait.** The world has a deadline and other companies are
  racing for the same way out. Standing still loses.
- **What money is for.** Capital is a means. The program needs particular
  materials and manufactured components, so what a company can make matters
  as much as what it has in the bank.
- **Why ores differ.** Each has a use further up a chain and in the
  program, so which deposits a company holds is a strategic position, not
  only a price.
- **Where complexity lives.** In building and running production: mines,
  equipment, people, processing and manufacturing, placed where demand is.
  The owner's reference points are Satisfactory and Factorio. Value added
  rises with the length and difficulty of the chain.
- **A second outlet for output.** Ore and goods can be sold, or stockpiled
  and consumed by the company's own chain and program. The player is no
  longer only a seller into one market, which is the trap playtest 4 found.

## The loop, in layers

Each layer is introduced after the one before it is working for the
player, so the game stays easy to start (`DOMAIN.md`: easy to learn, hard
to master).

1. **Find and dig.** Survey, win land, build a mine, sell ore. This is the
   game as it stands today.
2. **Run the operation.** Mines have size, equipment and crews, cost more
   as they are worked out, and close (spec 009 is a first step).
3. **Make things.** Processing and manufacturing turn ore into goods worth
   more, in plants sited in regions with their own demand.
4. **Choose the outlet.** Sell for capital, supply your own plants, or
   stockpile for the program.
5. **Build the program.** Stages that need capital, research and delivered
   materials and components. Progress is visible, the player's and every
   rival's.
6. **Reach a way out.** Late research branches toward one of the endings.

Against all of it: the deadline, and rivals doing the same.

## What this changes in the existing plan

- **`DOMAIN.md`.** Its boundaries ("What Deep Claim Is Not") defer space
  and story design. The owner has now given the story. The build order can
  still be Earth first, but the document needs the owner's instruction to
  be amended; this draft does not touch it.
- **Earth strategy.** System 7 (a goal) moves to the front and becomes the
  program and the race. "Processing", listed under "later", becomes the
  centre of the mid-game. "Workforce", listed as "only if it creates
  choices", is now wanted. System 3 (regions) gains regional demand.
  Systems 1 and 2 (markets, mine economics) remain, as parts of the loop
  and no longer as the source of all tension.
- **Spec 009 (mine economics)**, approved and unbuilt, still fits layer 2.
  Its tuning assumed the market was the only outlet.
- **Market depth**, paused, becomes a tuning task inside whichever spec
  first adds a second outlet.
- **The interface** is tables and panels. A production layer needs a way
  to see and arrange a chain (see the open decisions).

## How big this is

This is a much larger game than the current plan described: several years
of work at the pace so far, not several specs. Two things follow.

- The next playable version should be a thin slice through every layer,
  not layer 2 finished. A slice a player can finish, and lose, proves the
  loop; then each layer deepens.
- Every system in the sim today survives into that slice. Nothing built is
  thrown away, but sweep gates stop being the test of whether it is a game.
  A playtest of the whole loop is.

## A proposed first slice

One game, short enough to finish in a sitting, with:

- the clock: a deadline in game years, shown from the first day;
- one rival racing for the same program, its progress shown;
- two or three ores as today, with spec 009's mine life;
- one processing step and one manufactured component, each a plant with
  capacity, inputs, running cost and a place;
- a program of three stages, each needing capital, a research project and
  delivered goods;
- one ending. Winning is finishing the program before the rival and the
  deadline; losing is the rival finishing first, the deadline passing, or
  bankruptcy.

Left out of the slice: crews and equipment as managed things, regional
demand, multiple endings, the AI relationship, more than one rival. Each is
a later layer on a loop already shown to work.

## Decisions

All by the owner, 2026-10-11.

- **Production is nodes and flows.** Plants with capacity and flows between
  them, managed from panels and a chain diagram, sited in regions. No
  machines or belts laid out on a floor.
- **The threat is ambiguous at first and revealed by play.** The game opens
  with a known deadline and competing theories about its cause.
- **A game plays the last decades.** The story's centuries are background;
  a game covers roughly the final 30 to 40 years, about three to four hours
  of play, with the deadline always on screen.
- **The race is for the lead role, not survival.** Humanity needs one
  program to succeed before the deadline. If a rival's succeeds first the
  world is saved on its terms: the player loses, or finishes as a lesser
  partner depending on how far along they were. If nobody finishes,
  everyone loses.
- **Materials are real, with one or two invented late**, where real physics
  runs out.
- **Crews and equipment are numbers per site, after the first slice.** A
  crew level and an equipment condition that the player funds; no named
  employees or individual machines.
- **The thin slice is the next spec.** It is the "proposed first slice"
  above.
- **Spec 009 (mine economics) is folded into the slice** and not built by
  itself. The market-depth spec is closed; market sizing is tuned inside
  the slice once a second outlet exists. Spec 008's small fixes from
  playtest 4 ride along.
- **`DOMAIN.md` is amended** with the premise and the changed boundaries.

Still open, for later documents: the endings in detail, the AI
relationship as a system, what the invented materials are, and how the
military and espionage layer fits the race.
