# Plan: Transform Game Engine into GPU Text Editor

## Context

The codebase at `/Users/sandragreidinger/dev/cpp-gaming` is a multi-threaded game engine (Vulkan + GLFW, Casey Muratori style). The architecture spec (`architecture_v2.md`) defines a GPU-accelerated text editor with: single-threaded frame loop, Slug font rendering, HarfBuzz shaping, Tree-sitter highlighting, rope text buffer, and a push-command rendering pipeline.

This plan transforms the game engine into the editor in 8 phases. Each phase produces a compilable, runnable result. Work happens on a new branch `editor`.

### Key Decisions
- **Types**: Keep `i8/u8` (existing convention), not `s8/u8` from spec
- **Arena**: Raddebugger-style chained blocks (64MB reserve / 64KB commit default). Header lives in first block (`ARENA_HEADER_SIZE=128`). Auto-chains when current block full. `arena_alloc()` / `arena_release()` / `arena_push()` / `arena_pop_to()` / `arena_clear()` / `temp_begin()` / `temp_end()`.
- **Font rendering**: Slug (MIT/Apache-2.0 dual-licensed). Ref repo has shaders only — no font compiler. We build the full CPU-side pipeline (TTF→curve/band textures) + manual GLSL port of the two HLSL shaders
- **Font source**: System font discovery at runtime (no bundled font file)
- **Text buffer**: SumTree rope (Zed-style B+ tree, `src/text/text_buffer.h`). `TEXT_CHUNK_MAX=128` (keep — cache-friendly edits; bump `TEXT_TREE_BASE` 6→8 later if profiling shows depth matters). COW snapshots with full-spine refcount bumps for incremental path-copying. Delete rebalancing (merge underflowed nodes). `TextIterator` for zero-copy sequential reads. `TextAnchor` (global byte offset + bias) for cursor/selection/bookmarks.
- **Undo**: COW snapshot-per-edit. `TextEdit { offset, old_len, new_len, TextSnapshot before }` stored in undo stack. Undo restores by swapping root + releasing current snapshot.
- **Multi-buffer**: Per-buffer arena (close = full memory reclaim). `EditorBuffer` owns `TextDocument*`, cursor anchor, `TSTree*`.
- **File I/O**: `read()` into arena (not mmap)
- **Tree-sitter**: Async from the start. Background parse thread via `ThreadMutex`/`ThreadConditionVariable`. Main thread takes COW snapshot, enqueues parse request, parser thread runs against snapshot, posts `TSTree*` back. Main thread picks up new tree next frame.
- **Windows**: `build.bat` added in Phase 0 — keep Windows compiling from day one

---

## Phase 0: Scaffold — Strip Game, Remove Lanes

**Goal**: Empty GLFW+Vulkan window, single-threaded loop, dark clear color. Compiles and runs.

### Delete
- `src/game/` (entire directory: `game.cpp`, `game_math.h`, `game_render_group.h`)
- `src/game_api.h`
- `src/base/lane.h`

### Create
- `src/draw/draw_core.cpp` — minimal: `PushCmdBuffer` with `CmdType` enum, `PushCmd` tagged union, `push_cmd()`, `push_rect()`, `push_clear()`. Single flat buffer (128K cmds). No lanes.

