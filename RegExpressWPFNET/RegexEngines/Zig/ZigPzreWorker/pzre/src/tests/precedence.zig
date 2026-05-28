const t = @import("test.zig");

const testMatchExact = t.testMatchExact;

test "pzre operations SIMPLER" {
  try testMatchExact("a+", "a", true);
  try testMatchExact("a{1,3}", "a", true);
  try testMatchExact("a{1,}", "a", true);
  try testMatchExact("a?", "a", true);
}

test "pzre operations" {
  try testMatchExact("a", "a", true);
  try testMatchExact("a", "", false);
  try testMatchExact("a", "", false);
  try testMatchExact("a*", "", true);
  try testMatchExact("a*", "a", true);
  try testMatchExact("a*", "aa", true);
  try testMatchExact("a+", "", false);
  try testMatchExact("a+", "a", true);
  try testMatchExact("a+", "aa", true);

  try testMatchExact("a*", "a", true);
  try testMatchExact("a+", "a", true);
  try testMatchExact("abc|123", "1233", false);

  try testMatchExact("abc|123", "123", true);
  try testMatchExact("abc|123", "abc", true);
  try testMatchExact("abc|123", "1233", false);
  try testMatchExact("abc|123", "ab", false);
  try testMatchExact("abc|123", "bc", false);

  try testMatchExact("(abc|123)+", "abcabc", true);
  try testMatchExact("(abc|123)+", "abc123", true);
  try testMatchExact("(abc|123)+", "123", true);
  try testMatchExact("(abc|123)+", "", false);
  try testMatchExact("(abc|123)+", "bc", false);
  try testMatchExact("(abc|123)+", "ab", false);

  try testMatchExact("(abc|123){1,}", "abcabc", true);
  try testMatchExact("(abc|123){1,}", "abc123", true);
  try testMatchExact("(abc|123){1,}", "123", true);
  try testMatchExact("(abc|123){1,}", "", false);
  try testMatchExact("(abc|123){1,}", "bc", false);
  try testMatchExact("(abc|123){1,}", "ab", false);

  try testMatchExact("(abc|123)*", "abcabc", true);
  try testMatchExact("(abc|123)*", "abc123", true);
  try testMatchExact("(abc|123)*", "123", true);
  try testMatchExact("(abc|123)*", "", true);
  try testMatchExact("(abc|123)*", "bc", false);
  try testMatchExact("(abc|123)*", "ab", false);

  try testMatchExact("(abc|123){0,}", "abcabc", true);
  try testMatchExact("(abc|123){0,}", "123abc123", true);
  try testMatchExact("(abc|123){0,}", "abc1", false);
  try testMatchExact("(abc|123){0,}", "123a", false);
  try testMatchExact("(abc|123){0,}", "12c", false);
  try testMatchExact("(abc|123){0,}", "ab3", false);
  try testMatchExact("(abc|123){0,}", "abc123", true);
  try testMatchExact("(abc|123){0,}", "123", true);
  try testMatchExact("(abc|123){0,}", "", true);
  try testMatchExact("(abc|123){0,}", "bc", false);
  try testMatchExact("(abc|123){0,}", "ab", false);

  try testMatchExact("(abc|123)?", "abcabc", false);
  try testMatchExact("(abc|123)?", "abc123", false);
  try testMatchExact("(abc|123)?", "123", true);
  try testMatchExact("(abc|123)?", "", true);
  try testMatchExact("(abc|123)?", "bc", false);
  try testMatchExact("(abc|123)?", "ab", false);

  try testMatchExact("(abc|123){2}", "abcabc", true);
  try testMatchExact("(abc|123){2}", "abcabcabc", false);
  try testMatchExact("(abc|123){2}", "abc", false);
  try testMatchExact("(abc|123){2}", "abc123", true);
  try testMatchExact("(abc|123){2}", "123", false);
  try testMatchExact("(abc|123){2}", "", false);
  try testMatchExact("(abc|123){2}", "bc", false);
  try testMatchExact("(abc|123){2}", "ab", false);

  try testMatchExact("(abc|123){2,3}", "abcabc", true);
  try testMatchExact("(abc|123){2,3}", "123123", true);
  try testMatchExact("(abc|123){2,3}", "abc123abc", true);
  try testMatchExact("(abc|123){2,3}", "abcabc1233", false);
  try testMatchExact("(abc|123){2,3}", "abcabc12", false);
  try testMatchExact("(abc|123){2,3}", "abcabc123", true);
  try testMatchExact("(abc|123){2,3}", "", false);
  try testMatchExact("(abc|123){2,3}", "abcabcabcabc", false);
  try testMatchExact("(abc|123){2,3}", "abcabcabc123", false);

  // check duplication uneven 2^2 + 1
  try testMatchExact("(aa){5,7}", "", false);
  try testMatchExact("(aa){5,7}", "aaaaaaaa", false);
  try testMatchExact("(aa){5,7}", "aaaaaaaaa", false);
  try testMatchExact("(aa){5,7}", "aaaaaaaaaa", true);
  try testMatchExact("(aa){5,7}", "aaaaaaaaaaa", false);
  try testMatchExact("(aa){5,7}", "aaaaaaaaaaaa", true);
  try testMatchExact("(aa){5,7}", "aaaaaaaaaaaaa", false);
  try testMatchExact("(aa){5,7}", "aaaaaaaaaaaaaa", true);
  try testMatchExact("(aa){5,7}", "aaaaaaaaaaaaaaa", false);
  try testMatchExact("(aa){5,7}", "aaaaaaaaaaaaaaaa", false);

  // exact
  try testMatchExact("(aa){2,2}", "", false);
  try testMatchExact("(aa){2,2}", "a", false);
  try testMatchExact("(aa){2,2}", "aa", false);
  try testMatchExact("(aa){2,2}", "aaa", false);
  try testMatchExact("(aa){2,2}", "aaaa", true);
  try testMatchExact("(aa){2,2}", "aaaaa", false);
  try testMatchExact("(aa){2,2}", "aaaaaa", false);

  try testMatchExact("(aa){1,1}", "", false);
  try testMatchExact("(aa){1,1}", "a", false);
  try testMatchExact("(aa){1,1}", "aa", true);
  try testMatchExact("(aa){1,1}", "aaa", false);
  try testMatchExact("(aa){1,1}", "aaaa", false);
  try testMatchExact("(aa){1,1}", "aaaaa", false);
  try testMatchExact("(aa){1,1}", "aaaaaa", false);
}
