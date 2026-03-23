#pragma once

#include <cstring>
#include "base/arena.h"
#include "base/string.h"

// ---- constants ----

#define TEXT_CHUNK_MAX                                                         \
    128 // max bytes per chunk (fits ~128 ASCII / ~42 CJK chars)
#define TEXT_TREE_BASE 6
#define TEXT_TREE_CAP (TEXT_TREE_BASE * 2) // max items per node/leaf
#define TEXT_MAX_DEPTH 24 // max tree height (log_6(file_bytes) ceiling)

// ---- summary (the monoid that aggregates up the tree) ----

struct TextSummary {
    u64 bytes;
    u64 lines; // newline count
    u64 utf16_units;
};

internal TextSummary text_summary_add(TextSummary a, TextSummary b) {
    return {
        a.bytes + b.bytes,
        a.lines + b.lines,
        a.utf16_units + b.utf16_units
    };
}

internal TextSummary text_summary_from_bytes(u8 const* data, u64 len) {
    TextSummary s = {};
    s.bytes = len;
    for(u64 i = 0; i < len; ++i) {
        u8 b = data[i];
        if(b == '\n')
            ++s.lines;
        // Count UTF-16 units: codepoints >= U+10000 need 2 units (4-byte UTF-8
        // start: 0xF0-0xFF)
        if((b & 0xC0) != 0x80) { // not a continuation byte → start of codepoint
            ++s.utf16_units;
            if(b >= 0xF0)
                ++s.utf16_units; // supplementary plane
        }
    }
    return s;
}

// ---- chunk ----

struct TextChunk {
    u8 text[TEXT_CHUNK_MAX];
    u16 len;
    TextSummary summary;
};

// ---- leaf node (holds chunks, doubly-linked for sequential scan) ----

struct TextLeaf {
    TextLeaf* prev;
    TextLeaf* next;
    u16 count;
    TextSummary summary;
    TextChunk chunks[TEXT_TREE_CAP];
};

// ---- internal node ----

struct TextNode {
    TextNode* next; // used only when node is in free list
    u16 count;
    u8 height; // 1 = children are leaves, >1 = nodes
    TextSummary summary;
    TextSummary child_summaries[TEXT_TREE_CAP];
    union {
        TextNode* nodes[TEXT_TREE_CAP];
        TextLeaf* leaves[TEXT_TREE_CAP];
    };
};

// ---- document ----

struct TextPoint {
    u64 line;
    u64 col;
}; // 0-based

struct TextDocument {
    Arena* arena;
    TextNode* root; // null when doc fits in one leaf
    TextLeaf* first_leaf;
    TextLeaf* last_leaf;
    TextSummary total;
    // Free list recycling (raddebugger pattern)
    TextNode* free_nodes;
    TextLeaf* free_leaves;
};

// ---- forward declarations ----

struct TextNodeSplit {
    bool did_split;
    TextNode* right;
    TextSummary right_summary;
};
struct TextLeafSplit {
    bool did_split;
    TextLeaf* right;
    TextSummary right_summary;
};

// ---- internal helpers ----

internal TextLeaf* text_alloc_leaf(TextDocument* doc) {
    TextLeaf* leaf = doc->free_leaves;
    if(leaf) {
        SLLStackPop(doc->free_leaves);
        memset(leaf, 0, sizeof(*leaf));
    } else {
        leaf = push_struct(doc->arena, TextLeaf);
    }
    return leaf;
}

internal TextNode* text_alloc_node(TextDocument* doc) {
    TextNode* node = doc->free_nodes;
    if(node) {
        SLLStackPop(doc->free_nodes);
        memset(node, 0, sizeof(*node));
    } else {
        node = push_struct(doc->arena, TextNode);
    }
    return node;
}

internal void text_free_leaf(TextDocument* doc, TextLeaf* leaf) {
    SLLStackPush(doc->free_leaves, leaf);
}

internal void text_free_node(TextDocument* doc, TextNode* node) {
    SLLStackPush(doc->free_nodes, node);
}

// Find the last valid UTF-8 codepoint start at or before pos in buf.
internal u16 utf8_boundary_at_or_before(u8 const* buf, u16 pos) {
    while(pos > 0 && (buf[pos] & 0xC0) == 0x80)
        --pos;
    return pos;
}

