const t = @import("test.zig");

const testFind     = t.testFind;
const testFindAll  = t.testFindAll;
const Match        = t.Match;
const Config       = t.Config;

// The branch under test is in search.zig start_set_pass:
//
//   const next_offset = f.start_set.find(u8, str[base..]) orelse {
//     if (m.matches(.{}, str, str.len, str.len, captures)) |_| {
//       return Match{.str = "", .loc = .init(str.len, str.len)};
//     } return null;
//   };
//
// When the start-set scan returns null and the machine matches the empty
// string, the code returns an empty match at str.len. But by leftmost-longest
// semantics, the match should be at the *current* base position, not str.len.
//
// To exercise this branch we need:
//   1. A nullable pattern (matches the empty string).
//   2. A non-trivial start-set (so start_set_pass is a viable strategy).
//   3. An input containing none of the start-set characters.

test "pzre start_set_pass nullable empty-match position" {
  // `a*` is nullable, start_set = {a}, "xxx" has no 'a'.
  // Iterator should produce empty matches at every position 0..str.len.
  try testFindAll("a*", "xxx", &[_]Match{
    .{ .str = "", .loc = .init(0, 0) },
    .{ .str = "", .loc = .init(1, 1) },
    .{ .str = "", .loc = .init(2, 2) },
    .{ .str = "", .loc = .init(3, 3) },
  }, .{});

  // `a?` - same shape with bounded repetition.
  try testFindAll("a?", "xxx", &[_]Match{
    .{ .str = "", .loc = .init(0, 0) },
    .{ .str = "", .loc = .init(1, 1) },
    .{ .str = "", .loc = .init(2, 2) },
    .{ .str = "", .loc = .init(3, 3) },
  }, .{});

  // Alternation with empty branch - semantically equivalent to `a?`.
  try testFindAll("a|", "xxx", &[_]Match{
    .{ .str = "", .loc = .init(0, 0) },
    .{ .str = "", .loc = .init(1, 1) },
    .{ .str = "", .loc = .init(2, 2) },
    .{ .str = "", .loc = .init(3, 3) },
  }, .{});

  // Multi-character start-set, still no start chars in input.
  try testFindAll("(a|b)?", "xyz", &[_]Match{
    .{ .str = "", .loc = .init(0, 0) },
    .{ .str = "", .loc = .init(1, 1) },
    .{ .str = "", .loc = .init(2, 2) },
    .{ .str = "", .loc = .init(3, 3) },
  }, .{});

  // Concatenation of nullable factors.
  try testFindAll("a*b*", "xxx", &[_]Match{
    .{ .str = "", .loc = .init(0, 0) },
    .{ .str = "", .loc = .init(1, 1) },
    .{ .str = "", .loc = .init(2, 2) },
    .{ .str = "", .loc = .init(3, 3) },
  }, .{});

  // Empty input - the str.len position is the only position and also the
  // current base, so the buggy and correct behavior coincide here. Included
  // as a sanity check that the branch's "match empty at str.len" intent is
  // preserved when it's actually the right answer.
  try testFindAll("a*", "", &[_]Match{
    .{ .str = "", .loc = .init(0, 0) },
  }, .{});
}

test "pzre start_set_pass direct find at non-zero start" {
  // testFind exercises the same branch via a single find() call with explicit
  // start_idx > 0. With the bug, find returns an empty match at str.len; the
  // correct behavior returns an empty match at start_idx.

  // start_idx = 1 on "xxx" - expect empty match at position 1, not 3.
  try testFind("a*", "xxx", 1, .{ .str = "", .loc = .init(1, 1) });
  try testFind("a*", "xxx", 2, .{ .str = "", .loc = .init(2, 2) });

  // start_idx = str.len - this is the case the buggy branch was actually
  // written for. Should still work.
  try testFind("a*", "xxx", 3, .{ .str = "", .loc = .init(3, 3) });

  // Same for alternation form.
  try testFind("a|", "xxx", 1, .{ .str = "", .loc = .init(1, 1) });

  // Multi-character start-set.
  try testFind("(a|b)?", "xyz", 1, .{ .str = "", .loc = .init(1, 1) });
  try testFind("(a|b)?", "xyz", 2, .{ .str = "", .loc = .init(2, 2) });
}

test "pzre start_set_pass forced strategy" {
  // Auto-dispatch may pick bi_directional_pass or another strategy for these
  // patterns, bypassing the bug. Forcing start_set_pass guarantees the buggy
  // branch is exercised regardless of dispatcher preferences.
  //
  // If start_set_pass cannot be applied to a pattern, compilation will fail
  // with FormulationImpossible - useful negative information either way.
  const config: Config = .{ .strategy = .start_set_pass };

  try testFindAll("a*", "xxx", &[_]Match{
    .{ .str = "", .loc = .init(0, 0) },
    .{ .str = "", .loc = .init(1, 1) },
    .{ .str = "", .loc = .init(2, 2) },
    .{ .str = "", .loc = .init(3, 3) },
  }, config);

  try testFindAll("(a|b)?", "xyz", &[_]Match{
    .{ .str = "", .loc = .init(0, 0) },
    .{ .str = "", .loc = .init(1, 1) },
    .{ .str = "", .loc = .init(2, 2) },
    .{ .str = "", .loc = .init(3, 3) },
  }, config);
}
