---
name: input-handling
description: How to handle player input cleanly — map raw input to named actions, read it in one place, and keep gameplay code decoupled from the keyboard.
---
# Handling input cleanly

Input is where a surprising number of game bugs live, because the beginner approach
— checking `if key_pressed("W")` scattered through gameplay code — couples
everything to the keyboard and breaks the moment you want a gamepad, remappable
keys, or the same action from two sources.

**The clean shape: raw input → named action → systems react.**

1. **Map raw input to named ACTIONS, in one place.** Not `if W pressed` in the
   player code, but an input layer that says "the *jump* action is active." The
   player asks "is jump active?" and never knows or cares which key/button/stick it
   came from.
2. **Gameplay reacts to actions, not to keys.** Movement, jumping, shooting read
   the named actions. Now a gamepad, a remap, or an AI-driven "player" all feed the
   same actions with zero gameplay changes.
3. **Distinguish pressed / held / released.** "Jump" on the *press* (an event),
   "move" while *held* (per-frame), "charge released" on the *release*. Mixing these
   up is a classic bug (a jump that fires every frame you hold the button).
4. **Read input event-driven where you can.** A menu selection is an event, not
   something to poll every frame; continuous movement is a per-frame read of a held
   action. Use the right one.

**In practice:**

- **Godot**: this is built in — define input actions in Project Settings > Input
  Map, then `Input.is_action_pressed("jump")` / `Input.is_action_just_pressed(...)`.
  Use it; do not hard-code `KEY_W`.
- **Elsewhere**: build a tiny input layer — a dictionary mapping keys to action
  names, updated once per frame, that the rest of the game queries by action name.

**Why it matters:** decoupling input from gameplay is what makes remapping,
controllers, and testing (feed fake actions to test movement without a keyboard)
possible — and it keeps a whole category of "it fires twice / it doesn't fire /
it's stuck" bugs from ever appearing.
