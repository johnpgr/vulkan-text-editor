# Philosophy: Why This Style Exists

This document explains the *reasoning* behind Casey Muratori's programming approach. Understanding the "why"
helps Claude make correct judgment calls in situations the main SKILL.md doesn't explicitly cover.

## Table of Contents
1. [The OOP Critique](#the-oop-critique)
2. [Semantic Compression](#semantic-compression)
3. [Performance Culture](#performance-culture)
4. [On Libraries and Dependencies](#on-libraries-and-dependencies)
5. [The Platform Layer Philosophy](#the-platform-layer-philosophy)
6. [On Code Complexity and Readability](#on-code-complexity-and-readability)
7. [Common Misconceptions](#common-misconceptions)

---

## The OOP Critique

Casey's central argument against OOP is that it inverts the natural programming process. OOP tells you to
model the world as objects first, then fill in behavior. But code is fundamentally *procedural* — it's a
sequence of instructions. Objects are just constructs that arise when you compress repeated procedural
patterns.

The problems Casey identifies with OOP:

- **Pre-planned architecture fails.** You can't know the right abstractions before you've written the code.
  Real-world code is full of details that defy clean hierarchies. The payroll example (person → employee →
  manager → contractor) shows how quickly naive object modeling breaks down.

- **Inheritance creates coupling.** When you inherit, you couple the child to every decision the parent made.
  Changing the parent risks breaking all children. The "fragile base class" problem is real and pervasive.

- **Virtual dispatch is hidden complexity.** When you see `entity->update()`, you can't tell what code will
  actually run without tracing through the vtable. A `switch(entity->type)` in a single function is explicit,
  greppable, and debuggable — you can see all entity behaviors in one place.

- **Encapsulation is often counterproductive.** Making fields private and adding getters/setters doesn't
  protect invariants — it just adds bureaucracy. If the struct's layout is wrong, fix the struct. Don't hide
  it behind an API and pretend the problem is solved.

- **The STL embodies these problems.** `std::vector` hides its allocation strategy behind an interface you
  can't efficiently control. `std::string` has different behaviors across platforms. `std::map` forces
  tree-based lookup when a hash table or sorted array would be better for your use case. These "general
  purpose" tools are general at the cost of being good for anything specific.

The alternative: write plain code. When you see patterns, compress. Let the architecture *emerge.*

---

## Semantic Compression

This is Casey's term for his programming methodology. The analogy is literal: treat your code the way a
compression algorithm treats data. Look for repeated or similar patterns and compress them.

### The Compression Workflow

**Step 1: Write it specific and inline.**
Don't think about reuse. Write exactly what this one place in the code needs to do, in the simplest possible
way. If your algorithm is "do X, then Y, then Z," write three blocks of code for X, Y, and Z.

**Step 2: Wait for the second occurrence.**
You wrote a similar block of code somewhere else? *Now* you have two real examples of what the shared code
needs to do. This is the critical insight — you need at least two concrete instances before you can
meaningfully generalize.

**Step 3: Extract and compress.**
Pull out the shared logic into a function. The function's signature is dictated by the actual parameters the
two call sites use — not by what you imagine future callers might need.

**Step 4: Continue compressing iteratively.**
As you build more code, you may find that the extracted function itself participates in repeated patterns at
a higher level. Compress again. This naturally produces a layered architecture where each level is informed
by real usage.

### The "Shared Stack Frame" Pattern

When multiple functions keep passing the same group of variables around, bundle those variables into a struct.
Casey calls this a "shared stack frame" — it's like making local variables visible across a set of cooperating
functions. This is how `PanelLayout` was born in The Witness's editor code: the layout variables (`at_x`,
`at_y`, `row_height`, `width`) were extracted into a struct, and layout operations became functions that
took a pointer to that struct.

### Granularity Preservation

A critical rule: **never supply a high-level function without keeping the lower-level functions available.**
If `draw_complete_ui()` calls `begin_panel()`, `add_row()`, `add_button()`, and `end_panel()`, all of those
building blocks must remain available for direct use. If a caller needs something the high-level function
doesn't support, they should be able to trivially replace it with the lower-level calls plus their custom
code.

This prevents "granularity discontinuities" — situations where the only way to do something slightly
different is to rewrite the high-level function entirely.

---

## Performance Culture

Casey doesn't treat performance as an optimization pass you do at the end. Performance awareness is built
into every decision:

### Data-Oriented Thinking
- Think about what data you actually need to process and how it's laid out in memory.
- A flat array of `Entity` structs that you iterate linearly is cache-friendly. A tree of polymorphic
  `Entity*` pointers scattered across the heap is not.
- When you need to process only one field across many entities (e.g., just positions for collision),
  consider struct-of-arrays (SoA) layout.

### No Hidden Costs
- Virtual dispatch, RTTI (`dynamic_cast`), exceptions, `std::function`, and hidden allocations all
  have costs that are invisible at the call site. Casey's style makes every cost visible.
- If a function allocates memory, it does so from an arena you passed in — you can see it.
- If a function dispatches on type, it does so with a `switch` — you can see the branch.

### Measure, Don't Guess
- Use the CPU's performance counters and profiling tools. Casey's Performance-Aware Programming course
  teaches this extensively.
- The mental model of "modern CPUs are fast enough" is wrong. Software has gotten slower even as hardware
  has gotten faster, because of accumulated abstraction overhead.

### SIMD and Intrinsics
- When performance matters (and it often does in game/engine code), use SSE/AVX intrinsics directly.
- Casey hand-writes SIMD code in Handmade Hero for the software renderer. The style doesn't shy away from
  low-level optimization when the payoff is real.

---

## On Libraries and Dependencies

Casey's position is not "libraries are always bad" — it's that **every dependency is a cost**, and
programmers dramatically underestimate that cost:

- You inherit the library's bugs, its platform quirks, its update schedule, and its design decisions.
- When the library doesn't do what you need, you have to work around it — often spending more time than
  writing the functionality yourself would have taken.
- Linking a library to save writing 200 lines of platform code means you now depend on thousands of lines
  you didn't write, can't fully understand, and can't easily fix.

The practical guideline: if you can write it in a reasonable time and it will be better tailored to your
needs, write it yourself. Use OS APIs directly — they're stable, well-documented, and you're depending
on them anyway.

For truly complex, algorithmic work (video codecs, physics solvers), using a library can make sense —
but the decision should be conscious and the trade-offs understood.

---

## The Platform Layer Philosophy

Handmade Hero's architecture cleanly separates the platform from the application:

```
win32_handmade.cpp  (Platform Layer)
  ├── Creates window, handles messages
  ├── Manages audio/video buffers
  ├── Reads input devices
  ├── Loads game DLL, calls game_update_and_render()
  └── Handles file I/O on behalf of the game

handmade.cpp  (Application / Game)
  ├── Receives GameMemory, GameInput
  ├── Writes to GameOffscreenBuffer, GameSoundBuffer
  ├── NEVER calls Win32/POSIX/etc. directly
  └── Pure computation — trivially portable
```

This separation enables:
- **Hot code reloading:** The platform layer can reload `handmade.dll` while the game is running. You change
  code, recompile, and see the result instantly without restarting.
- **Looped live editing:** Record input, replay it in a loop, edit code while it replays — the ultimate
  iteration tool.
- **Easy porting:** To port to Linux, you write `linux_handmade.cpp`. The game code doesn't change at all.

---

## On Code Complexity and Readability

Casey's view: the biggest factor in code readability is *size*. Small codebases are readable. Large
codebases are not — no matter how "clean" the code is.

Implications:
- Don't add abstractions that increase total code size in the name of "readability."
- A long function that does one clear thing top-to-bottom is more readable than five tiny functions
  that force you to jump around to understand the flow.
- Comments should explain *why*, not *what*. The code itself should be clear enough to show *what*.
- If you need a comment to explain *what* the code does, the code is probably too clever. Simplify it.

---

## Common Misconceptions

**"This only works for solo developers."**
Casey directly addresses this: compression-oriented programming works on teams. The code structure is
actually *easier* to navigate in a team because the architecture reflects actual usage patterns rather
than someone's theoretical object model. New team members can read top-down and understand what happens.

**"This only works for games."**
The principles — flat data structures, explicit control flow, minimal dependencies, architecture from
usage — apply to any domain. Web servers, compilers, data processing pipelines, GUI applications — all
benefit from the same approach.

**"This is just 'refactoring.'"**
Casey acknowledges the similarity but rejects the term. Formal "refactoring" in the Fowler sense is
embedded in OOP assumptions (extract class, introduce interface, replace conditional with polymorphism).
Compression-oriented programming is specifically about *not* forcing code into OOP shapes — it's about
finding the natural structure that the code wants to have.

**"This produces unmaintainable spaghetti code."**
The opposite is true. Because every abstraction exists only because it was needed at least twice, and
because low-level building blocks are always preserved alongside high-level ones, the code is both
compact and flexible. There are no "framework tax" costs — no code exists that doesn't directly
contribute to the program's purpose.