### Modify
- `src/render/vulkan.h` — remove `#include "base/lane.h"`, `#include "game/game_math.h"`, `#include "game/game_render_group.h"`. Remove `lane_pools[MAX_LANES]`, `lane_cmds[MAX_LANES]`, `active_lane_count`. Add `#include "draw/draw_core.h"`. Keep single `primary_pool`/`primary_cmd`.
- `src/render/vulkan.cpp` — remove `init_lane_command_pools()`, all secondary command buffer logic, lane-indexed recording. `render_group_to_output()` becomes `render_drain_cmd_buffer(PushCmdBuffer *buf)` — iterates flat buffer, emits rects via existing sprite pipeline push constants. Remove all `lane_idx()` / `lane_count()` references.
- `src/app/editor_main.cpp` — unity build root:
  - Remove: `#include "game_api.h"`, `#include "base/lane.h"`, all `GameCode`/`dlopen`/`dlsym`, `LaneThreadParams`, lane thread creation/join, `LaneBarrier`, `init_lane_barrier`, `run_frame_loop` lane logic
  - Add: `#include "base/types.h"` (before other includes)
  - Simplify to single-threaded loop: `glfwPollEvents()` → `begin_frame()` → push clear cmd → `render_drain_cmd_buffer()` → present
  - Window title: "editor", size 1280x800
  - Two arenas: `permanent_arena` (chunk_size=megabytes(1)) and `transient_arena` (chunk_size=megabytes(1))
- `src/base/arena.h` — make `ARENA_RESERVE_SIZE` overridable: `#ifndef ARENA_RESERVE_SIZE`
- `build.sh` — remove game .dylib build, change `editor_main.cpp`, remove `-ldl` if present

### Create
- `build.bat` — MSVC build script: `cl /std:c++11` compiling `editor_main.cpp`, linking `glfw3.lib`, `vulkan-1.lib`. Shader compilation step. Debug/release modes. Mirror structure of `build.sh`.

### Reuse
- `src/base/core.h` — added DLL/SLLQueue/SLLStack linked list macros (raddebugger style)
- `src/base/memory.h` — as-is (reserve/commit/decommit/release)
- `src/base/arena.h` — rewritten to raddebugger chained-block style (see Key Decisions). `push_array(arena, T, count)` param order.
- `src/base/log.h` — as-is
- `src/base/string.h` — updated 3 `push_array` calls to new param order
- `src/base/threads/` — as-is (needed later for highlight thread + worker pool)
- `assets/shaders/sprite.vert`, `sprite.frag` — keep temporarily for rect rendering

### Verify
```bash
./build.sh debug && ./bin/main
# Window opens, dark background, handles resize, ESC closes
```

---

## Phase 1: Editor State + Input + Cursor Rect

**Goal**: EditorState struct, GLFW key/char/scroll callbacks, render a blinking cursor rect. Proves push-command → Vulkan pipeline works single-threaded.

### Create
- `src/editor/editor_input.h` — `EditorInput` struct: `key_events[]` array (KeyEvent: key, mods, pressed), `scroll_delta`, `mouse_x/y`, `window_width/height`, `char_input[]` buffer. GLFW callback wiring functions.
- `src/editor/editor_core.cpp` — `EditorState` struct (initially: permanent/transient arena ptrs, cursor pos, blink timer, dirty flag). `frame()` entry point: reset transient arena, process input, push cursor rect, call renderer.

### Modify
- `src/app/editor_main.cpp` — add `glfwSetKeyCallback`, `glfwSetCharCallback`, `glfwSetScrollCallback` that fill `EditorInput`. Call `frame()` each iteration.

### Verify
```bash
# Blinking cursor rect visible. Arrow keys move it. Window title shows "editor".
```

---

## Phase 2: Text Buffer — SumTree Rope

### Phase 2a: Core Rope ✅

**Goal**: UTF-8 rope with insert/delete/line-query. No text rendering yet — validate via LOG_DEBUG.

- `src/text/text_buffer.h` — SumTree B+ tree (Zed-style):
  - `TextSummary { bytes, lines, codepoints, tail_codepoints, utf16_units }` — monoid aggregated up tree
  - `TextChunk` — 128-byte leaf chunks with precomputed summary
  - `TextLeaf` / `TextNode` — B+ tree nodes (base=6, cap=12), doubly-linked leaf list
  - `TextDocument` — root, leaf list, total summary, free lists for node/leaf recycling
  - `text_document_create`, `text_insert`, `text_delete`
  - `text_offset_to_point`, `text_point_to_offset`, `text_line_content`
  - `text_content_size`, `text_line_count`