// Recompute leaf summary from its chunks.
internal void leaf_recompute_summary(TextLeaf* leaf) {
    leaf->summary = {};
    for(int i = 0; i < leaf->count; ++i) {
        leaf->summary =
            text_summary_add(leaf->summary, leaf->chunks[i].summary);
    }
}

// Recompute node summary from child_summaries.
internal void node_recompute_summary(TextNode* node) {
    node->summary = {};
    for(int i = 0; i < node->count; ++i) {
        node->summary =
            text_summary_add(node->summary, node->child_summaries[i]);
    }
}

// Recursively recompute all node summaries from leaves up.
internal void node_recompute_all(TextNode* node) {
    if(node->height > 1) {
        for(int i = 0; i < node->count; ++i)
            node_recompute_all(node->nodes[i]);
    }
    node_recompute_summary(node);
}

// ---- leaf insert ----
// Inserts at most TEXT_CHUNK_MAX bytes at local_offset within leaf.
// Returns a TextLeafSplit if the leaf needed to split.
internal TextLeafSplit leaf_insert_small(
    TextDocument* doc,
    TextLeaf* leaf,
    u64 local_offset, // byte offset within this leaf
    u8 const* bytes,
    u16 len // len <= TEXT_CHUNK_MAX
) {
    // Flatten leaf content + new bytes into a stack buffer
    u64 old_size = leaf->summary.bytes;
    u64 new_size = old_size + len;
    // max new_size: TEXT_TREE_CAP * TEXT_CHUNK_MAX + TEXT_CHUNK_MAX = 13 * 128
    // = 1664
    u8 buf[TEXT_TREE_CAP * TEXT_CHUNK_MAX + TEXT_CHUNK_MAX];

    // Copy leaf content into buf
    u64 written = 0;
    for(int i = 0; i < leaf->count; ++i) {
        memcpy(buf + written, leaf->chunks[i].text, leaf->chunks[i].len);
        written += leaf->chunks[i].len;
    }

    // Insert new bytes at local_offset
    memmove(
        buf + local_offset + len,
        buf + local_offset,
        old_size - local_offset
    );
    memcpy(buf + local_offset, bytes, len);

    // Redistribute buf into chunks in leaf (and possibly a right leaf)
    leaf->count = 0;
    leaf->summary = {};

    TextLeafSplit split = {};
    TextLeaf* right_leaf = nullptr;
    u64 remaining = new_size;
    u8* p = buf;

    while(remaining > 0) {
        TextLeaf* target =
            (right_leaf && leaf->count == TEXT_TREE_CAP) ? right_leaf : leaf;

        if(target->count == TEXT_TREE_CAP) {
            // Both leaves full — need a third? Shouldn't happen with len ≤
            // TEXT_CHUNK_MAX. But guard anyway: push remainder into last chunk
            // (extend it).
            TextChunk* last = &target->chunks[target->count - 1];
            u64 space = TEXT_CHUNK_MAX - last->len;
            u64 copy = remaining < space ? remaining : space;
            memcpy(last->text + last->len, p, copy);
            last->len += (u16)copy;
            last->summary = text_summary_from_bytes(last->text, last->len);
            p += copy;
            remaining -= copy;
            continue;
        }

        if(!right_leaf && target->count == TEXT_TREE_CAP) {
            right_leaf = text_alloc_leaf(doc);
            // Insert into linked list after leaf
            right_leaf->next = leaf->next;
            right_leaf->prev = leaf;
            if(leaf->next)
                leaf->next->prev = right_leaf;
            else
                doc->last_leaf = right_leaf;
            leaf->next = right_leaf;
            target = right_leaf;
        }

        TextChunk* chunk = &target->chunks[target->count++];
        u64 clen = remaining < TEXT_CHUNK_MAX ? remaining : TEXT_CHUNK_MAX;
        // Trim to UTF-8 boundary if not the final segment
        if(remaining > TEXT_CHUNK_MAX) {
            u16 boundary = utf8_boundary_at_or_before(p, (u16)clen);
            if(boundary > 0)
                clen = boundary;
        }
        chunk->len = (u16)clen;
        memcpy(chunk->text, p, clen);
        chunk->summary = text_summary_from_bytes(p, clen);
        target->summary = text_summary_add(target->summary, chunk->summary);
        p += clen;
        remaining -= clen;
    }

    if(right_leaf) {
        split.did_split = true;
        split.right = right_leaf;
        split.right_summary = right_leaf->summary;
    }
    return split;
}

