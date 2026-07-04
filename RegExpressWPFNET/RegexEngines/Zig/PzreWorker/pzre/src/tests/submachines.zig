//! Submachine fragment-generation tests.
//!
//! When the engine compiles `(X){n}`, it must produce n structurally-identical
//! copies of X with distinct state IDs but preserved internal structure.  This
//! "cloning" is the most bug-prone operation in NFA construction.  The tests
//! here isolate specific failure modes:
//!
//!   1. Off-by-one in clone count                  — `(X){5}` produces 4 or 6 X's
//!   2. Broken back-edges within clones            — second clone's loop is dead
//!   3. State contamination across clones          — choice in clone 1 forces clone 2
//!   4. Epsilon leak across clone boundaries       — empty match short-circuits next clone
//!   5. Variable-length boundary confusion         — inner clone over-consumes
//!   6. Mixed-quantifier composition errors        — `((X){a}){b}` count wrong
//!
//! Tests are organized by what they isolate.  Each one verifies a single
//! property; a failure tells you which specific cloning behavior broke.

const t = @import("test.zig");
const pzre = @import("../root.zig");
const Repeat = pzre.misc.Repeat;
const Config = pzre.compile.Config;

const testMatchExact = t.testMatchExact;
const testMatchExactWithConfig = t.testMatchExactWithConfig;
const testMatches = t.testMatches;

// Larger gpa upper bound for complex composite patterns.
const cfg: Config = .{ .limits = .{ .gpa_upper_bound = 1 << 20 } };

// ═══════════════════════════════════════════════════════════════════════════
// EXACT-COUNT QUANTIFIERS — boundary precision
// ═══════════════════════════════════════════════════════════════════════════
//
// `(X){n}` must produce exactly n clones of X.  Tests probe at the minimum
// boundary, maximum boundary, and adjacent values to catch off-by-one bugs.

test "submachine: (X){n} requires exactly n repetitions of X" {
  // Two-char X, exact count 4 → input length must be exactly 8
  const p = "(ab){4}";
  try testMatchExact(p, "", false);
  try testMatchExact(p, "ab", false);          // 1
  try testMatchExact(p, "abab", false);        // 2
  try testMatchExact(p, "ababab", false);      // 3
  try testMatchExact(p, "abababab", true);     // 4 ✓
  try testMatchExact(p, "ababababab", false);  // 5
  try testMatchExact(p, "abababababab", false); // 6
}

test "submachine: (X){0} produces zero clones (epsilon)" {
  const p = "(ab){0}";
  try testMatchExact(p, "", true);
  try testMatchExact(p, "ab", false);
}

test "submachine: (X){1} produces exactly one clone" {
  // Verifies that the cloning logic doesn't accidentally produce 0 or 2 at n=1
  const p = "(ab){1}";
  try testMatchExact(p, "", false);
  try testMatchExact(p, "ab", true);
  try testMatchExact(p, "abab", false);
}

test "submachine: large exact counts maintain precision" {
  // Verify the cloning logic remains correct at non-trivial counts.
  // 20 clones of (xy) → exactly 40 chars
  const p = "(xy){20}";
  const ok = "xy" ** 20;
  const short = "xy" ** 19;
  const long = "xy" ** 21;
  try testMatchExactWithConfig(p, ok, true, cfg);
  try testMatchExactWithConfig(p, short, false, cfg);
  try testMatchExactWithConfig(p, long, false, cfg);
}

// ═══════════════════════════════════════════════════════════════════════════
// RANGE QUANTIFIERS — count varies within bounds
// ═══════════════════════════════════════════════════════════════════════════
//
// `(X){n,m}` must accept exactly the counts in [n,m] and reject outside.
// Tests the full range plus boundary outliers.

test "submachine: (X){n,m} accepts every count in the closed range" {
  const p = "(ab){2,5}";
  try testMatchExact(p, "ab", false);              // 1 — below n
  try testMatchExact(p, "abab", true);             // 2 — at n
  try testMatchExact(p, "ababab", true);           // 3
  try testMatchExact(p, "abababab", true);         // 4
  try testMatchExact(p, "ababababab", true);       // 5 — at m
  try testMatchExact(p, "abababababab", false);    // 6 — above m
}

test "submachine: (X){n,m} where n=m equals (X){n}" {
  const p = "(ab){3,3}";
  try testMatchExact(p, "abab", false);
  try testMatchExact(p, "ababab", true);
  try testMatchExact(p, "abababab", false);
}