- `src/editor/editor_core.cpp` — `EditorState` gets `TextDocument *document`. Char input → `text_insert`. Backspace → `text_delete`. Arrow keys use `cursor_to_offset` / `cursor_from_offset`.

### Phase 2b: Iterator API ✅

**Goal**: Zero-copy sequential reads over byte/line ranges. Unblocks viewported rendering + Tree-sitter input.

Add to `src/text/text_buffer.h`:
- `TextIterator { TextDocument* doc, TextLeaf* leaf, u16 chunk_index, u16 byte_index, u64 global_offset }`
- `text_iterator_at_offset(doc, offset)` — wraps `text_find_leaf`, resolves to chunk/byte pos
- `text_iterator_at_line(doc, line)` — `text_point_to_offset` → `text_iterator_at_offset`
- `text_iterator_read(iter, buf, max_bytes)` — copy bytes, advance across chunks/leaves via linked list
- `text_iterator_advance(iter, n_bytes)` — skip without copy
- Reimplement `text_line_content` on top of iterator (same behavior, cleaner internals)

~100 LOC. No `core.cpp` changes.

### Phase 2c: Delete Rebalancing ✅

**Goal**: Tree stays balanced after deletes. Prevents degenerate trees from long editing sessions.

Add to `src/text/text_buffer.h`:
- `leaf_try_merge(doc, node, child_idx)` — if leaf count < `TEXT_TREE_BASE`, merge with adjacent sibling or redistribute
- `node_try_merge(doc, parent, child_idx)` — same at internal node level
- Hook into `text_delete` upward walk: after content removal, check underflow at each level
- Update linked-list pointers on leaf merge

~80-100 LOC. No `core.cpp` changes.

### Phase 2d: Anchors ✅

**Goal**: Positions that survive edits — for cursor, selections, Tree-sitter edit ranges, bookmarks.

Add to `src/text/text_buffer.h`:
- `TextAnchor { u64 offset; TextAnchorBias bias; }` — `LEFT`/`RIGHT` bias for insert-at-anchor behavior
- Flat anchor array + count on `TextDocument` (linear scan fine for <100 anchors)
- `text_anchor_create/destroy/get_offset`
- Internal `text_anchors_adjust_insert(doc, offset, len)` and `_delete` — called from `text_insert`/`text_delete`

Refactor `src/editor/editor_core.cpp`:
- Replace `cursor_row/cursor_column` with anchor ID. `cursor_to_offset` reads anchor directly. Edits auto-update cursor.

~80 LOC text_buffer.h, ~30 LOC core.cpp.

### Phase 2e: COW Snapshots (undo-aware) ✅

**Goal**: Immutable tree snapshots for background Tree-sitter + foundation for undo.

Add to `src/text/text_buffer.h`:
- `u32 ref_count` on `TextNode` and `TextLeaf` (init 1 on alloc)
- `TextSnapshot { TextNode* root; TextSummary total; u64 version; }`
- `text_snapshot(doc)` — copy root ptr, bump refcounts on full root-to-leaf spine. O(depth × branching).
- `text_snapshot_release(doc, snapshot)` — decrement refcounts, free refcount==0 nodes to free list
- COW on mutation: before modifying node/leaf with refcount > 1, clone via `push_struct`, decrement old refcount
- `text_snapshot_read(snapshot, offset, buf, max_bytes)` — tree-descent iterator (no leaf linked list — snapshot leaves may have stale prev/next)
- Version counter on `TextDocument`, incremented on each edit
- `TextEdit { u64 offset; u64 old_len; u64 new_len; TextSnapshot before; }` — undo stack (flat array on doc). Undo restores by swapping root + releasing current.

~180-220 LOC. No core.cpp changes.

### Verify (all of Phase 2)
```bash
# Cursor movement + editing still works. text_line_content returns same results.
# Insert+delete cycles produce balanced trees. Check tree depth after bulk deletes.
# Cursor position survives inserts/deletes at various positions relative to cursor.
# Snapshot read returns same content as live doc at snapshot time.
# Edits after snapshot don't corrupt snapshot. Undo restores previous state.
```

