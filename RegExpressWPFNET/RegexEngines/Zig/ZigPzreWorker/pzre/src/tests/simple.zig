const t = @import("test.zig");

const testMatchExact = t.testMatchExact;

test "pzre operations SIMPLER" {
  try testMatchExact("a", "a", true);
  try testMatchExact("a+", "aa", true);
  try testMatchExact("a?", "a", true);
  try testMatchExact("a*", "aa", true);
}
