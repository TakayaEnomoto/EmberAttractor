# Reference game notes

The assignment asks to play or watch at least two reference games and name the one the mechanic borrows from. These are my own observations, gathered before and during the project. The table in the brief points at *PixelJunk Shooter* (water, lava and gas interactions as the core puzzle) and *Noita* (every pixel simulated; fire, liquid and gas as gameplay).

## PixelJunk Shooter — watched

**How:** about an hour of a longplay, covering the first two episodes: the rock, water and lava caverns, then the ice and flammable-gas areas. I skipped ahead to the fluid interactions and the boss fights.

**Material interaction as puzzle.** The interaction that stuck with me most is lava + water → a crust of cooled rock, chained with flammable gas or oil. It changes the level's topology on the fly. Survivors are often trapped under or beyond a lava pool that would overheat the ship. You release water from above, it solidifies the lava surface into brittle rock, and a missile then opens a safe passage. The reverse can happen too: careless flooding seals a passage you still need, and you have to find fire or explosives to reopen it. The puzzle is predicting where the fluid will flow.

**Indirect control.** Early on you cannot create material, only redirect it: break soft terrain to release a flood, or carry it with the ship. Blasting a thin wall and watching water rush down onto lava feels great. Later tools that soak up or spray liquid over an area also give strong feedback. Spinning the ship to scoop and fling small amounts of liquid feels awkward, though. The particle arc and the ship's inertia rarely line up, so you miss or graze the lava and overheat.

**Reading state changes.** Hardly any UI is needed. Lava is bright, slow-moving orange. Water hitting it throws off white steam, and the contact surface darkens into rough, cracked rock that reads as breakable. Sound backs this up with a hiss when lava quenches and a dull crack as the rock sets. The ship's health is a heat gauge: heat shimmer and a red, beeping gauge near lava, then a crisp cooling sound when you dive into water.

## Noita — played

**State changes that flip danger and value.** Toxic sludge plus water turns into clean water, so a hazard you lose health in becomes a resource for putting out fires. The opposite extreme is polymorphine: a few leftover pink pixels can turn you into a sheep and get you killed instantly. A handful of simulated cells can decide the run.

**Readability under load.** I can read the overall trend, but I often miss the detail that kills me. When wooden structures burn, oil tanks explode and water pours in, fire, smoke, steam, blood and oil all overlap. Two deaths stand out. In one, water, blood and oil mixed until I couldn't see that I was standing in conductive liquid, and an electric spell or a broken lamp electrified the pool. In the other, a single leftover pixel of rock or ice I couldn't see blocked a jump or a point-blank explosion.

**Emergent highs and lows.** The high point: instead of fighting a group of elite enemies, I shot a barrel above them. Burning oil ate through the wooden supports, an acid tank dropped, and the acid dissolved the floor with the enemies on it while their gold fell to me. The low point: testing a freshly built wand in the Holy Mountain, I knocked over an oil barrel and set off a chain reaction. Physics debris crushed me and the guardian came after me, and half an hour of progress was gone.

## What Ember Attractor borrows

- **Noita (the reference the mechanic borrows from):** manipulating loose material through a force field rather than steering a character. The black hole pulls, swirls and damps a crowd of embers; the skill is predicting their inertia. The look is closest to Noita's gravity effects tugging at sparks and loose pixels.
- **PixelJunk Shooter (secondary):** state that changes value, and reading that state through colour rather than UI. Embers lose value as they cool, and their colour fades from hot orange to dark, so the player has to gather them while they are still bright. Heat as the central resource also echoes the ship's heat gauge. Like Shooter's puzzles, the round is solved by redirecting material indirectly, under a resource limit (energy), against a state change (cooling).

## What they do that we do not yet

- **Particles do not interact with each other or turn into other materials.** Both games get their depth from reactions: water quenching lava, oil catching fire. A next step would be a second particle species. Water droplets swept into the hole would quench embers into worthless steam (a penalty), and embers dragged across flammable gas would ignite it and spawn new embers (a chain reaction).
- **Accumulation doesn't produce anything new.** Swallowed matter is only counted. Absorbing a dense burst of hot embers could overload the core and eject a solid molten blob with real collision, turning collection into terrain-changing play.
- **Readability at scale** is something to protect: Noita shows how a crowded simulation hides the one cell that matters. Our cold embers fade to near-black, and captured debris could use a stronger colour cue. The planned debris state machine does exactly that.