---

## Phase 3: Slug Font Rendering Pipeline

**Goal**: Render text on screen using Slug's GPU Bezier evaluation. This is the most complex phase.

**Reference repo**: `github.com/EricLengyel/Slug` (MIT OR Apache-2.0, dual-licensed). Contains only two shaders — `SlugVertexShader.hlsl` (4.2KB) and `SlugPixelShader.hlsl` (9KB). No font compiler, no CPU-side code. Everything else we build ourselves.

### Phase 3a: Font Data Pipeline (CPU side)

**Goal**: Extract Bezier curves from TTF, build Slug-format GPU textures.

#### External Dependencies
- `stb_truetype.h` — single header, for TTF outline extraction (not rasterization)
- Reference: `github.com/mightycow/Sluggish` CPU implementation (guide for curve extraction + band building)

#### Create
- `src/font.cpp`:
  - **System font discovery**: find monospace font at runtime (macOS: CoreText, Linux: fontconfig, Win32: DirectWrite/registry). Fallback search paths.
  - **TTF outline extraction** via stb_truetype: `stbtt_GetGlyphShape()` returns quadratic Bezier control points per glyph. Convert cubic curves to quadratic if needed.
  - **Curve texture builder** (`VK_FORMAT_R16G16B16A16_SFLOAT`, width=4096):
    - Pack control points: texel N = `(p1.x, p1.y, p2.x, p2.y)`, texel N+1 = `(p3.x, p3.y, ...)`. Adjacent curves share endpoint texel (p3 of curve K = p1 of curve K+1).
    - All coordinates in em-space (normalized by units_per_em).
  - **Band texture builder** (`VK_FORMAT_R16G16B16A16_UINT`, width=4096):
    - For each glyph: choose band count to minimize max curves per band. Use epsilon 1/1024 em-space for band overlap.
    - Horizontal bands: curves sorted descending by max x-coordinate. Exclude straight horizontal lines.
    - Vertical bands: curves sorted descending by max y-coordinate. Exclude straight vertical lines.
    - Band data layout: `[hband_count, hband_curve_offset, ...]` then `[vband_count, vband_curve_offset, ...]`. Adjacent bands with identical curve sets can share data.
    - Each band entry = `(curve_count, offset_to_curve_list)` in uint2.
  - **Glyph metrics**: advance width, bearing, bounding box. Read `sCapHeight` from OS/2 table for pixel-grid alignment (`font_size * sCapHeight` should be integer).
  - `struct FontData { VkImage curve_tex; VkImageView curve_view; VkImage band_tex; VkImageView band_view; VkSampler sampler; GlyphMetrics *metrics; u32 glyph_count; f32 units_per_em; u16 cap_height; }`
  - `font_load(Arena*, VkDevice, char const *ttf_path) -> FontData`

### Phase 3b: Slug GLSL Shaders

**Goal**: Port the two reference HLSL shaders to GLSL 450.

#### HLSL→GLSL translation map
| HLSL | GLSL |
|---|---|
| `asuint(x)` | `floatBitsToUint(x)` |
| `saturate(x)` | `clamp(x, 0.0, 1.0)` |
| `frac(x)` | `fract(x)` |
| `fwidth(x)` | `fwidth(x)` (same) |
| `Texture2D.Load(int3(xy, 0))` | `texelFetch(sampler, ivec2(xy), 0)` |
| `cbuffer` | `layout(push_constant) uniform` or UBO |
| `SV_Position` | `gl_Position` |
| `SV_VertexID` | `gl_VertexIndex` |