// ---- node insert ----

internal TextNodeSplit node_insert_small(
    TextDocument* doc,
    TextNode* node,
    u64 offset,
    u8 const* bytes,
    u16 len
);

internal TextNodeSplit
node_split_overflowed(TextDocument* doc, TextNode* node) {
    // Split node: right half goes into a new node
    TextNode* right_node = text_alloc_node(doc);
    right_node->height = node->height;

    int left_count = node->count / 2;
    int right_count = node->count - left_count;
    right_node->count = (u16)right_count;

    for(int i = 0; i < right_count; ++i) {
        right_node->child_summaries[i] = node->child_summaries[left_count + i];
        if(node->height == 1)
            right_node->leaves[i] = node->leaves[left_count + i];
        else
            right_node->nodes[i] = node->nodes[left_count + i];
    }
    node->count = (u16)left_count;

    node_recompute_summary(node);
    node_recompute_summary(right_node);

    TextNodeSplit s;
    s.did_split = true;
    s.right = right_node;
    s.right_summary = right_node->summary;
    return s;
}

internal TextNodeSplit node_insert_small(
    TextDocument* doc,
    TextNode* node,
    u64 offset,
    u8 const* bytes,
    u16 len
) {
    // Find which child to descend into
    int child_idx = 0;
    u64 local_offset = offset;
    for(; child_idx < node->count - 1; ++child_idx) {
        if(local_offset < node->child_summaries[child_idx].bytes)
            break;
        local_offset -= node->child_summaries[child_idx].bytes;
    }

    bool did_child_split = false;
    TextNode* right_node = nullptr;
    TextLeaf* right_leaf = nullptr;
    TextSummary right_summary = {};

    if(node->height == 1) {
        TextLeafSplit s = leaf_insert_small(
            doc,
            node->leaves[child_idx],
            local_offset,
            bytes,
            len
        );
        node->child_summaries[child_idx] = node->leaves[child_idx]->summary;
        if(s.did_split) {
            did_child_split = true;
            right_leaf = s.right;
            right_summary = s.right_summary;
        }
    } else {
        TextNodeSplit s = node_insert_small(
            doc,
            node->nodes[child_idx],
            local_offset,
            bytes,
            len
        );
        node->child_summaries[child_idx] = node->nodes[child_idx]->summary;
        if(s.did_split) {
            did_child_split = true;
            right_node = s.right;
            right_summary = s.right_summary;
        }
    }
    node_recompute_summary(node);

    if(!did_child_split)
        return {false};

    // Insert right sibling at child_idx+1
    int insert_at = child_idx + 1;
    // Shift everything right
    for(int i = node->count; i > insert_at; --i) {
        node->child_summaries[i] = node->child_summaries[i - 1];
        if(node->height == 1)
            node->leaves[i] = node->leaves[i - 1];
        else
            node->nodes[i] = node->nodes[i - 1];
    }
    node->child_summaries[insert_at] = right_summary;
    if(node->height == 1)
        node->leaves[insert_at] = right_leaf;
    else
        node->nodes[insert_at] = right_node;
    node->count++;
    node_recompute_summary(node);

    if(node->count <= TEXT_TREE_CAP)
        return {false};

    // Node overflowed — split it
    return node_split_overflowed(doc, node);
}

// ---- public insert ----

