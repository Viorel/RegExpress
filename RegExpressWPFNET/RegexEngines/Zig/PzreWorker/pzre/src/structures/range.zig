const std = @import("std");
const assert = std.debug.assert;
const expect = std.testing.expect;

/// A range of integers
/// Since end range is exclusive: the type has to be of +1 integer size higher than the logical type, e.g. u9 for ascii
pub fn Range(comptime T: type) type {
  // Comptime crashes for non pow2 integers due to the struct being packed
  // this might be fixed in the future
  assert(std.math.isPowerOfTwo(@typeInfo(T).int.bits));
  return packed struct {
    /// inclusive
    start: T,
    /// exclusive
    end: T,

    const Self = @This();

    pub fn init(start: T, end: T) Self {
      return .{
        .start = start,
        .end = end,
      };
    }

    /// Extends a range with another range, any values between them (not included in either) are included in the
    /// extension: (3 .. 5).extend(9 .. 12) -> (3 .. 12)
    pub fn extend(self: *Self, with: Self) void {
      self.assertInvariance();
      self.start = @min(self.start, with.start);
      self.end = @max(self.end, with.end);
    }

    /// Checks if a range overlaps with self
    pub fn doesOverlap(self: Self, with: Self) bool {
      self.assertInvariance();
      with.assertInvariance();
      if (self.start == self.end or with.start == with.end) return false;
      if (self.start == with.start) return true;
      return (self.start < with.start and self.end > with.start)
        or (with.start < self.start and with.end > self.start);
    }

    /// Checks if an element is within range
    pub inline fn contains(self: Self, v: T) bool {
      self.assertInvariance();
      return v >= self.start and v < self.end;
    }

    /// Checks if another range is contained fully
    pub inline fn containsRange(self: Self, other: Self) bool {
      self.assertInvariance();
      other.assertInvariance();
      return self.start <= other.start and other.end <= self.end;
    }

    pub fn len(self: Self) usize {
      self.assertInvariance();
      return self.end - self.start;
    }

    fn assertInvariance(self: Self) void {
      assert(self.start <= self.end);
    }
  };
}

test "Range" {
  const R = Range(usize);
  {
    const r = R{.start = 5, .end = 15};
    try expect(!r.doesOverlap(R{.start = 0, .end = 1}));
    try expect(!r.doesOverlap(R{.start = 0, .end = 4}));
    try expect(!r.doesOverlap(R{.start = 0, .end = 5}));
    try expect(r.doesOverlap(R{.start = 0, .end = 6}));

    try expect(!r.doesOverlap(R{.start = 4, .end = 5}));
    try expect(r.doesOverlap(R{.start = 4, .end = 6}));

    try expect(!r.doesOverlap(R{.start = 5, .end = 5}));
    try expect(r.doesOverlap(R{.start = 5, .end = 6}));
    try expect(r.doesOverlap(R{.start = 5, .end = 14}));
    try expect(r.doesOverlap(R{.start = 5, .end = 15}));
    try expect(r.doesOverlap(R{.start = 5, .end = 16}));
    try expect(r.doesOverlap(R{.start = 5, .end = 20}));

    try expect(!r.doesOverlap(R{.start = 6, .end = 6}));
    try expect(r.doesOverlap(R{.start = 6, .end = 14}));
    try expect(r.doesOverlap(R{.start = 6, .end = 15}));
    try expect(r.doesOverlap(R{.start = 6, .end = 16}));
    try expect(r.doesOverlap(R{.start = 6, .end = 20}));

    try expect(!r.doesOverlap(R{.start = 4, .end = 5}));
    try expect(r.doesOverlap(R{.start = 4, .end = 6}));
    try expect(r.doesOverlap(R{.start = 4, .end = 14}));
    try expect(r.doesOverlap(R{.start = 4, .end = 15}));
    try expect(r.doesOverlap(R{.start = 4, .end = 16}));
    try expect(r.doesOverlap(R{.start = 4, .end = 20}));

    try expect(r.doesOverlap(R{.start = 7, .end = 12}));

    try expect(!r.doesOverlap(R{.start = 14, .end = 14}));
    try expect(r.doesOverlap(R{.start = 14, .end = 15}));
    try expect(r.doesOverlap(R{.start = 14, .end = 16}));
    try expect(r.doesOverlap(R{.start = 14, .end = 17}));

    try expect(!r.doesOverlap(R{.start = 15, .end = 15}));
    try expect(!r.doesOverlap(R{.start = 15, .end = 16}));
    try expect(!r.doesOverlap(R{.start = 15, .end = 17}));
    try expect(!r.doesOverlap(R{.start = 21, .end = 30}));
  }
}