#### Create
- `assets/shaders/glyph.vert` — GLSL 450 port of `SlugVertexShader.hlsl`:
  - **Uniforms** (push constant or UBO): `mat4 slug_matrix` (MVP, used as 4 row-vectors), `vec2 slug_viewport` (pixel dimensions)
  - **Vertex inputs** (5 vec4 attributes at locations 0-4):
    - `loc 0` pos: `.xy` = object-space position, `.zw` = object-space normal (for dilation direction)
    - `loc 1` tex: `.xy` = em-space sample coords, `.z` = packed band texture location (uint: low 16 = x, high 16 = y), `.w` = packed band max indices + flags
    - `loc 2` jac: inverse Jacobian matrix entries (2x2: `00, 01, 10, 11`)
    - `loc 3` bnd: `(band_scale_x, band_scale_y, band_offset_x, band_offset_y)`
    - `loc 4` col: vertex color RGBA
  - **Core operation**: `SlugDilate()` — expands glyph quad boundary using MVP rows + viewport to compute exact pixel-correct dilation. Outputs dilated em-space texcoord.
  - **Outputs**: clip-space position, em-space texcoord (interpolated), banding (flat), glyph data (flat), color (interpolated)
- `assets/shaders/glyph.frag` — GLSL 450 port of `SlugPixelShader.hlsl`:
  - **Descriptor bindings**: `binding 0` = curveTexture (sampler2D, R16G16B16A16_SFLOAT), `binding 1` = bandTexture (usampler2D, R16G16B16A16_UINT)
  - **Algorithm** (per-pixel):
    1. Compute `emsPerPixel = fwidth(texcoord)`, `pixelsPerEm = 1.0 / emsPerPixel`
    2. Determine horizontal + vertical band indices from `texcoord * bandTransform.xy + bandTransform.zw`, clamp to `[0, bandMax]`
    3. **Horizontal band loop**: fetch curve count + offset from band texture. For each curve: load 3 control points from curve texture (subtract renderCoord to make sample-relative). Early exit when `max(p1.x, p2.x, p3.x) * pixelsPerEm.x < -0.5` (curves sorted descending by max x). `CalcRootCode()` via sign-bit extraction determines which roots contribute. `SolveHorizPoly()` solves `at² - 2bt + c = 0`. Accumulate coverage + weight.
    4. **Vertical band loop**: same logic, rotated 90°. Early exit on max y. `SolveVertPoly()`.
    5. `CalcCoverage()`: combine h/v coverages weighted, clamp via nonzero fill rule. Optional `sqrt()` for optical weight boost.
  - **Output**: `vec4(color.rgb * coverage, color.a * coverage)` (premultiplied alpha)

### Phase 3c: Vulkan Pipeline + Integration

#### Create
- `src/layout.cpp`:
  - `struct LineLayout { f32 y_baseline; f32 x_start; u32 rope_byte_start; u32 rope_byte_end; }`
  - `struct ViewLayout { LineLayout *lines; u32 line_count; f32 scroll_y; f32 line_height; f32 gutter_width; }`
  - `layout_compute(ViewLayout*, Rope*, f32 viewport_w, f32 viewport_h, f32 font_size)` — compute visible lines from scroll position

#### Modify
- `src/render/vulkan.h` — add: `VkPipeline glyph_pipeline`, `VkPipelineLayout glyph_pipeline_layout`, `VkDescriptorSetLayout glyph_desc_layout`, `VkDescriptorPool glyph_desc_pool`, `VkDescriptorSet glyph_desc_set`, `FontData *font`, `VkBuffer glyph_vertex_buf`, `void *glyph_vertex_mapped` (host-visible, persistently mapped)
- `src/render/vulkan.cpp`:
  - **Descriptor set**: 2 combined image samplers (curve tex at binding 0, band tex at binding 1)
  - **Glyph pipeline**: vertex input with 5 vec4 attributes (stride = 80 bytes), alpha blending (`srcAlpha, oneMinusSrcAlpha`), dynamic viewport/scissor, push constants for MVP + viewport
  - **Vertex buffer**: host-visible, persistently mapped. Each glyph = 4 vertices × 5 vec4 = 320 bytes. Budget for ~4K visible glyphs = ~1.3MB.
  - **Index buffer**: shared quad index pattern (0,1,2, 2,3,0) repeated
  - `render_emit_glyph_run()` — for each glyph in run: compute object-space quad corners + normals from metrics, compute em-space texcoords, pack inverse Jacobian from glyph transform, write 4 vertices. One draw call per glyph run (instanced or batched).
  - Keep sprite pipeline for rect rendering