test "submachine: (X){0,m} permits zero or more up to m" {
  const p = "(ab){0,3}";
  try testMatchExact(p, "", true);
  try testMatchExact(p, "ab", true);
  try testMatchExact(p, "abab", true);
  try testMatchExact(p, "ababab", true);
  try testMatchExact(p, "abababab", false);
}

// ═══════════════════════════════════════════════════════════════════════════
// OPEN-ENDED QUANTIFIERS
// ═══════════════════════════════════════════════════════════════════════════

test "submachine: (X){n,} accepts n or more (no upper bound)" {
  const p = "(ab){3,}";
  try testMatchExact(p, "abab", false);          // 2
  try testMatchExact(p, "ababab", true);         // 3
  try testMatchExact(p, "abababab", true);       // 4
  try testMatchExactWithConfig(p, "ab" ** 20, true, cfg); // 20
}

test "submachine: (X)* matches zero or more, including empty" {
  const p = "(ab)*";
  try testMatchExact(p, "", true);
  try testMatchExact(p, "ab", true);
  try testMatchExact(p, "abab", true);
  try testMatchExactWithConfig(p, "ab" ** 30, true, cfg);
}

test "submachine: (X)+ matches one or more, excluding empty" {
  const p = "(ab)+";
  try testMatchExact(p, "", false);
  try testMatchExact(p, "ab", true);
  try testMatchExact(p, "abab", true);
}

test "submachine: (X)? matches zero or one" {
  const p = "(ab)?";
  try testMatchExact(p, "", true);
  try testMatchExact(p, "ab", true);
  try testMatchExact(p, "abab", false);
}

// ═══════════════════════════════════════════════════════════════════════════
// CROSS-CLONE INDEPENDENCE
// ═══════════════════════════════════════════════════════════════════════════
//
// Each clone must be independent: choices in one clone (which alternation
// branch to take, how many times to repeat an inner quantifier) must not
// constrain other clones.  Bugs here usually manifest as "first match
// determines the rest" behavior.

test "submachine: (A|B){n} permits arbitrary mixing of branches" {
  const p = "(cat|dog){3}";
  // Every permutation of 3 picks from {cat, dog}
  try testMatchExact(p, "catcatcat", true);
  try testMatchExact(p, "catcatdog", true);
  try testMatchExact(p, "catdogcat", true);
  try testMatchExact(p, "dogcatcat", true);
  try testMatchExact(p, "catdogdog", true);
  try testMatchExact(p, "dogcatdog", true);
  try testMatchExact(p, "dogdogcat", true);
  try testMatchExact(p, "dogdogdog", true);

  // Wrong counts still fail
  try testMatchExact(p, "catcat", false);
  try testMatchExact(p, "catcatcatcat", false);
}

test "submachine: (A|B|C){n} permits any branch at any position" {
  const p = "(red|green|blue){2}";
  // 3^2 = 9 combinations
  try testMatchExact(p, "redred", true);
  try testMatchExact(p, "redgreen", true);
  try testMatchExact(p, "redblue", true);
  try testMatchExact(p, "greenred", true);
  try testMatchExact(p, "greengreen", true);
  try testMatchExact(p, "greenblue", true);
  try testMatchExact(p, "bluered", true);
  try testMatchExact(p, "bluegreen", true);
  try testMatchExact(p, "blueblue", true);
}

test "submachine: clones don't constrain each other (variable-length branches)" {
  // Different branches consume different lengths.  Clone 1 picking "ab" must
  // not force clone 2 to also pick the 2-char branch.
  const p = "(ab|c){2}";
  try testMatchExact(p, "abab", true);
  try testMatchExact(p, "abc", true);
  try testMatchExact(p, "cab", true);
  try testMatchExact(p, "cc", true);
}