internal void text_insert(
    TextDocument* doc,
    u64 byte_offset,
    u8 const* bytes,
    u64 len
) {
    assert(doc != nullptr, "doc must not be null");
    assert(byte_offset <= doc->total.bytes, "insert offset out of range");

    // Segment large inserts so each leaf operation is ≤ TEXT_CHUNK_MAX bytes.
    u64 cursor = byte_offset;
    u8 const* p = bytes;
    u64 remaining = len;

    while(remaining > 0) {
        u16 chunk =
            (u16)(remaining < TEXT_CHUNK_MAX ? remaining : TEXT_CHUNK_MAX);

        if(!doc->first_leaf) {
            // Empty document: create first leaf
            doc->first_leaf = doc->last_leaf = text_alloc_leaf(doc);
        }

        if(!doc->root) {
            // Single leaf
            TextLeafSplit s =
                leaf_insert_small(doc, doc->first_leaf, cursor, p, chunk);
            if(s.did_split) {
                // Promote to root node
                TextNode* root = text_alloc_node(doc);
                root->height = 1;
                root->count = 2;
                root->leaves[0] = doc->first_leaf;
                root->child_summaries[0] = doc->first_leaf->summary;
                root->leaves[1] = s.right;
                root->child_summaries[1] = s.right_summary;
                node_recompute_summary(root);
                doc->root = root;
            }
        } else {
            TextNodeSplit s =
                node_insert_small(doc, doc->root, cursor, p, chunk);
            if(s.did_split) {
                // Root split: new root one level higher
                TextNode* new_root = text_alloc_node(doc);
                new_root->height = doc->root->height + 1;
                new_root->count = 2;
                new_root->nodes[0] = doc->root;
                new_root->child_summaries[0] = doc->root->summary;
                new_root->nodes[1] = s.right;
                new_root->child_summaries[1] = s.right_summary;
                node_recompute_summary(new_root);
                doc->root = new_root;
            }
        }

        doc->total =
            text_summary_add(doc->total, text_summary_from_bytes(p, chunk));
        cursor += chunk;
        p += chunk;
        remaining -= chunk;
    }
}

// ---- delete helpers ----

// Remove child at index from node, update summaries.
// Returns true if node is now empty.
internal bool node_remove_child(TextNode* node, int idx) {
    for(int i = idx; i < node->count - 1; ++i) {
        node->child_summaries[i] = node->child_summaries[i + 1];
        if(node->height == 1)
            node->leaves[i] = node->leaves[i + 1];
        else
            node->nodes[i] = node->nodes[i + 1];
    }
    node->count--;
    node_recompute_summary(node);
    return node->count == 0;
}

// Descend to find and remove a specific leaf pointer from the tree.
// Returns true if node should be removed (empty).
internal bool node_remove_leaf(
    TextDocument* doc,
    TextNode* node,
    TextLeaf* leaf
) {
    if(node->height == 1) {
        for(int i = 0; i < node->count; ++i) {
            if(node->leaves[i] == leaf) {
                return node_remove_child(node, i);
            }
        }
        return false;
    }
    for(int i = 0; i < node->count; ++i) {
        if(node_remove_leaf(doc, node->nodes[i], leaf)) {
            text_free_node(doc, node->nodes[i]);
            return node_remove_child(node, i);
        }
    }
    return false;
}

// ---- find leaf by byte offset (tree descent) ----

struct TextLeafPos {
    TextLeaf* leaf;
    u64 leaf_start;
}; // leaf + offset of leaf's first byte

internal TextLeafPos text_find_leaf(TextDocument* doc, u64 byte_offset) {
    if(!doc->root) {
        return {doc->first_leaf, 0};
    }
    TextNode* node = doc->root;
    u64 accumulated = 0;
    while(node->height > 1) {
        int i = 0;
        for(; i < node->count - 1; ++i) {
            if(byte_offset - accumulated < node->child_summaries[i].bytes)
                break;
            accumulated += node->child_summaries[i].bytes;
        }
        node = node->nodes[i];
    }
    // node is now at height 1, children are leaves
    int i = 0;
    for(; i < node->count - 1; ++i) {
        if(byte_offset - accumulated < node->child_summaries[i].bytes)
            break;
        accumulated += node->child_summaries[i].bytes;
    }
    return {node->leaves[i], accumulated};
}