- `src/draw/draw_core.cpp` — add `CmdGlyphRun` type (baseline_pos, color, layer, glyph_count, GlyphInfo*)
- `src/editor/editor_core.cpp` — after layout, iterate visible lines, push CmdGlyphRun per line
- `build.sh` — compile glyph.vert + glyph.frag to SPIR-V

### Verify
```bash
# Open editor, type text, see Bezier-rendered glyphs at any zoom level.
# Resize works. Scroll works. No pixelation at any size.
```

### Fallback
If Slug shader porting proves too complex in one pass, temporarily use stb_truetype atlas rendering to unblock later phases, then circle back. The push-command layer abstracts the backend — swapping is a pipeline-only change.

---

## Phase 4: Core Editor Features + Multi-Buffer

**Goal**: Usable minimal editor — file open/save, multi-buffer, selection, copy/paste, undo/redo, navigation.

### Create
- `src/file.cpp` — `open_file(EditorState*, char const *path)` reads file (`read()` into per-buffer arena) into rope. `save_file(EditorBuffer*, char const *path)` serializes rope to disk. CLI arg parsing in main.

### Modify
- `src/editor/editor_core.cpp`:
  - **Multi-buffer**: `EditorBuffer { Arena* arena; TextDocument* doc; u32 cursor_anchor; i32 desired_column; char filepath[PATH_MAX]; bool dirty; u64 version; }`. `EditorState` becomes `{ EditorBuffer* buffers; u32 buffer_count; u32 active_buffer; ... }`. `editor_open_file`, `editor_close_buffer` (releases per-buffer arena = full reclaim), `editor_switch_buffer`.
  - **Undo**: Ctrl+Z / Ctrl+Shift+Z via COW undo stack from Phase 2e (`TextEdit.before` snapshot → swap root to restore)
  - Selection: `struct Selection { u64 anchor; u64 cursor; }` (uses TextAnchors from Phase 2d), Shift+arrow extends
  - `push_selection_rects()` — colored rects behind selected text (layer 1)
  - Copy/paste via `glfwGetClipboardString` / `glfwSetClipboardString`
  - Navigation: Home/End, Ctrl+Home/End, Ctrl+Left/Right (word boundaries), Page Up/Down
  - Dirty-state gating: skip frame + sleep if no input and no pending highlight
  - `move_cursor`/`push_cursor` operate on `EditorBuffer*`
- `src/draw/draw_core.cpp` — add `push_cursor_line()` convenience
- `src/app/editor_main.cpp` — parse argv for file path, call `open_file` before loop

### Verify
```bash
./bin/main src/editor/editor_core.cpp
# Opens file, navigate, edit, Ctrl+S saves. Undo works. Selection + Ctrl+C/V works.
# Idle CPU near zero (dirty gating).
```

---

## Phase 5: HarfBuzz Shaping + Shape Cache

**Goal**: Proper Unicode text shaping. LRU cache for shaped glyph runs.

### External Dependencies
- HarfBuzz library (system install or vendored): `pkg-config --libs harfbuzz`

### Create
- `src/shaper.cpp` — `ShaperContext` wrapping `hb_font_t*`, `hb_buffer_t*`. `shaper_init(ShaperContext*, char const *font_path)`. `shape_utf8(ShaperContext*, u8 *text, u32 len, f32 font_size, Arena *scratch) -> GlyphRun`.
- `src/shape_cache.cpp` — LRU hash map (FNV-1a, open addressing, 4096 slots). `GlyphInfo` pool (262144 entries). Free-list reclamation on eviction. `shape_cache_lookup()`, `shape_cache_store()`, `shape_cache_invalidate_stale()`, `shape_visible_lines_budgeted()`.