test "submachine: nested quantifier choice differs per clone" {
  // Inner a* can match 0..n a's; each outer clone should independently
  // pick how many a's to consume.
  const p = "(a*b){3}";

  // Test non bidirectional_pass first
  const start_set = Config{.strategy = .start_set_pass};

  try testMatchExactWithConfig(p, "bbb", true, start_set);
  try testMatchExactWithConfig(p, "ababab", true, start_set);
  try testMatchExactWithConfig(p, "aabaabaab", true, start_set);
  try testMatchExactWithConfig(p, "abaabb", true, start_set);
  try testMatchExactWithConfig(p, "aaabbab", true, start_set);
 
  // Test reversed right anchor
  const rev = Config{.strategy = .end_anchor_reverse_pass};
 
  try testMatchExactWithConfig(p, "bbb", true, rev);
  try testMatchExactWithConfig(p, "ababab", true, rev);
  try testMatchExactWithConfig(p, "aabaabaab", true, rev);
  try testMatchExactWithConfig(p, "abaabb", true, rev);
  try testMatchExactWithConfig(p, "aaabbab", true, rev);
 
  try testMatchExact(p, "bbb", true);            // each clone: 0 a's
  try testMatchExact(p, "ababab", true);         // each clone: 1 a
  try testMatchExact(p, "aabaabaab", true);      // each clone: 2 a's
  try testMatchExact(p, "abaabb", true);         // mixed: 1, 2, 0
  try testMatchExact(p, "aaabbab", true);         // mixed: 3, 1, 0
 
  // Anything with 2 is fucked
  try testMatchExact(p, "abab", false);
  try testMatchExact(p, "aaaabaaaaab", false);
  try testMatchExact(p, "bb", false);
  try testMatchExact(p, "aabab", false);
  try testMatchExact(p, "aaaabaab", false);
  try testMatchExact(p, "abaab", false);
  try testMatchExact(p, "aaabab", false);
}

// ═══════════════════════════════════════════════════════════════════════════
// EPSILON CLONES — submachines that can match empty
// ═══════════════════════════════════════════════════════════════════════════
//
// When the submachine can match empty (via *, ?, {0,n}, or empty union side),
// the epsilon path must NOT leak across clone boundaries.

test "submachine: (X?){n} permits each clone to independently match empty" {
  const p = "(a?){4}";
  try testMatchExact(p, "", true);       // all 4 epsilon
  try testMatchExact(p, "a", true);      // 1 a, 3 epsilon
  try testMatchExact(p, "aa", true);
  try testMatchExact(p, "aaa", true);
  try testMatchExact(p, "aaaa", true);   // all 4 a's
  try testMatchExact(p, "aaaaa", false); // 5 a's, only 4 clones
}

test "submachine: (X|){n} (union with epsilon) — each clone choosable" {
  const p = "(ab|){3}";
  try testMatchExact(p, "", true);             // all epsilon
  try testMatchExact(p, "ab", true);           // 1 ab + 2 epsilon
  try testMatchExact(p, "abab", true);         // 2 ab + 1 epsilon
  try testMatchExact(p, "ababab", true);       // 3 ab
  try testMatchExact(p, "abababab", false);    // 4 ab — too many
}

test "submachine: (X*){n} where X is variable-length" {
  // Each clone is a* (zero or more a's).  Total a's is unbounded.
  const p = "(a*){3}";
  try testMatchExact(p, "", true);
  try testMatchExact(p, "a", true);
  try testMatchExactWithConfig(p, "a" ** 50, true, cfg);
  try testMatchExact(p, "b", false);
}

test "submachine: clones with mixed epsilon-capable branches" {
  // Each clone picks either "a" or epsilon.  Length determines distribution.
  const p = "(a|){5}";
  try testMatchExact(p, "", true);
  try testMatchExact(p, "a", true);
  try testMatchExact(p, "aa", true);
  try testMatchExact(p, "aaa", true);
  try testMatchExact(p, "aaaa", true);
  try testMatchExact(p, "aaaaa", true);
  try testMatchExact(p, "aaaaaa", false); // 6 > 5 clones
}

// ═══════════════════════════════════════════════════════════════════════════
// VARIABLE-LENGTH SUBMACHINES UNDER FIXED QUANTIFIERS
// ═══════════════════════════════════════════════════════════════════════════
//
// `(X){n,m}` where X itself is variable-length is the case most likely to
// expose boundary-confusion bugs.  The inner submachine's "end" must be
// correctly determined for each outer clone.

test "submachine: variable-length inner under bounded outer" {
  // a+b — at least one a then b.  Outer clones repeat this.
  const p = "(a+b){3}";
  try testMatchExact(p, "ababab", true);            // each clone: ab
  try testMatchExact(p, "aabaabaab", true);         // each clone: aab
  try testMatchExact(p, "aaabaabab", true);         // mixed: aaab, aab, ab
  try testMatchExact(p, "abababab", false);         // 4 ab's — too many
  try testMatchExact(p, "abab", false);             // 2 — too few
}