// ---- delete within one leaf ----
// Deletes [local_start, local_start+len) from the leaf.
// Returns true if leaf became empty.
internal bool leaf_delete_range(
    TextLeaf* leaf,
    u64 local_start,
    u64 local_len
) {
    // Flatten leaf content
    u64 old_size = leaf->summary.bytes;
    u8 buf[TEXT_TREE_CAP * TEXT_CHUNK_MAX];
    u64 written = 0;
    for(int i = 0; i < leaf->count; ++i) {
        memcpy(buf + written, leaf->chunks[i].text, leaf->chunks[i].len);
        written += leaf->chunks[i].len;
    }

    // Remove [local_start, local_start+local_len)
    u64 new_size = old_size - local_len;
    memmove(
        buf + local_start,
        buf + local_start + local_len,
        old_size - local_start - local_len
    );

    // Redistribute into chunks
    leaf->count = 0;
    leaf->summary = {};
    u64 rem = new_size;
    u8* p = buf;
    while(rem > 0) {
        u64 clen = rem < TEXT_CHUNK_MAX ? rem : TEXT_CHUNK_MAX;
        if(rem > TEXT_CHUNK_MAX) {
            u16 boundary = utf8_boundary_at_or_before(p, (u16)clen);
            if(boundary > 0)
                clen = boundary;
        }
        TextChunk* chunk = &leaf->chunks[leaf->count++];
        chunk->len = (u16)clen;
        memcpy(chunk->text, p, clen);
        chunk->summary = text_summary_from_bytes(p, clen);
        leaf->summary = text_summary_add(leaf->summary, chunk->summary);
        p += clen;
        rem -= clen;
    }
    return leaf->count == 0;
}

// ---- public delete ----

internal void text_delete(TextDocument* doc, u64 byte_offset, u64 len) {
    assert(doc != nullptr, "doc must not be null");
    assert(byte_offset + len <= doc->total.bytes, "delete out of range");
    if(len == 0)
        return;

    TextSummary deleted_summary = {};

    u64 remaining = len;
    while(remaining > 0) {
        TextLeafPos lp = text_find_leaf(doc, byte_offset);
        TextLeaf* leaf = lp.leaf;

        u64 local_start = byte_offset - lp.leaf_start;
        u64 leaf_avail = leaf->summary.bytes - local_start;
        u64 delete_now = remaining < leaf_avail ? remaining : leaf_avail;

        // Record what we're deleting for total summary update
        u8 tmp[TEXT_TREE_CAP * TEXT_CHUNK_MAX];
        u64 src = 0;
        for(int i = 0; i < leaf->count; ++i) {
            memcpy(tmp + src, leaf->chunks[i].text, leaf->chunks[i].len);
            src += leaf->chunks[i].len;
        }
        deleted_summary = text_summary_add(
            deleted_summary,
            text_summary_from_bytes(tmp + local_start, delete_now)
        );

        bool leaf_empty = leaf_delete_range(leaf, local_start, delete_now);

        if(leaf_empty) {
            // Remove from linked list
            if(leaf->prev)
                leaf->prev->next = leaf->next;
            else
                doc->first_leaf = leaf->next;
            if(leaf->next)
                leaf->next->prev = leaf->prev;
            else
                doc->last_leaf = leaf->prev;

            if(doc->root) {
                bool root_empty = node_remove_leaf(doc, doc->root, leaf);
                if(root_empty) {
                    text_free_node(doc, doc->root);
                    doc->root = nullptr;
                } else if(doc->root->count == 1 && doc->root->height > 1) {
                    // Collapse unnecessary root level
                    TextNode* old_root = doc->root;
                    doc->root = old_root->nodes[0];
                    text_free_node(doc, old_root);
                }
            }
            text_free_leaf(doc, leaf);
            // byte_offset stays the same, content shifted left
        } else {
            // Update node summaries for this leaf
            if(doc->root)
                node_recompute_all(doc->root);
            // If no root, the single-leaf total is already updated via
            // leaf->summary byte_offset stays the same (we deleted from this
            // position)
        }

        remaining -= delete_now;
    }

    doc->total.bytes -= deleted_summary.bytes;
    doc->total.lines -= deleted_summary.lines;
    doc->total.utf16_units -= deleted_summary.utf16_units;
}

// ---- create ----

internal TextDocument* text_document_create(Arena* arena, String content) {
    TextDocument* doc = push_struct(arena, TextDocument);
    doc->arena = arena;

    if(content.size == 0) {
        // Empty document still needs one leaf
        doc->first_leaf = doc->last_leaf = text_alloc_leaf(doc);
        return doc;
    }

    text_insert(doc, 0, content.str, content.size);
    return doc;
}