### Modify
- `src/editor/editor_core.cpp` — `EditorState` gets `ShapeCache shape_cache`, `ShaperContext shaper`. Frame flow: invalidate stale cache on edit → shape visible lines (budgeted) → push glyph runs from cached shapes.
- `src/layout.cpp` — glyph positions come from shaped runs, not monospace grid
- `build.sh` — add `pkg-config --cflags --libs harfbuzz`

### Verify
```bash
# Text renders with proper kerning/positioning. Cache hit rate logged. No visual regression.
```

---

## Phase 6: Tree-sitter Syntax Highlighting (Async)

**Goal**: Background-thread incremental parsing using COW snapshots, double-buffered highlight state.

### External Dependencies
- Tree-sitter C library + language grammars (tree-sitter-c, tree-sitter-cpp)

### Create
- `src/atomics.cpp` — `atomic_load_u32`, `atomic_store_u32`, `atomic_exchange_u32`, `atomic_fetch_add_u64`, `atomic_compare_exchange_u32`, `atomic_fence()` via `__atomic_*` builtins. MSVC fallback via `_Interlocked*`.
- `src/highlight.cpp`:
  - `SyntaxKind` enum (normal, keyword, string, comment, number, operator, type, macro)
  - `SyntaxSpan { u32 byte_start; u32 byte_end; SyntaxKind kind; }`
  - `HighlightState { SyntaxSpan *spans; u32 span_count; u64 rope_version; }`
  - `HighlightChannel` — double-buffered front/back, atomic pending flag
  - `EditorBuffer` gains `TSTree*`, `TSParser*`, `TextSnapshot last_parse_snapshot`
  - `TSInput` callback wraps `text_snapshot_read` from Phase 2e (tree-descent iterator over immutable snapshot — no flattening needed)
  - `highlight_thread_proc()` — background OS thread (via `base/threads/`). Main thread takes COW snapshot + records `TSInputEdit` (start/old_end/new_end byte+point), enqueues via `ThreadMutex`/`ThreadConditionVariable`. Parser thread calls `ts_tree_edit` then `ts_parser_parse` against snapshot. Posts `TSTree*` + spans back.
  - `highlight_consume()` — main thread picks up new tree next frame if ready, swaps front/back highlight state, never blocks
- `src/editor/editor_core.cpp` — color theme table (`V4 syntax_colors[syntax_kind_count]`), `push_line_commands()` segments glyph runs by syntax span color. Query highlights for visible line range (Phase 2b iterator provides byte range), map TSNode ranges to highlight tokens.

### Modify
- `src/editor/editor_core.cpp` — on rope edit: take snapshot + enqueue parse. Frame start: `highlight_consume()`. Dirty gate checks `highlight.pending`.
- `build.sh` — add `-ltree-sitter`, grammar linking

### Verify
```bash
./bin/main src/editor/editor_core.cpp
# Keywords blue, strings green, comments gray. Edit text, colors update within 1-2 frames.
# No frame stalls — parsing is fully async.
```

---

## Phase 7: Worker Pool for Burst Shaping

**Goal**: Semaphore-gated worker pool for parallel shaping on file open / large paste.

### Create
- `src/worker_pool.cpp`:
  - `WorkerPool` struct: `os_semaphore`, `WorkItem`, `completion_count` (atomic), per-worker scratch arenas, per-worker `ShaperContext`
  - `worker_pool_init()`, `worker_pool_dispatch()`, `worker_pool_destroy()`
  - `choose_worker_count()`: `min(physical_cores / 2, 4)`
  - Workers sleep on semaphore, zero CPU when idle

### Modify
- `src/editor/editor_core.cpp` — `EditorState` gets `WorkerPool *worker_pool`, `bool needs_bulk_shape`. On file open / large paste: set `needs_bulk_shape = 1`. In frame: if `needs_bulk_shape`, dispatch `bulk_shape_worker` to pool, else use budgeted single-threaded shaping.
- `src/app/editor_main.cpp` — init worker pool at startup, destroy at shutdown