test "submachine: variable inner with greedy quantifier respects clone boundaries" {
  // a+ is greedy, but must not consume across the clone boundary if doing
  // so would prevent the next clone from matching.
  const p = "(a+b){2}";
  try testMatchExact(p, "aaabaab", true);           // a+ in clone 1 takes 3, clone 2 takes 2
  try testMatchExact(p, "abaaaaab", true);          // clone 1: 1 a, clone 2: 5 a
  try testMatchExact(p, "aab", false);              // only 1 clone present
  try testMatchExact(p, "aaaa", false);             // no b's at all
}

test "submachine: nested variable inside fixed outer" {
  // Two layers of variable-length under fixed outer count.
  const p = "((a*)b{1,2}){3}";
  try testMatchExact(p, "bbb", true);                       // 0 a's, 1 b each
  try testMatchExact(p, "bbbbbb", true);                    // 0 a's, 2 b each
  try testMatchExact(p, "abbabbabb", true);                 // 1 a, 2 b each
  try testMatchExact(p, "aaaabaabab", true);                // 4a 1b, 1a 1b, 1b — wait, 1+1=2 b's, third needs 1+ b
  try testMatchExact(p, "abab", false);                     // only 2 clones
}

// ═══════════════════════════════════════════════════════════════════════════
// NESTED QUANTIFIERS — outer × inner cloning composition
// ═══════════════════════════════════════════════════════════════════════════
//
// `((X){a}){b}` produces a*b total copies of X.  The composition must
// preserve correct count and structure.

test "submachine: ((X){a}){b} produces exactly a×b total copies" {
  // Inner: (ab){2} = abab (4 chars).  Outer: ({...}){3} = abab × 3 = ababababababab (12 chars)
  const p = "((ab){2}){3}";
  try testMatchExact(p, "abab" ** 3, true);          // 3 outer × 2 inner = 6 ab = 12 chars
  try testMatchExact(p, "abab" ** 2, false);         // only 4 ab
  try testMatchExact(p, "abab" ** 4, false);         // too many
  try testMatchExact(p, "ab" ** 6, true);            // same as above, different decomp
  try testMatchExact(p, "ab" ** 5, false);
  try testMatchExact(p, "ab" ** 7, false);
}

test "submachine: nested ranges multiply correctly at boundaries" {
  // Outer 2..3, inner 2..3 → total ab count is in [4, 9]
  const p = "((ab){2,3}){2,3}";
  try testMatchExactWithConfig(p, "ab" ** 4, true, cfg);
  try testMatchExactWithConfig(p, "ab" ** 5, true, cfg);
  try testMatchExactWithConfig(p, "ab" ** 6, true, cfg);
  try testMatchExactWithConfig(p, "ab" ** 7, true, cfg);
  try testMatchExactWithConfig(p, "ab" ** 8, true, cfg);
  try testMatchExactWithConfig(p, "ab" ** 9, true, cfg);
  try testMatchExactWithConfig(p, "ab" ** 3, false, cfg);    // below 4
  try testMatchExactWithConfig(p, "ab" ** 10, false, cfg);   // above 9
}

test "submachine: deeply nested quantifiers compose correctly" {
  // Three levels: (((ab){2}){2}){2} → 2×2×2 = 8 ab's
  const p = "(((ab){2}){2}){2}";
  try testMatchExactWithConfig(p, "ab" ** 8, true, cfg);
  try testMatchExactWithConfig(p, "ab" ** 7, false, cfg);
  try testMatchExactWithConfig(p, "ab" ** 9, false, cfg);
}

test "submachine: inner * inside outer fixed expands unbounded total" {
  // Inner can match 0 or more; outer fixed = 3 clones.
  // Total length unbounded but must decompose into 3 segments.
  const p = "((ab)*){3}";
  try testMatchExact(p, "", true);
  try testMatchExact(p, "ab", true);
  try testMatchExact(p, "ababab", true);
  try testMatchExactWithConfig(p, "ab" ** 30, true, cfg);
}

