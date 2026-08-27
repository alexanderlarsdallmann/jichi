---
name: entity-component
description: How to organize game objects with composition (components/nodes) instead of deep inheritance — so behaviour is reusable and the code stays flexible.
---
# Organizing game objects: composition over inheritance

As a game grows, the hard question becomes "how do I structure my objects?" The
beginner instinct is a deep inheritance tree — `Entity` > `Character` > `Enemy` >
`FlyingEnemy` > `FlyingShootingEnemy` — and it collapses fast, because real games
mix and match behaviours (a flying-shooting-exploding-healing enemy). Inheritance
forces one line; games need a grid.

**Composition is the answer.** Instead of an object that *is* many things, build an
object that *has* many small behaviours (components):

- A `Health` component (has HP, takes damage, dies).
- A `Movement` component (moves by input or AI).
- A `Shooter` component (fires on a cooldown).
- A `Collision` component, an `Animation` component, ...

An entity is then a bag of components: a player has Health + Movement(input) +
Shooter; an enemy has Health + Movement(AI) + Shooter; a crate has Health +
Collision. New behaviour is a new component, reused everywhere — no tree to fight.

**In practice, per engine:**

- **Godot**: the node tree *is* composition. A `CharacterBody2D` with child nodes
  (a `Sprite`, a `CollisionShape`, a custom `Health` node) — each node a component.
  Scenes are reusable component bundles.
- **Elsewhere**: components as small classes/tables the entity holds a list of, each
  with an `update(delta)` the entity calls. (A full "ECS" — entity-component-system
  — is the industrial version; you rarely need the full thing to benefit from the
  idea.)

**Why it matters for a learner:** each component is small, testable in isolation,
and reused instead of copied. When you want a new kind of thing, you compose
existing components rather than adding a branch to a tree that already hurts. Start
simple — you do not need a framework, just the habit of "small reusable behaviours,
combined."