### Verify
```bash
# Open a large file (10K+ lines). First render completes quickly (workers shape in parallel).
# During normal editing, workers never wake (LOG_DEBUG confirms).
```

---

## Phase 8: Performance Tiers + Polish

**Goal**: GPU-based tier selection, swapchain present mode optimization, final polish.

### Create/Modify
- `src/editor/editor_core.cpp` / `src/render/vulkan.cpp`:
  - `PerformanceTier` struct: `target_fps`, `present_mode`, `swapchain_image_count`
  - `choose_performance_tier()` — query VRAM, check mailbox support
  - Tier: 240fps (MAILBOX, 3 images) / 144fps (MAILBOX, 3) / 60fps (FIFO, 2)
  - Dirty-state gate sleep duration = `1.0 / target_fps`
- Swapchain recreation with correct present mode per tier
- Frame budget logging (debug builds)

### Verify
```bash
# Battery tier: 60fps, FIFO, near-zero idle CPU
# High-end: 240fps with mailbox, smooth scrolling
```

---

## Files Summary

### Keep as-is
| File | Purpose |
|---|---|
| `src/base/core.h` | Compiler/OS detection, assert, bit ops + DLL/SLL macros (added Ph0) |
| `src/base/memory.h` | Platform virtual memory (mmap/VirtualAlloc) |
| `src/base/arena.h` | Raddebugger chained-block arena (rewritten Ph0) |
| `src/base/log.h` | 6-level logging |
| `src/base/string.h` | Non-owning String, arena ops (push_array param order updated Ph0) |
| `src/base/threads/` | Thread, Mutex, CondVar (POSIX + Win32) |

### Delete
| File | Reason |
|---|---|
| `src/game/game.cpp` | Game logic, replaced by editor |
| `src/game/game_math.h` | Merged into base/types.h |
| `src/game/game_render_group.h` | Replaced by push_cmds.cpp |
| `src/game_api.h` | Game interface, no longer needed |
| `src/base/lane.h` | Lane threading removed per spec |
| `src/base/typedef.h` | Merged into base/types.h |

### Create (new files)
| File | Phase |
|---|---|
| `src/base/types.h` | 0 |
| `src/draw/draw_core.cpp` | 0 |
| `src/editor/editor_input.h` | 1 |
| `src/editor/editor_core.cpp` | 1 |
| `src/text/text_buffer.h` | 2a-2e |
| `src/font.cpp` | 3 |
| `src/layout.cpp` | 3 |
| `assets/shaders/glyph.vert` | 3 |
| `assets/shaders/glyph.frag` | 3 |
| `src/file.cpp` | 4 |
| `src/shaper.cpp` | 5 |
| `src/shape_cache.cpp` | 5 |
| `src/highlight.cpp` | 6 |
| `src/atomics.cpp` | 6 |
| `src/worker_pool.cpp` | 7 |

### Heavily modify
| File | Phases |
|---|---|
| `src/app/editor_main.cpp` | 0, 1, 4, 6, 7 |
| `src/render/vulkan.h` | 0, 3 |
| `src/render/vulkan.cpp` | 0, 3, 8 |
| `build.sh` | 0, 3, 5, 6 |
| `build.bat` | 0 |

### Create (build)
| File | Phase |
|---|---|
| `build.bat` | 0 |

---

## Unresolved Questions

None — all resolved.

## Dependency Notes

Phases 2b (iterator) and 2c (rebalancing) are independent — can be done in either order or parallel. Phase 2d (anchors) depends on neither but logically follows. Phase 2e (COW) depends on 2b (snapshot iterators reuse iterator logic). Phase 4 (multi-buffer + undo) depends on 2d+2e. Phase 6 (Tree-sitter) depends on 2e (COW snapshots) + 4 (multi-buffer for per-buffer TSTree).
