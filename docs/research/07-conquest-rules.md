# Conquest (gpm_cq): the rules, from the game's data

BF2 keeps this mode's logic **not in C++ but in Python**, shipped with the
game in plain text: `mods/bf2/python/game/gamemodes/gpm_cq.py` (621 lines)
and `game/scoringCommon.py`. The C++ side (`ServerGameLogic`) only
provides the mechanics — tickets, game state, events — while the script
decides who loses how much and when.

So no reverse engineering is needed here: the rules are open. Below are
notes on what the script does; the implementation in `src/server/` is
written from these notes.

Numbers for Dalian Plant (gpm_cq/16) from `GamePlayObjects.con`: `radius`
10..20, `areaValueTeam1/2` 35, `timeToGetControl` and `timeToLoseControl`
20..40, 250 tickets per team at the start (`GameLogicInit.con`),
`sv.ticketRatio 100`, `sv.spawnTime 15`, `sv.manDownTime 15`
(`Settings/ServerSettings.con`).

## The flag

Every point has a **flag position** — `Top`, `Middle`, `Bottom` — and a
raise counter that moves at `takeOverChangePerSecond`. The flag goes down
and only at the bottom can it change whose it is (`flag`), after which it
rises again.

The speed is recomputed on every entry into or exit from the radius, on
death, and on an owner change:

1. Count the living players of each team inside the radius. In a vehicle
   **only the first** passenger counts; someone who is "man down" does
   not.
2. `overweight = t1 - t2`, giving `attackingTeam` = 1, 2 or 0.
3. Nobody in the radius: a neutral point **slowly goes down** (`-0.5`),
   an owned one **slowly returns up** (`+0.5`), over `timeToLoseControl`.
4. People present: if the flag is already theirs (or it is at the bottom
   and the point is neutral) it rises by `|overweight|` over
   `timeToGetControl`; otherwise it first **descends** by `-|overweight|`
   over `timeToLoseControl`.
5. `takeOverChangePerSecond = attackOverWeight / timeToChangeControl`;
   rising at the top and descending at the bottom are zeroed.
6. `unableToChangeTeam` — the point is not touched at all;
   `onlyTakeableByTeam` admits only the named team.

When the counter reaches an end:

- it was owned and fell to the bottom → **neutralised** (the point becomes
  nobody's);
- it was neutral and rose → **captured** by the flag's team;
- a one-off ticket loss for the enemy: `enemyTicketLossWhenCaptured`.

## Tickets

- Start: `defaultTickets * ticketRatio / 100`.
- A player's death costs their team **one ticket**.
- The constant bleed depends on "area weight": the sum of the team's
  points' `areaValue`. For team T the bleed per second is
  `(defaultTicketLossPerMin(T) / 60) * (enemy_advantage / 100)`, and only
  if the enemy's area is ≥ 100 and the advantage is positive. Otherwise
  zero.
- If a team has **no points at all** and nobody alive is left, it bleeds
  at `defaultTicketLossAtEndPerMin` while the other team does not bleed at
  all.
- Warning thresholds (`setTicketLimit`): 0 (the end), 10, 10 % and 20 % of
  the start. Reaching zero (`limitId == -1`) ends the round: the other
  team wins.
- Time limit: whoever has more tickets wins.

## Score

`scoringCommon.py`: capture +2, neutralise +2, assist +1, defend +1.
Everyone inside the radius gets a point, but "first" is whoever entered
earlier (`enterCpAt`) — the full capture goes to them, the rest get
assists.