test "submachine: inner fixed inside outer * — unbounded total" {
  // Outer * over inner {2} → total ab count must be multiple of 2.
  const p = "((ab){2})*";
  try testMatchExact(p, "", true);
  try testMatchExact(p, "ab", false);           // not multiple of 2
  try testMatchExact(p, "abab", true);          // 1 outer × 2
  try testMatchExact(p, "ababab", false);       // 3 ab's, not multiple of 2
  try testMatchExact(p, "abababab", true);      // 2 outer × 2
  try testMatchExactWithConfig(p, "ab" ** 10, true, cfg);  // 5 outer × 2
  try testMatchExactWithConfig(p, "ab" ** 11, false, cfg); // odd count
}

// ═══════════════════════════════════════════════════════════════════════════
// UNION SUBMACHINES UNDER QUANTIFIERS
// ═══════════════════════════════════════════════════════════════════════════

test "submachine: (A|B|C){n,m} accepts any branch at any position" {
  const p = "(cat|dog|fox){2,3}";
  // All 2-pick combinations
  try testMatchExact(p, "catdog", true);
  try testMatchExact(p, "dogfox", true);
  try testMatchExact(p, "foxcat", true);
  try testMatchExact(p, "catcat", true);
  // All 3-pick combinations sampled
  try testMatchExact(p, "catdogfox", true);
  try testMatchExact(p, "foxdogcat", true);
  try testMatchExact(p, "catcatcat", true);
  // Outside bounds
  try testMatchExact(p, "cat", false);          // 1 — below 2
  try testMatchExact(p, "catdogfoxcat", false); // 4 — above 3
}

test "submachine: union with shared-prefix branches" {
  // "ab" and "abc" share a prefix; both must be selectable per clone.
  const p = "(ab|abc){2}";
  try testMatchExact(p, "abab", true);
  try testMatchExact(p, "ababc", true);
  try testMatchExact(p, "abcab", true);
  try testMatchExact(p, "abcabc", true);
  try testMatchExact(p, "ab", false);
  try testMatchExact(p, "abc", false);
}

test "submachine: union with variable-length branches" {
  // Two branches: `a+b` (variable length 2+) and `c` (length 1).
  // Each clone independently picks a branch.
  const p = "(a+b|c){3}";
  try testMatchExact(p, "ccc", true);             // c, c, c
  try testMatchExact(p, "abcc", true);            // ab, c, c
  try testMatchExact(p, "cabc", true);            // c, ab, c
  try testMatchExact(p, "ccab", true);            // c, c, ab
  try testMatchExact(p, "ababab", true);          // ab, ab, ab
  try testMatchExact(p, "aabcc", true);           // aab, c, c
  try testMatchExact(p, "aabaabaab", true);       // aab, aab, aab
  try testMatchExact(p, "aabccc", false);         // 4 clones worth of content
  try testMatchExact(p, "cc", false);             // 2 clones
}

test "submachine: alternation under nested quantifier" {
  // Each outer clone picks a branch which itself has a quantifier.
  // Branches: `a+` (1+ a's) or `b{2}` (exactly 2 b's).
  const p = "((a+|b{2}))+";
  try testMatchExact(p, "a", true);              // 1 outer: a+
  try testMatchExact(p, "aaaa", true);           // 1 outer: a+ (all 4)
  try testMatchExact(p, "bb", true);             // 1 outer: b{2}
  try testMatchExact(p, "bbbb", true);           // 2 outer: bb + bb
  try testMatchExact(p, "abb", true);            // 2 outer: a + bb
  try testMatchExact(p, "bba", true);            // 2 outer: bb + a
  try testMatchExact(p, "abbabb", true);         // 4 outer: a, bb, a, bb
  try testMatchExact(p, "b", false);             // b{2} needs exactly 2 b's
  try testMatchExact(p, "bbb", false);           // bb + single b — single b matches neither branch
  try testMatchExact(p, "bbbb", true);           // bb + bb (correctly accepted above)
}

// Removed the now-redundant separate `(a+|b{2})+ rejects single trailing b` test
// since the bbb case is now in the test above.

// ═══════════════════════════════════════════════════════════════════════════
// CHARACTER CLASSES AND ASSERTIONS INSIDE QUANTIFIED SUBMACHINES
// ═══════════════════════════════════════════════════════════════════════════