// ---- queries ----

internal u64 text_content_size(TextDocument* doc) {
    return doc->total.bytes;
}
internal u64 text_line_count(TextDocument* doc) {
    return doc->total.lines + 1;
}

// Convert logical byte offset → (line, col)
internal TextPoint text_offset_to_point(TextDocument* doc, u64 byte_offset) {
    byte_offset =
        byte_offset < doc->total.bytes ? byte_offset : doc->total.bytes;
    TextPoint pt = {};
    u64 accumulated = 0;

    // Walk leaves
    for(TextLeaf* leaf = doc->first_leaf; leaf; leaf = leaf->next) {
        if(accumulated + leaf->summary.bytes < byte_offset) {
            pt.line += leaf->summary.lines;
            accumulated += leaf->summary.bytes;
            continue;
        }
        // Within this leaf
        for(int i = 0; i < leaf->count; ++i) {
            TextChunk* c = &leaf->chunks[i];
            if(accumulated + c->len < byte_offset) {
                pt.line += c->summary.lines;
                accumulated += c->len;
                continue;
            }
            // Within this chunk
            for(u16 j = 0; j < c->len && accumulated < byte_offset;
                ++j, ++accumulated) {
                if(c->text[j] == '\n') {
                    ++pt.line;
                    pt.col = 0;
                } else {
                    ++pt.col;
                }
            }
            return pt;
        }
    }
    return pt;
}

// Convert (line, col) → logical byte offset
internal u64 text_point_to_offset(TextDocument* doc, u64 line, u64 col) {
    u64 cur_line = 0;
    u64 offset = 0;

    for(TextLeaf* leaf = doc->first_leaf; leaf; leaf = leaf->next) {
        if(cur_line + leaf->summary.lines < line) {
            cur_line += leaf->summary.lines;
            offset += leaf->summary.bytes;
            continue;
        }
        for(int i = 0; i < leaf->count; ++i) {
            TextChunk* c = &leaf->chunks[i];
            if(cur_line + c->summary.lines < line) {
                cur_line += c->summary.lines;
                offset += c->len;
                continue;
            }
            for(u16 j = 0; j < c->len; ++j) {
                if(cur_line == line && col == 0)
                    return offset;
                if(c->text[j] == '\n') {
                    ++cur_line;
                    col = col > 0 ? col - 1 : 0;
                } else if(cur_line == line) {
                    if(col == 0)
                        return offset;
                    --col;
                }
                ++offset;
            }
        }
    }
    return offset;
}

// Copy one line's content into scratch arena. Returns a String slice.
internal String
text_line_content(TextDocument* doc, u64 line_idx, Arena* scratch) {
    u64 start_offset = text_point_to_offset(doc, line_idx, 0);
    u64 end_offset = text_point_to_offset(doc, line_idx + 1, 0);
    // Remove trailing newline if present
    if(end_offset > start_offset)
        --end_offset;

    u64 size = end_offset - start_offset;
    if(size == 0)
        return {(u8 const*)"", 0};

    u8* buf = push_array(scratch, u8, size + 1);
    u64 written = 0;

    // Walk from start_offset, collect bytes
    u64 accumulated = 0;
    for(TextLeaf* leaf = doc->first_leaf; leaf && written < size;
        leaf = leaf->next) {
        if(accumulated + leaf->summary.bytes <= start_offset) {
            accumulated += leaf->summary.bytes;
            continue;
        }
        for(int i = 0; i < leaf->count && written < size; ++i) {
            TextChunk* c = &leaf->chunks[i];
            u64 chunk_start = accumulated;
            u64 chunk_end = accumulated + c->len;
            if(chunk_end <= start_offset) {
                accumulated += c->len;
                continue;
            }
            u64 copy_from =
                start_offset > chunk_start ? start_offset - chunk_start : 0;
            u64 copy_to = (start_offset + size) < chunk_end
                              ? (start_offset + size) - chunk_start
                              : c->len;
            u64 copy_len = copy_to - copy_from;
            memcpy(buf + written, c->text + copy_from, copy_len);
            written += copy_len;
            accumulated += c->len;
        }
    }
    buf[written] = 0;
    return {buf, written};
}
