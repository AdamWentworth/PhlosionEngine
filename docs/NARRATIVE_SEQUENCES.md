# Narrative sequence playback

Link `Phlosion::Narrative` and include
`engine/narrative/SequencePlayer.h`. `SequencePlayer` owns a validated copy of a
linear list of steps. A step has a unique ID and either a positive fixed tick
duration or one or more UTF-8 dialogue pages. The host selects its tick rate and
binds each step ID to actors, animation tracks, cameras, sounds and presentation.

Call `tick(confirmDown, enabled)` once per fixed update. A fresh press reveals
the current page; another fresh press advances it. Holding confirm cannot
consume multiple pages or carry a press from a timed step into dialogue. UTF-8
code points reveal atomically. Disabled playback freezes time and requires a
neutral confirmation sample before accepting input again.

`restart()` resets progression and the input latch. `skip()` finishes the
sequence. The host owns world/input restoration, completion persistence, replay
policy and content validation beyond the sequence contract. The engine does
not own content paths, speaker identities, narrative JSON, game flags, quest
rules, character bindings or UI styling.

Contracts: `PhlosionNarrativeContracts` in the engine test build. Games that
embed the engine with its tests disabled can compile the same contract source
against `Phlosion::Narrative` (Cyberpunk does this).