test "submachine: character class inside quantified group" {
  // Each clone matches a single [abc].
  const p = "([abc]){3}";
  try testMatchExact(p, "abc", true);
  try testMatchExact(p, "cba", true);
  try testMatchExact(p, "aaa", true);
  try testMatchExact(p, "bcb", true);
  try testMatchExact(p, "abcd", false);
  try testMatchExact(p, "ab", false);
}

test "submachine: character class with quantifier inside quantifier" {
  // Each clone matches [0-9]+ (one or more digits).
  const p = "([0-9]+){3}";
  try testMatchExact(p, "123", true);             // 1, 2, 3 — each clone 1 digit
  try testMatchExact(p, "11223344", true);        // various decompositions exist
  try testMatchExact(p, "abc", false);
  try testMatchExact(p, "12", false);             // only 2 digits, need 3 clones
}

test "submachine: perl class inside quantified group" {
  // \d in three clones
  const p = "(\\d){4}";
  try testMatchExact(p, "1234", true);
  try testMatchExact(p, "0000", true);
  try testMatchExact(p, "123", false);
  try testMatchExact(p, "12345", false);
  try testMatchExact(p, "a234", false);
}

test "submachine: word-boundary assertion inside quantified group" {
  // Each clone matches a complete word.
  const p = "(\\b\\w+\\b ?){3}";
  try testMatchExact(p, "one two three", true);
  try testMatchExact(p, "aa bb cc", true);
  try testMatchExact(p, "one two", false);        // only 2 words
}

test "submachine: anchor inside quantified group" {
  // ^ inside a quantifier can only fire at position 0.  In subsequent
  // clones, ^ would not be at position 0 and must fail.
  // (^a) — first clone can match at position 0, second cannot.
  const p = "(^a){2}";
  try testMatchExact(p, "aa", false);  // second ^ can't be at position 0

  // But (^a|b){2} — second clone takes the 'b' branch.
  const p2 = "(^a|b){2}";
  try testMatchExact(p2, "ab", true);  // clone 1: ^a, clone 2: b
}

// ═══════════════════════════════════════════════════════════════════════════
// SUBMACHINE BOUNDARY PRECISION — variable-length adjacency
// ═══════════════════════════════════════════════════════════════════════════
//
// When variable-length submachines are adjacent, the boundary between them
// must be correctly determined.  Tests probe greediness across clone
// boundaries.

test "submachine: adjacent variable-length submachines find correct split" {
  // a+ must leave at least one a for the next a+.
  const p = "a+a+";
  try testMatchExact(p, "a", false);     // can't satisfy both
  try testMatchExact(p, "aa", true);     // 1 + 1
  try testMatchExact(p, "aaa", true);    // 2 + 1 or 1 + 2
  try testMatchExact(p, "aaaa", true);   // various
}

test "submachine: variable-length followed by exact" {
  // a* followed by a{2} — a* must leave 2 a's
  const p = "a*a{2}";
  try testMatchExact(p, "a", false);
  try testMatchExact(p, "aa", true);
  try testMatchExact(p, "aaa", true);
  try testMatchExact(p, "aaaa", true);
  try testMatchExactWithConfig(p, "a" ** 20, true, cfg);
}

test "submachine: quantified group followed by fixed prefix of same chars" {
  // (a*)b — greedy a* terminates at first b
  try testMatchExact("(a*)b", "b", true);
  try testMatchExact("(a*)b", "ab", true);
  try testMatchExact("(a*)b", "aaaaab", true);
  try testMatchExact("(a*)b", "ba", false);

  // (ab)+(ab) — last (ab) must be reserved
  const p = "(ab)+(ab)";
  try testMatchExact(p, "ab", false);          // 1 ab — can't split
  try testMatchExact(p, "abab", true);         // 1 outer + 1 final
  try testMatchExact(p, "ababab", true);       // 2 outer + 1 final
}

// ═══════════════════════════════════════════════════════════════════════════
// COMPOSITE PATTERNS — real-world-ish patterns combining all of the above
// ═══════════════════════════════════════════════════════════════════════════

test "submachine: IPv4-like pattern" {
  // Three "octet." then one "octet"; each octet is 1-3 digits
  const p = "([0-9]{1,3}\\.){3}[0-9]{1,3}";
  try testMatchExact(p, "1.2.3.4", true);
  try testMatchExact(p, "192.168.0.1", true);
  try testMatchExact(p, "255.255.255.255", true);
  try testMatchExact(p, "0.0.0.0", true);
  try testMatchExact(p, "1.2.3", false);              // only 3 segments
  try testMatchExact(p, "1.2.3.4.5", false);          // 5 segments
  try testMatchExact(p, "1234.1.1.1", false);         // octet too long
  try testMatchExact(p, "a.b.c.d", false);            // not digits
}

