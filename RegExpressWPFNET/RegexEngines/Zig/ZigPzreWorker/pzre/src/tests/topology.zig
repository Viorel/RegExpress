const t = @import("test.zig");

const testMatchExact = t.testMatchExact;
const testMatches = t.testMatches;

test "Topology: Alternation Bypass (|)" {
  // Tests if normal characters correctly patch their alt_jump to skip sibling islands
  
  try testMatchExact("abc|def", "abc", true);
  try testMatchExact("abc|def", "def", true);
  
  try testMatchExact("a|b|c", "a", true);
  try testMatchExact("a|b|c", "b", true);
  try testMatchExact("a|b|c", "c", true);

  try testMatches("abc|def", "---abc---", true);
  try testMatches("abc|def", "---def---", true);
}

test "Topology: Loop Exit Collision (+)" {
  // Tests if the + split state correctly utilizes an explicit Jump state 
  // to avoid falling through into the next memory island.

  try testMatchExact("a+|b+", "aaa", true);
  try testMatchExact("a+|b+", "bbb", true);
  try testMatches("a+|b+", "---aaa---", true);
  try testMatches("a+|b+", "---bbb---", true);

  try testMatchExact("a+|b+|c+", "aaa", true);
  try testMatchExact("a+|b+|c+", "bbb", true);
  try testMatchExact("a+|b+|c+", "ccc", true);

  try testMatchExact("(aa)+|(bb)+", "aaaa", true);
  try testMatchExact("(aa)+|(bb)+", "bbbb", true);
}

test "Topology: Optional Bypass (?)" {
  // Tests the ? operator, which places a split at the START of the island.
  // It must successfully jump over its own content, and then the sibling island.

  // 1. Basic optional branching
  try testMatchExact("a?|b?", "a", true);
  try testMatchExact("a?|b?", "b", true);
  try testMatchExact("a?|b?", "", true);

  // 2. Cascading optionals colliding with alternations
  try testMatchExact("a?b?|c?d?", "ab", true);
  try testMatchExact("a?b?|c?d?", "cd", true);
  try testMatchExact("a?b?|c?d?", "b", true);
  try testMatchExact("a?b?|c?d?", "d", true);

  // 3. Unanchored optional bypass
  try testMatches("a?b?|c?d?", "---ab---", true);
  try testMatches("a?b?|c?d?", "---cd---", true);
}

test "Topology: Star Bypass (*)" {
  // Tests the * operator, which combines the front-split of ? with the back-loop of +.
  // This is a highly volatile state topology.

  try testMatchExact("a*|b*", "aaaa", true);
  try testMatchExact("a*|b*", "bbbb", true);
  try testMatchExact("a*|b*", "", true);

  try testMatchExact("(ab)*|c*", "ababab", true);
  try testMatchExact("(ab)*|c*", "ccc", true);

  try testMatches("a*|b*", "---aaaa---", true);
  try testMatches("a*|b*", "---bbbb---", true);
}

test "Topology: The Island Hopping Torture Test" {
  // Combines all volatile jump operators into dense memory layouts.
  // If the explicit Jump tags or alt_jump patching logic fails, these will segfault or return false.

  try testMatchExact("a*|b+|c?", "aaaa", true);
  try testMatchExact("a*|b+|c?", "bbbb", true);
  try testMatchExact("a*|b+|c?", "c", true);
  try testMatchExact("a*|b+|c?", "", true);

  try testMatchExact("a?b*c+|x?y*z+", "abbccc", true);
  try testMatchExact("a?b*c+|x?y*z+", "bccc", true);
  try testMatchExact("a?b*c+|x?y*z+", "c", true);
  try testMatchExact("a?b*c+|x?y*z+", "xyyzzz", true);
  try testMatchExact("a?b*c+|x?y*z+", "z", true);

  try testMatchExact("(a+|b+)+|(c+|d+)+", "abaabba", true);
  try testMatchExact("(a+|b+)+|(c+|d+)+", "cddccdc", true);

  try testMatches("(a?b*c+)|(x?y*z+)", "---abbccc---", true);
  try testMatches("(a?b*c+)|(x?y*z+)", "---xyyzzz---", true);
}