test "submachine: semver-like pattern" {
  // major.minor.patch with optional pre-release suffix
  const p = "[0-9]+\\.[0-9]+\\.[0-9]+(-[a-z]+(\\.[0-9]+)?)?";
  try testMatchExact(p, "1.0.0", true);
  try testMatchExact(p, "10.20.30", true);
  try testMatchExact(p, "0.1.2", true);
  try testMatchExact(p, "1.0.0-alpha", true);
  try testMatchExact(p, "1.0.0-beta.1", true);
  try testMatchExact(p, "1.0.0-rc.99", true);
  try testMatchExact(p, "1.0", false);                // missing patch
  try testMatchExact(p, "1.0.0-", false);             // empty pre-release
  try testMatchExact(p, "1.0.0-Alpha", false);        // case
}

test "submachine: nested phone-like pattern" {
  // (XXX) XXX-XXXX where each X is a digit
  const p = "\\([0-9]{3}\\) [0-9]{3}-[0-9]{4}";
  try testMatchExact(p, "(555) 123-4567", true);
  try testMatchExact(p, "(000) 000-0000", true);
  try testMatchExact(p, "555 123-4567", false);
  try testMatchExact(p, "(55) 123-4567", false);
  try testMatchExact(p, "(555)123-4567", false);
}

// ═══════════════════════════════════════════════════════════════════════════
// REGRESSION-PRESERVED COMPLEX PATTERNS
// ═══════════════════════════════════════════════════════════════════════════
//
// The original complex patterns from this file's previous version.  These are
// kept as integration tests: a single pattern exercises many submachine
// behaviors at once, which is harder to debug but provides broad coverage.

test "submachine integration: union bounds and isolation" {
  const p = "((cat)+|dog|mouse){2,3}|(man|woman)+|((left|and_woman){3,}|arg)+";

  try testMatchExactWithConfig(p, "dogdog", true, cfg);
  try testMatchExactWithConfig(p, "dogdogdog", true, cfg);
  try testMatchExactWithConfig(p, "catcat", true, cfg);
  try testMatchExactWithConfig(p, "catcatcatcatcatcat", true, cfg);
  try testMatchExactWithConfig(p, "mousecatcatdog", true, cfg);

  try testMatchExactWithConfig(p, "dog", false, cfg);
  try testMatchExactWithConfig(p, "mouse", false, cfg);
  try testMatchExactWithConfig(p, "dogdogdogdog", false, cfg);
  try testMatchExactWithConfig(p, "mousemousemousemouse", false, cfg);

  try testMatchExactWithConfig(p, "man", true, cfg);
  try testMatchExactWithConfig(p, "woman", true, cfg);
  try testMatchExactWithConfig(p, "manwomanman", true, cfg);

  try testMatchExactWithConfig(p, "", false, cfg);

  try testMatchExactWithConfig(p, "arg", true, cfg);
  try testMatchExactWithConfig(p, "argarg", true, cfg);
  try testMatchExactWithConfig(p, "leftleftleft", true, cfg);
  try testMatchExactWithConfig(p, "and_womanleftand_woman", true, cfg);
  try testMatchExactWithConfig(p, "leftleftleftarg", true, cfg);

  try testMatchExactWithConfig(p, "leftleft", false, cfg);
  try testMatchExactWithConfig(p, "and_womanleft", false, cfg);

  try testMatchExactWithConfig(p, "dogman", false, cfg);
  try testMatchExactWithConfig(p, "manarg", false, cfg);
  try testMatchExactWithConfig(p, "catleftleftleft", false, cfg);

  try testMatchExactWithConfig(p, "cat", false, cfg);
  try testMatchExactWithConfig(p, "manwomanmanx", false, cfg);
}

test "submachine integration: epsilon convergence and fallthrough" {
  const p = "x(a|b?|c*|(d+e?)+|f{0,2})y";

  try testMatchExactWithConfig(p, "xy", true, cfg);
  try testMatchExactWithConfig(p, "xay", true, cfg);
  try testMatchExactWithConfig(p, "xby", true, cfg);
  try testMatchExactWithConfig(p, "xcccccy", true, cfg);
  try testMatchExactWithConfig(p, "xdy", true, cfg);
  try testMatchExactWithConfig(p, "xdey", true, cfg);
  try testMatchExactWithConfig(p, "xddedddedy", true, cfg);
  try testMatchExactWithConfig(p, "xfy", true, cfg);
  try testMatchExactWithConfig(p, "xffy", true, cfg);

  try testMatchExactWithConfig(p, "xaay", false, cfg);
  try testMatchExactWithConfig(p, "xbby", false, cfg);
  try testMatchExactWithConfig(p, "xfffy", false, cfg);
  try testMatchExactWithConfig(p, "xey", false, cfg);
  try testMatchExactWithConfig(p, "xaccy", false, cfg);
  try testMatchExactWithConfig(p, "xbdy", false, cfg);

  try testMatchExactWithConfig(p, "xcecy", false, cfg);
  try testMatchExactWithConfig(p, "xddfy", false, cfg);
}

test "submachine integration: complex back-edge geometry" {
  const p = "(((a|b){2,3}|c+){2}|(d?e+){3,})+";

  try testMatchExactWithConfig(p, "aabaa", true, cfg);
  try testMatchExactWithConfig(p, "ccc", true, cfg);
  try testMatchExactWithConfig(p, "bbcccc", true, cfg);

  try testMatchExactWithConfig(p, "aa", false, cfg);
  try testMatchExactWithConfig(p, "c", false, cfg);
  try testMatchExactWithConfig(p, "aaaaaaa", false, cfg);

  try testMatchExactWithConfig(p, "eee", true, cfg);
  try testMatchExactWithConfig(p, "dedede", true, cfg);
  try testMatchExactWithConfig(p, "deeedee", true, cfg);

  try testMatchExactWithConfig(p, "ee", false, cfg);
  try testMatchExactWithConfig(p, "dede", false, cfg);

  try testMatchExactWithConfig(p, "aabaaeee", true, cfg);
  try testMatchExactWithConfig(p, "eeebbaa", true, cfg);
  try testMatchExactWithConfig(p, "ccccdededebbaa", true, cfg);

  try testMatchExactWithConfig(p, "abcc", true, cfg);
  try testMatchExactWithConfig(p, "ccbab", true, cfg);
 
  try testMatchExactWithConfig(p, "abccab", true, cfg);
}

test "submachine integration: exact state cloning preservation" {
  const p = "((a{2,4}b+|(c?d*)+)e{1,3}|(f+g?){2,}){3,5}";

  try testMatchExactWithConfig(p, "eee", true, cfg);
  try testMatchExactWithConfig(p, "ffffff", true, cfg);
  try testMatchExactWithConfig(p, "aaaabeeeaaaabeeeaaaabeeeaaaabeeeaaaabeee", true, cfg);

  try testMatchExactWithConfig(p, "ee", false, cfg);
 
  try testMatchExactWithConfig(p, "eee", true, cfg);
  try testMatchExactWithConfig(p, "eeee", true, cfg);
  try testMatchExactWithConfig(p, "eeeee", true, cfg);
 
  try testMatchExactWithConfig(p, "eeee", true, cfg);
  try testMatchExactWithConfig(p, "eeeeee", true, cfg);
  try testMatchExactWithConfig(p, "eeeeeeee", true, cfg);
 
  // Impossible to get the product 7 from {1,3} * {3,5}
  // try testMatchExactWithConfig(p, "eeeeeee", false, cfg);
  // but wait thats now how it works

  try testMatchExactWithConfig(p, "eeeeeeeee", true, cfg);
  try testMatchExactWithConfig(p, "eeeeeeeeeeee", true, cfg);
  try testMatchExactWithConfig(p, "eeeeeeeeeeeeeee", true, cfg);
  try testMatchExactWithConfig(p, "eeeeeeeeeeeeeeee", false, cfg);
 
  try testMatchExactWithConfig(p, "ffff", false, cfg);

  try testMatchExactWithConfig(p, "cddecddecdde", true, cfg);

  try testMatchExactWithConfig(p, "abeabeabe", false, cfg);
  try testMatchExactWithConfig(p, "aaaaabeeeeaaaaabeeeeaaaaabeeee", false, cfg);
  try testMatchExactWithConfig(p, "aabeeefffffaabeeefffffaabeee", true, cfg);
}
