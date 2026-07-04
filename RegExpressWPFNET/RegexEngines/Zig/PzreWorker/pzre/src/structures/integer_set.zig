const std = @import("std");
const assert = std.debug.assert;
const testing = std.testing;
const pzre = @import("../root.zig");
const meta = pzre.meta;
const math = std.math;
const Allocator = std.mem.Allocator;
const ArrayList = std.ArrayList;

const structures = pzre.structures;
const ComptimeArrayList = structures.comptime_arraylist.ComptimeArrayList;

/// A mathematical integer set composed out of ranges
/// Optimal for set theoretic operations and simd algorithms
/// 
/// Each range is interpreted as end exclusive [start, end)
/// The type has to be +1 integer size compared to the logical type, e.g. u9 for ascii (due to end exclusivity)
/// 
/// The api asserts that input and output sets are always canonized, that is, ranges are sorted in ascending order, there are no overlapping ranges, no empty ranges, split ranges are combined where possible.
///
/// -- Variants --
/// 'union' operands immutable; output does not alias with operands
/// 
/// 'unionAlloc' performs an exact allocation using gpa, output does not alias with inputs
/// 
/// 'unionInplace' modifies lhs inplace
/// 
/// -- Logical vs Data type
/// T refers to the data type. This has to be strictly greater than the logical type. As an example, for a set of ascii characters, the logical type is u8, and the data type is either u16 or u9. The set structure is generally very small, so it is best to sacrifice some insignificant space to keep the integer type neatly aligned. 
/// 
/// Api functions such as 'find' or 'containsSlice' accept a sequence of ints (anytype) as the input. This is a slice of logical type ([]const u8). The simd is designed to be extremely optimal for both cases when LT < T and when LT == T
/// 
pub fn IntegerSet(comptime T: type) type {
  pzre.meta.propertyAssert("Integer", meta.isInteger, T);
  const Range = pzre.structures.range.Range(T);
  return struct {
    ranges: []const Range,

    const Self = @This();

    /// Static; 0 elements
    pub const empty: Self = .{.ranges = &.{}};

    /// Static; all elements
    pub const universe: Self = .{.ranges = &.{Range{.start = 0, .end = std.math.maxInt(T)}}};

    /// Calculates the exact number of characters matched by a set
    pub fn cardinality(set: Self) usize {
      assert(set.isCanonical());
      var count: usize = 0;
      for (set.ranges) |range| count += (range.end - range.start);
      return count;
    }

    /// Returns the size of the compiled machine
    pub fn heapSize(self: Self) usize {
      return self.ranges.len * @sizeOf(Range);
    }

    /// Basic init, simply wraps the provided slice.
    /// Assumes the slice is valid (or will be canonized later).
    pub fn init(ranges: []const Range) Self {
      return .{ .ranges = ranges };
    }

    /// Init, but dupes the ranges using allocator
    pub fn initDuped(gpa: Allocator, ranges: []const Range) Self {
      const duped = try gpa.dupe(Range, .{ .ranges = ranges });
      return .init(duped);
    }

    /// Basic init, simply wraps the provided slice.
    /// Assumes the slice is valid (or will be canonized later).
    pub fn initComptimeEnsureCanonical(comptime ranges: []const Range) Self {
      comptime {
        const self = Self{ .ranges = ranges };
        return self.canonizeComptime();
      }
    }

    pub fn fromSliceComptime(comptime E: type, comptime slice: []const E) Self {
      comptime {
        meta.propertyAssert("Integer", meta.isInteger, E);
        assert(std.math.maxInt(E) <= std.math.maxInt(T));

        var ranges: ComptimeArrayList(Range) = .empty;
        for (slice) |c| {
          ranges.append(Range{.start = c, .end = c + 1});
        }
        var set = Self{.ranges = ranges.items};

        var ret: [ranges.items.len]Range = undefined;
        const canonized = set.canonize(&ret);
        return Self{.ranges = canonized.ranges ++ &[_]Range{}};
      }
    }

    /// Init, but dupes the ranges using allocator
    pub fn fromOwnedRanges(gpa: Allocator, ranges: []const Range) Allocator.Error!Self {
      const canonized = try canonizeAlloc(.{.ranges = ranges}, gpa);
      gpa.free(ranges);
      return canonized;
    }

    pub fn fromSlice(comptime E: type, gpa: Allocator, slice: []const E) Allocator.Error!Self {
      comptime {
        meta.propertyAssert("Integer", meta.isInteger, E);
        assert(std.math.maxInt(E) <= std.math.maxInt(T));
      }

      const ranges = try gpa.alloc(Range, slice.len);
      defer gpa.free(ranges);

      for (slice, 0..) |c, i| {
        ranges[i] = Range{.start = c, .end = @as(T, @intCast(c)) + 1};
      }
      const set = Self{.ranges = ranges};
      return set.canonizeAlloc(gpa);
    }

    // ============================================================================
    // Core Logic
    // ============================================================================

    pub fn isEmptySet(self: Self) bool {
      return self.ranges.len == 0;
    }

    pub fn isUniverse(self: Self) bool {
      return self.ranges.len == 1 
        and self.ranges[0].start == 0 
        and self.ranges[0].end == std.math.maxInt(T);
    }

    pub fn isSizeOne(self: Self) bool {
      return self.ranges.len == 1 and self.ranges[0].start + 1 == self.ranges[0].end;
    }

    fn rangeCmp(context: void, a: Range, b: Range) bool {
      _ = context;
      return a.start < b.start;
    }

    /// Base canonize: Sorts, merges overlaps, and removes empty ranges.
    /// Writes result into `out`. Returns the sub-slice of `out` that contains the result.
    /// Complexity: O(N log N) due to sort.
    pub fn canonize(self: Self, out: []Range) Self {
      const ranges = self.ranges;
      if (ranges.len == 0) return Self{.ranges = out[0..0]};

      assert(out.len >= ranges.len);

      @memcpy(out[0..ranges.len], ranges);
      const work = out[0..ranges.len];

      // 1. Sort
      std.mem.sort(Range, work, {}, rangeCmp);

      // 2. Merge/Compact
      if (work.len <= 1) {
        if (work.len == 1 and work[0].start >= work[0].end) return Self{.ranges = out[0..0]};
        const n = Self{.ranges = work};
        assert(n.isCanonical());
        return n;
      }

      var res_idx: usize = 0;
      var curr: Range = work[0];
      var i: usize = 1;

      // Edge case: First element might be empty
      while (curr.start >= curr.end and i < work.len) : (i += 1) {
        curr = work[i];
      }
      if (curr.start >= curr.end) return Self{.ranges = out[0..0]}; // All empty

      while (i < work.len) : (i += 1) {
        const next = work[i];
        if (next.start >= next.end) continue; // Skip empty

        if (next.start <= curr.end) {
          // Overlap/Abutting: Merge
          if (next.end > curr.end) {
            curr.end = next.end;
          }
        } else {
          // Disjoint: Push
          out[res_idx] = curr;
          res_idx += 1;
          curr = next;
        }
      }
      // Push final
      if (curr.start < curr.end) {
        out[res_idx] = curr;
        res_idx += 1;
      }

      const n = Self{.ranges = out[0..res_idx]};
      assert(n.isCanonical());
      return n;
    }

    /// Checks if the set is in canonical form in a single pass.
    /// Requirements:
    /// 1. No empty ranges (start < end)
    /// 2. Sorted by start
    /// 3. No overlaps
    /// 4. No abutting ranges (gaps must exist between ranges)
    pub fn isCanonical(self: Self) bool {
      if (self.ranges.len == 0) return true;

      // Check first element for emptiness
      if (self.ranges[0].start >= self.ranges[0].end) return false;

      for (self.ranges[1..], 0..) |curr, i| {
        const prev = self.ranges[i]; // 'i' here points to the previous element due to slice offset

        // 1. Check Non-Empty
        if (curr.start >= curr.end) return false;

        // 2, 3, 4. Check Sorted, Disjoint, and Gapped
        // Logic:
        // If curr.start < prev.end: They overlap or are unsorted.
        // If curr.start == prev.end: They abut (should be merged).
        // Therefore, we strictly require curr.start > prev.end.
        if (curr.start <= prev.end) return false;
      }

      return true;
    }

    /// Computes the complement of the set (relative to the full range of T).
    /// Writes result into 'out'. 
    /// Note: Due to Range using T for 'end' (exclusive), the value maxInt(T) is effectively the upper bound.
    pub fn complement(self: Self, out: []Range) Self {
      assert(self.isCanonical());
      var res_idx: usize = 0;
      var cursor: T = std.math.minInt(T);

      for (self.ranges) |range| {
        if (range.start > cursor) {
          out[res_idx] = .{ .start = cursor, .end = range.start };
          res_idx += 1;
        }
        // Advance cursor past the current range
        if (range.end > cursor) {
          cursor = range.end;
        }
      }

      // Fill tail if not at max
      const max = std.math.maxInt(T);
      if (cursor < max) {
        out[res_idx] = .{ .start = cursor, .end = max };
        res_idx += 1;
      }
      const n = Self{.ranges = out[0..res_idx]};
      assert(n.isCanonical());
      return n;
    }

    pub const ShiftOp = enum { add, sub };

    /// Shifts all elements in the set by a constant amount.
    /// Preserves canonical form automatically.
    pub fn shift(self: Self, comptime op: ShiftOp, amount: T, out: []Range) Self {
      assert(self.isCanonical());
      assert(out.len >= self.ranges.len);

      for (self.ranges, 0..) |rng, i| {
        out[i] = switch (op) {
          .add => .{ .start = rng.start + amount, .end = rng.end + amount },
          .sub => .{ .start = rng.start - amount, .end = rng.end - amount },
        };
      }
      const n = Self{ .ranges = out[0..self.ranges.len] };
      assert(n.isCanonical());
      return n;
    }

    /// Standard union mathematical set operation
    /// Writes result to `out`.
    pub fn @"union"(a: Self, b: Self, out: []Range) Self {
      assert(a.isCanonical());
      assert(b.isCanonical());
      var i: usize = 0;
      var j: usize = 0;
      var res_idx: usize = 0;
      var curr: Range = undefined;

      // Initialize accumulator with the earliest range
      if (i < a.ranges.len and (j >= b.ranges.len or a.ranges[i].start < b.ranges[j].start)) {
        curr = a.ranges[i];
        i += 1;
      } else if (j < b.ranges.len) {
        curr = b.ranges[j];
        j += 1;
      } else {
        return Self{.ranges = out[0..0]};
      }

      while (i < a.ranges.len or j < b.ranges.len) {
        var next: Range = undefined;
        if (i < a.ranges.len and (j >= b.ranges.len or a.ranges[i].start < b.ranges[j].start)) {
          next = a.ranges[i];
          i += 1;
        } else {
          next = b.ranges[j];
          j += 1;
        }

        if (next.start <= curr.end) {
          if (next.end > curr.end) curr.end = next.end;
        } else {
          out[res_idx] = curr;
          res_idx += 1;
          curr = next;
        }
      }
      out[res_idx] = curr;
      res_idx += 1;
      const n = Self{.ranges = out[0..res_idx]};
      assert(n.isCanonical());
      return n;
    }

    /// Standard intersection mathematical set operation
    pub fn intersect(a: Self, b: Self, out: []Range) Self {
      assert(a.isCanonical());
      assert(b.isCanonical());
      var i: usize = 0;
      var j: usize = 0;
      var res_idx: usize = 0;

      while (i < a.ranges.len and j < b.ranges.len) {
        const r1 = a.ranges[i];
        const r2 = b.ranges[j];

        const s = @max(r1.start, r2.start);
        const e = @min(r1.end, r2.end);

        if (s < e) {
          out[res_idx] = .{ .start = s, .end = e };
          res_idx += 1;
        }

        if (r1.end < r2.end) {
          i += 1;
        } else {
          j += 1;
        }
      }
      const n = Self{.ranges = out[0..res_idx]};
      assert(n.isCanonical());
      return n;
    }

    /// Subtracts 'b' from 'a'
    pub fn subtract(a: Self, b: Self, out: []Range) Self {
      assert(a.isCanonical());
      assert(b.isCanonical());
      var i: usize = 0;
      var j: usize = 0;
      var res_idx: usize = 0;

      // We need a nullable current because split logic might invalidate it
      // or we might need to carry it over iterations.
      // Using an optional here:
      var curr_opt: ?Range = null;

      while (i < a.ranges.len) {
        if (curr_opt == null) {
          curr_opt = a.ranges[i];
        }
        // We know curr is not null here, capture by value for mutation
        var curr = curr_opt.?;

        if (j >= b.ranges.len) {
          // Flush remaining A
          out[res_idx] = curr;
          res_idx += 1;
          curr_opt = null; // consumed
          // Append rest of A
          const rest = a.ranges[i + 1 ..];
          @memcpy(out[res_idx .. res_idx + rest.len], rest);
          res_idx += rest.len;

          const n = Self{.ranges = out[0..res_idx]};
          assert(n.isCanonical());
          return n;
        }

        const sub = b.ranges[j];

        if (sub.end <= curr.start) {
          // B is entirely before A
          j += 1;
          // Keep curr active, loop again against next B
          curr_opt = curr;
        } else if (sub.start >= curr.end) {
          // B is entirely after A
          out[res_idx] = curr;
          res_idx += 1;
          curr_opt = null;
          i += 1;
        } else {
          // Overlap
          if (sub.start <= curr.start) {
            if (sub.end >= curr.end) {
              // B covers A completely
              curr_opt = null;
              i += 1;
            } else {
              // Head Chop (remove start of A)
              curr.start = sub.end;
              curr_opt = curr;
            }
          } else {
            // B starts inside A
            // Commit prefix
            out[res_idx] = .{ .start = curr.start, .end = sub.start };
            res_idx += 1;

            if (sub.end < curr.end) {
              // Middle Split (keep rest of A active)
              curr.start = sub.end;
              curr_opt = curr;
            } else {
              // Tail Chop (remove end of A)
              curr_opt = null;
              i += 1;
            }
          }
        }
      }
      const n = Self{.ranges = out[0..res_idx]};
      assert(n.isCanonical());
      return n;
    }

    // ============================================================================
    // Boolean Queries (No Allocation)
    // ============================================================================

    pub fn overlap(a: Self, b: Self) bool {
      assert(a.isCanonical());
      assert(b.isCanonical());
      var i: usize = 0;
      var j: usize = 0;
      while (i < a.ranges.len and j < b.ranges.len) {
        const start = @max(a.ranges[i].start, b.ranges[j].start);
        const end = @min(a.ranges[i].end, b.ranges[j].end);

        if (start < end) return true;

        if (a.ranges[i].end < b.ranges[j].end) {
          i += 1;
        } else {
          j += 1;
        }
      }
      return false;
    }

    pub fn subset(a: Self, b: Self) bool {
      assert(a.isCanonical());
      assert(b.isCanonical());
      if (a.ranges.len == 0) return b.ranges.len > 0;

      var i: usize = 0;
      var j: usize = 0;
      var is_strict = false;

      while (i < a.ranges.len) {
        if (j >= b.ranges.len) return false;

        const rA = a.ranges[i];
        const rB = b.ranges[j];

        if (rB.end <= rA.start) {
          is_strict = true;
          j += 1;
        } else if (rB.start > rA.start) {
          return false;
        } else {
          // rB.start <= rA.start
          if (rB.start > rA.start or rB.end < rA.end) return false;
          // rB covers rA
          if (rB.start < rA.start or rB.end > rA.end) is_strict = true;
          i += 1;
          if (rB.end == rA.end) j += 1;
        }
      }
      if (j < b.ranges.len) is_strict = true;
      return is_strict;
    }

    pub fn subseteq(a: Self, b: Self) bool {
      assert(a.isCanonical());
      assert(b.isCanonical());
      var i: usize = 0;
      var j: usize = 0;
      while (i < a.ranges.len) {
        if (j >= b.ranges.len) return false;

        const rA = a.ranges[i];
        const rB = b.ranges[j];

        if (rB.end <= rA.start) {
          j += 1;
        } else if (rB.start > rA.start) {
          return false;
        } else {
          if (rB.end >= rA.end) {
            i += 1;
            // Do not advance j, it might cover next A
          } else {
            return false;
          }
        }
      }
      return true;
    }

    pub fn equal(a: Self, b: Self) bool {
      assert(a.isCanonical());
      assert(b.isCanonical());
      if (a.ranges.len != b.ranges.len) return false;
      for (a.ranges, 0..) |rA, i| {
        const rB = b.ranges[i];
        if (rA.start != rB.start or rA.end != rB.end) return false;
      }
      return true;
    }

    // ============================================================================
    // Unified SIMD Scanner
    // ============================================================================

    const ScanMode = enum {
      FindMember,
      FindNonMember,
    };

    fn assertSequence(comptime Int: type) void {
      comptime {
        assert(@typeInfo(Int) == .int);
        assert(@bitSizeOf(Int) <= @bitSizeOf(T));
      }
    }

    /// Returns index of first non-member (failure), or null if all pass.
    /// sequence has to be a slice of integers with int_size <= T
    pub fn findNot(self: Self, comptime Int: type, sequence: []const Int) ?usize {
      assertSequence(Int);
      return self.scan(Int, sequence, .FindNonMember);
    }

    /// Returns index of first member (success), or null if none found.
    /// sequence has to be a slice of integers with int_size <= T
    pub fn find(self: Self, comptime Int: type, sequence: []const Int) ?usize {
      assertSequence(Int);
      return self.scan(Int, sequence, .FindMember);
    }

    /// sequence has to be a slice of integers with int_size <= T
    pub fn containsSlice(self: Self, comptime Int: type, sequence: []const Int) bool {
      assertSequence(Int);
      return self.findNot(Int, sequence) == null;
    }

    /// The generic SIMD kernel.
    /// 'mode' is comptime-known, so the 'if' checks inside loops vanish in the binary.
    fn scan(self: Self, comptime Int: type, sequence: []const Int, comptime mode: ScanMode) ?usize {
      assertSequence(Int);
      if (comptime std.simd.suggestVectorLength(T)) |simd_size| {
        if (sequence.len >= simd_size) {
          return self.scanVec(simd_size, Int, sequence, mode);
        }
      }
      return self.scanScalar(Int, sequence, mode);
    }

    inline fn scanScalar(self: Self, comptime Int: type, sequence: []const Int, comptime mode: ScanMode) ?usize {
      for (sequence, 0..) |val, i| {
        const is_member = self.contains(Int, val);
        switch (mode) {
          // We want the first Member, so return index if is_member is true
          .FindMember => if (is_member) return i,
          // We want the first NonMember, so return index if is_member is false
          .FindNonMember => if (!is_member) return i,
        }
      }
      return null;
    }

    /// Optimized bit-scan helper.
    /// Inlines perfectly because 'mode' is comptime.
    inline fn checkMask(comptime N: usize, mask: @Vector(N, bool), comptime mode: ScanMode) ?usize {
      const Int = std.meta.Int(.unsigned, N);
      const bits: Int = @bitCast(mask);

      // If looking for members, we want 1s (bits).
      // If looking for non-members, we want 0s (~bits).
      const target = switch (comptime mode) {
        .FindMember => bits,
        .FindNonMember => ~bits,
      };

      if (target == 0) return null;
      return @ctz(target);
    }

    /// Binary search check for value containment.
    /// O(log N) complexity.
    pub fn contains(self: Self, comptime Int: type, val: Int) bool {
      assert(self.isCanonical());
      var low: usize = 0;
      var high: usize = self.ranges.len;

      while (low < high) {
        const mid = low + (high - low) / 2;
        const rng = self.ranges[mid];

        if (val < rng.start) {
          high = mid;
        } else if (val >= rng.end) {
          low = mid + 1;
        } else {
          return true;
        }
      }
      return false;
    }

    // The "Magic" SIMD Kernel
    // No widening. No refactoring. Uses standard u8 comparison.
    pub inline fn containsVec(self: Self, comptime N: usize, chunk: anytype) @Vector(N, bool) {
      assert(self.isCanonical());
      // 1. Detect Input Type (e.g., u8)
      const InputT = @typeInfo(@TypeOf(chunk)).vector.child;
      const max_val = std.math.maxInt(InputT);
      var mask: @Vector(N, bool) = @splat(false);

      if (comptime InputT != T) { // The input integer type is smaller than T
        for (self.ranges) |range| {
          const start_check = chunk >= @as(@Vector(N, T), @splat(range.start));
          const end_check = chunk < @as(@Vector(N, T), @splat(range.end));
          mask = mask | (start_check & end_check);
        }
        return mask;
      } else {
        for (self.ranges) |range| {
          // OPTIMIZATION 1: Skip ranges that start too high
          // If the range starts at 300, a u8 (0-255) can never reach it.
          if (range.start > max_val) continue;

          // 2. Cast 'Start' (Safe because we checked > max_val)
          const vec_start: @Vector(N, InputT) = @splat(@intCast(range.start));

          // THE MAGIC TRICK:
          // Convert [Start, End) -> [Start, End - 1]
          // We use standard integer subtraction on the u16 type, THEN cast.
          // If end was 256, end-1 is 255. Term u8 perfectly.
          const inclusive_end = range.end - 1;

          // OPTIMIZATION 2: Saturate 'End'
          // If the range goes up to 500 (effectively "the rest"), clamp it to 255.
          const clamped_end = @min(inclusive_end, max_val);
          const vec_end: @Vector(N, InputT) = @splat(@intCast(clamped_end));

          // 3. Perform Inclusive Check
          // val >= start AND val <= (end - 1)
          const in_range = (chunk >= vec_start) & (chunk <= vec_end);
          mask = mask | in_range;
        }
        return mask;
      }
    }

    fn scanVec(self: Self, comptime simd_size: comptime_int, comptime Int: type, sequence: []const Int, comptime mode: ScanMode) ?usize {
      const InputT = @typeInfo(@TypeOf(sequence)).pointer.child;
      if (@bitSizeOf(InputT) > @bitSizeOf(T)) {
        // @compileError("Input slice type is larger than Set element type");
      }
      const Chunk = @Vector(simd_size, InputT);
      const len = sequence.len;

      // 1. Reduced Size Optimization
      inline for (1..6) |s| {
        const n = 16 << s; 
        if (n <= simd_size and len <= n) {
          const reduced_size = n / 2;
          const V = @Vector(reduced_size, T);

          { // First half
            const chunk: V = @bitCast(sequence[0..reduced_size].*);
            const mask = self.containsVec(reduced_size, chunk);
            if (checkMask(reduced_size, mask, mode)) |idx| return idx;
          }

          { // Second half
            const offset = len - reduced_size;
            const chunk: V = @bitCast(sequence[offset..][0..reduced_size].*);
            const mask = self.containsVec(reduced_size, chunk);
            if (checkMask(reduced_size, mask, mode)) |idx| return offset + idx;
          }
          return null;
        }
      }

      // 2. Main Loop
      const loop_count = (len - 1) / simd_size;
      for (0..loop_count) |i| {
        const offset = i * simd_size;
        const chunk: Chunk = @bitCast(sequence[offset..][0..simd_size].*);
        const mask = self.containsVec(simd_size, chunk);

        if (checkMask(simd_size, mask, mode)) |idx| return offset + idx;
      }

      // 3. Tail
      const tail_offset = len - simd_size;
      const last_chunk: Chunk = @bitCast(sequence[tail_offset..][0..simd_size].*);
      const last_mask = self.containsVec(simd_size, last_chunk);

      if (checkMask(simd_size, last_mask, mode)) |idx| return tail_offset + idx;

      return null;
    }

    // ============================================================================
    // Alloc Variants
    // ============================================================================

    /// The recommended buffer sizes for transformations
    pub const buf_size = struct {
      pub inline fn @"union"(a: Self, b: Self) usize {
        return a.ranges.len + b.ranges.len;
      }

      pub inline fn shift(a: Self) usize {
        return a.ranges.len;
      }

      pub inline fn intersect(a: Self, b: Self) usize {
        return @max(a.ranges.len, b.ranges.len);
      }

      pub inline fn subtract(a: Self, b: Self) usize {
        // Worst case: B splits every interval of A.
        return a.ranges.len + b.ranges.len;
      }

      pub inline fn complement(a: Self) usize {
        return a.ranges.len + 1;
      }

      pub inline fn canonize(a: Self) usize {
        return a.ranges.len;
      }
    };

    fn finalizeAlloc(gpa: Allocator, buf: []Range, out: Self) Allocator.Error!Self {
      if (@inComptime()) unreachable; // comptime allocator bugs the fuck out
      const used_len = out.ranges.len;
      if (gpa.remap(buf, used_len)) |new_mem| {
        // 'new_mem' is a reference to a comptime variable
        // The ++ operator acts as a copy which will essentially freeze the memory region so it can be used at runtime
        return Self{ .ranges = new_mem };
      }

      const new_mem = try gpa.alloc(Range, used_len);
      @memcpy(new_mem, buf[0..used_len]);
      gpa.free(buf);

      return Self{ .ranges = new_mem };
    }

    fn finalizeAllocInplace(self: *Self, gpa: Allocator, buf: []Range, out: Self) Allocator.Error!void {
      const finalized = try finalizeAlloc(gpa, buf, out);
      self.deinit(gpa);
      self.ranges = finalized.ranges;
    }

    pub fn dupe(self: Self, gpa: Allocator) Allocator.Error!Self {
      const duped = try gpa.dupe(Range, self.ranges);
      return Self{.ranges = duped};
    }

    pub fn canonizeAlloc(set: Self, gpa: Allocator) Allocator.Error!Self {
      const buf = try gpa.alloc(Range, buf_size.canonize(set));
      errdefer gpa.free(buf);

      const res = set.canonize(buf); 
      return finalizeAlloc(gpa, buf, res);
    }

    pub fn canonizeInplace(self: *Self, gpa: Allocator) Allocator.Error!void {
      const set = self.*;
      const buf = try gpa.alloc(Range, buf_size.canonize(set));
      errdefer gpa.free(buf);

      const res = set.canonize(buf); 
      try self.finalizeAllocInplace(gpa, buf, res);
    }

    pub fn canonizeComptime(a: Self) Self {
      const size = buf_size.canonize(a);
      var buf: [size]Range = undefined;
      const res = a.canonize(buf[0..]);
      return Self{.ranges = res.ranges ++ &[_]Range{}};
    }

    pub fn complementAlloc(a: Self, gpa: Allocator) Allocator.Error!Self {
      const size = buf_size.complement(a);
      const buf = try gpa.alloc(Range, size);
      errdefer gpa.free(buf);

      const res = a.complement(buf[0..]);
      return finalizeAlloc(gpa, buf[0..], res);
    }

    pub fn complementInplace(self: *Self, gpa: Allocator) Allocator.Error!void {
      const a = self.*;
      const buf = try gpa.alloc(Range, buf_size.complement(a));
      errdefer gpa.free(buf);

      const res = a.complement(buf);
      try self.finalizeAllocInplace(gpa, buf, res);
    }

    pub fn complementComptime(comptime a: Self) Self {
      const size = buf_size.complement(a);
      var buf: [size]Range = undefined;
      const res = a.complement(buf[0..]);
      return Self{.ranges = res.ranges ++ &[_]Range{}};
    }

    pub fn unionAlloc(a: Self, b: Self, gpa: Allocator) Allocator.Error!Self {
      const buf = try gpa.alloc(Range, buf_size.@"union"(a, b));
      errdefer gpa.free(buf);

      const res = @"union"(a, b, buf);
      return try finalizeAlloc(gpa, buf, res);
    }

    pub fn unionInplace(self: *Self, b: Self, gpa: Allocator) Allocator.Error!void {
      const a = self.*;
      const buf = try gpa.alloc(Range, buf_size.@"union"(a, b));
      errdefer gpa.free(buf);

      const res = @"union"(a, b, buf);
      try self.finalizeAllocInplace(gpa, buf, res);
    }

    pub fn unionComptime(a: Self, b: Self) Self {
      const size = buf_size.@"union"(a, b);
      var buf: [size]Range = undefined;
      const res = a.@"union"(b, buf[0..]);
      return Self{.ranges = res.ranges ++ &[_]Range{}};
    }

    pub fn intersectAlloc(a: Self, b: Self, gpa: Allocator) Allocator.Error!Self {
      const buf = try gpa.alloc(Range, buf_size.intersect(a, b));
      errdefer gpa.free(buf);

      const res = intersect(a, b, buf);
      return finalizeAlloc(gpa, buf, res);
    }

    pub fn intersectInplace(self: *Self, b: Self, gpa: Allocator) Allocator.Error!void {
      const a = self.*;
      const buf = try gpa.alloc(Range, buf_size.intersect(a, b));
      errdefer gpa.free(buf);

      const res = intersect(a, b, buf);
      try self.finalizeAllocInplace(gpa, buf, res);
    }

    pub fn intersectComptime(a: Self, b: Self) Self {
      const size = buf_size.intersect(a, b);
      var buf: [size]Range = undefined;
      const res = a.intersect(b, buf[0..]);
      return Self{.ranges = res.ranges ++ &[_]Range{}};
    }

    pub fn subtractAlloc(a: Self, b: Self, gpa: Allocator) Allocator.Error!Self {
      const buf = try gpa.alloc(Range, buf_size.subtract(a, b));
      errdefer gpa.free(buf);

      const res = subtract(a, b, buf);
      return finalizeAlloc(gpa, buf, res);
    }

    pub fn subtractInplace(self: *Self, b: Self, gpa: Allocator) Allocator.Error!void {
      const a = self.*;
      const buf = try gpa.alloc(Range, buf_size.subtract(a, b));
      errdefer gpa.free(buf);

      const res = subtract(a, b, buf);
      try self.finalizeAllocInplace(gpa, buf, res);
    }

    pub fn subtractComptime(a: Self, b: Self) Self {
      const size = buf_size.subtract(a, b);
      var buf: [size]Range = undefined;
      const res = a.subtract(b, buf[0..]);
      return Self{.ranges = res.ranges ++ &[_]Range{}};
    }

    pub fn shiftAlloc(a: Self, comptime op: ShiftOp, amount: T, gpa: Allocator) Allocator.Error!Self {
      const buf = try gpa.alloc(Range, buf_size.shift(a));
      errdefer gpa.free(buf);

      const res = a.shift(op, amount, buf);
      return finalizeAlloc(gpa, buf, res);
    }

    pub fn shiftInplace(self: *Self, comptime op: ShiftOp, amount: T, gpa: Allocator) Allocator.Error!void {
      const a = self.*;
      const buf = try gpa.alloc(Range, buf_size.shift(a));
      errdefer gpa.free(buf);

      const res = a.shift(op, amount, buf);
      try self.finalizeAllocInplace(gpa, buf, res);
    }

    pub fn shiftComptime(comptime a: Self, comptime op: ShiftOp, comptime amount: T) Self {
      const size = buf_size.shift(a);
      var buf: [size]Range = undefined;
      const res = a.shift(op, amount, buf[0..]);
      return Self{ .ranges = res.ranges ++ &[_]Range{} };
    }

    /// Optimally transforms 'self' to 'to' by buffer reduction and mem copy
    /// 'to' cannot alias with self
    /// 'self.ranges' has to be owned by gpa
    pub fn transformOwnedInplace(self: *Self, to: Self, gpa: Allocator) void {
      var ranges: ArrayList(Range) = .fromOwnedSlice(@constCast(self.ranges));
      ranges.shrinkAndFree(gpa, to.ranges.len);
      const dest_bytes = std.mem.sliceAsBytes(ranges.items);
      const src_bytes = std.mem.sliceAsBytes(to.ranges);
      @memcpy(dest_bytes, src_bytes);
      self.ranges = ranges.items;
    }

    /// Creates a slice sequence (ascending order)
    /// Ex. [2, 4), [13, 14)  ->  2, 3, 13
    pub fn toSequence(a: Self, comptime R: type, gpa: Allocator) Allocator.Error![]R {
      comptime assert(@typeInfo(R).int.bits <= @typeInfo(T).int.bits);
      assert(a.isCanonical());

      // We avoid toOwnedSlice deallocation by calculating the exact allocation size
      var size: usize = 0;
      for (a.ranges) |range| {
        size += range.end - range.start;
      }

      var arr: ArrayList(R) = try .initCapacity(gpa, size);
      for (a.ranges) |range| {
        for (range.start .. range.end) |i| arr.appendAssumeCapacity(@intCast(i));
      }
      assert(arr.items.len == size);
      assert(arr.capacity == size);
      return arr.items;
    }

    /// Creates a slice sequence (ascending order)
    /// Ex. [2, 4), [13, 14)  ->  2, 3, 13
    pub fn toSequenceComptime(a: Self, comptime R: type) []const R {
      comptime {
        @setEvalBranchQuota(10000);
        assert(@typeInfo(R).int.bits <= @typeInfo(T).int.bits);
        assert(a.isCanonical());

        var arr: ComptimeArrayList(R) = .empty;
        for (a.ranges) |range| {
          for (range.start .. range.end) |i| arr.append(i);
        }
        return arr.items;
      }
    }

    pub fn deinit(self: Self, gpa: Allocator) void {
      gpa.free(self.ranges);
    }
  };
}

// ============================================================================
// Tests
// ============================================================================

const Set = IntegerSet(u32);
const R32 = pzre.structures.range.Range(u32);

fn fmt(gpa: Allocator, ranges: []const R32) ![]u8 {
  var str: std.ArrayList(u8) = .empty;
  errdefer str.deinit(gpa);

  try str.append(gpa, '{');
  for (ranges, 0..) |rng, i| {
    try str.print(gpa, "[{d}, {d})", .{ rng.start, rng.end });
    if (i < ranges.len - 1) try str.appendSlice(gpa, ", ");
  }
  try str.append(gpa, '}');

  return str.toOwnedSlice(gpa);
}

fn expectEqualSets(expected: Set, actual: Set) !void {
  const gpa = std.testing.allocator;
  if (!expected.equal(actual)) {
    const e_str = try fmt(gpa, expected.ranges);
    defer gpa.free(e_str);
    const a_str = try fmt(gpa, actual.ranges);
    defer gpa.free(a_str);
    std.debug.print("\nExpected: {s}\nActual:   {s}\n", .{ e_str, a_str });
    return error.TestExpectedEqual;
  }
}

fn runTest(
  op: []const u8,
  a: Set,
  b: ?Set,
  expected: anytype, // Set or bool
) !void {
  const gpa = std.testing.allocator;
  const T = @TypeOf(expected);

  if (comptime T == bool) {
    if (std.mem.eql(u8, op, "overlap")) {
      try testing.expectEqual(expected, a.overlap(b.?));
    } else if (std.mem.eql(u8, op, "subset")) {
      try testing.expectEqual(expected, a.subset(b.?));
    } else if (std.mem.eql(u8, op, "subseteq")) {
      try testing.expectEqual(expected, a.subseteq(b.?));
    } else @panic("Unknown op");
  } else {
    if (std.mem.eql(u8, op, "union")) {
      const res = try Set.unionAlloc(a, b.?, gpa);
      defer gpa.free(res.ranges);
      try expectEqualSets(expected, res);
    } else if (std.mem.eql(u8, op, "intersect")) {
      const res = try Set.intersectAlloc(a, b.?, gpa);
      defer gpa.free(res.ranges);
      try expectEqualSets(expected, res);
    } else if (std.mem.eql(u8, op, "subtract")) {
      const res = try Set.subtractAlloc(a, b.?, gpa);
      defer gpa.free(res.ranges);
      try expectEqualSets(expected, res);
    } else if (std.mem.eql(u8, op, "canonize")) {
      const res = try a.canonizeAlloc(gpa);
      defer gpa.free(res.ranges);
      try expectEqualSets(expected, res);
    } else {
      @panic("Unknown op");
    }
  }
}

// Lua tests imported by AI

test "IntegerSet: Lua Port Complete Suite" {
  // --- Definitions (Identical to Lua) ---
  const s_full  = Set.init(&.{r(0, 100)});
  const s_left  = Set.init(&.{r(0, 50)});
  const s_right = Set.init(&.{r(50, 100)});
  const s_mid   = Set.init(&.{r(40, 60)});
  const s_gap   = Set.init(&.{r(0, 40), r(60, 100)}); // Gap at 40-60
  const s_empty = Set.init(&.{});

  // Exhaustive Permutations Variables
  const r_target_split   = Set.init(&.{ r(30, 120), r(150, 180) });

  const r_in_in          = Set.init(&.{r(50, 170)});
  const r_in_out         = Set.init(&.{r(50, 200)});
  const r_out_in         = Set.init(&.{r(10, 160)});
  const r_out_out        = Set.init(&.{r(50, 200)});

  const r_in_in_border   = Set.init(&.{r(30, 150)});
  const r_in_out_border  = Set.init(&.{r(30, 180)});
  const r_out_in_border  = Set.init(&.{r(120, 180)});
  const r_out_out_border = Set.init(&.{r(120, 180)});


  // --- Basic Set Operations ---
  try runTest("union",     s_left, s_right, s_full);  // Should merge to [0, 100)
  try runTest("union",     s_left, s_mid, Set.init(&.{r(0, 60)})); // Should merge to [0, 60)
  try runTest("intersect", s_left, s_mid, Set.init(&.{r(40, 50)})); // Should be [40, 50)
  try runTest("intersect", s_left, s_right, s_empty); // Should be empty (abutting)
  try runTest("subtract",  s_full, s_mid, s_gap); // Should split into [0, 40), [60, 100)
  try runTest("subtract",  s_left, s_mid, Set.init(&.{r(0, 40)})); // Tail chop
  try runTest("subtract",  s_mid,  s_left, Set.init(&.{r(50, 60)})); // Head chop
  // Note: The Lua test M.range(1, 7), M.range(0, 2) test was variable dependent (M.range(a,b)) but implied 2-7
  try runTest("subtract",  Set.init(&.{r(1, 7)}), Set.init(&.{r(0, 2)}), Set.init(&.{r(2, 7)}));


  // --- Edge Cases (Empty/Identity) ---
  try runTest("subtract",  s_empty, s_empty, s_empty);
  try runTest("subtract",  s_full, s_empty, s_full);
  try runTest("subtract",  s_empty, s_full, s_empty);
  try runTest("subtract",  s_full, s_full, s_empty);

  try runTest("union",  s_empty, s_empty, s_empty);
  try runTest("union",  s_full, s_empty, s_full);
  try runTest("union",  s_empty, s_full, s_full);
  try runTest("union",  s_full, s_full, s_full);

  try runTest("intersect",  s_empty, s_empty, s_empty);
  try runTest("intersect",  s_full, s_empty, s_empty);
  try runTest("intersect",  s_empty, s_full, s_empty);
  try runTest("intersect",  s_full, s_full, s_full);

  try runTest("subset",  s_empty, s_empty, false); // Strict subset check in lua logic returned false for equal sets?
  // Reviewing Lua logic: "subset" checked strict subset.
  // Lua code: if #a == 0 then return #b > 0 end.
  // s_empty vs s_empty -> #a=0, return #b>0 (0>0 is false). Correct.
  try runTest("subset",  s_full, s_empty, false);
  try runTest("subset",  s_empty, s_full, true);
  try runTest("subset",  s_full, s_full, false);

  try runTest("subseteq",  s_empty, s_empty, true);
  try runTest("subseteq",  s_full, s_empty, false);
  try runTest("subseteq",  s_empty, s_full, true);
  try runTest("subseteq",  s_full, s_full, true);

  try runTest("overlap",  s_empty, s_empty, false);
  try runTest("overlap",  s_full, s_empty, false);
  try runTest("overlap",  s_empty, s_full, false);
  try runTest("overlap",  s_full, s_full, true);


  // --- Exhaustive Permutations (Union - 18 permutations) ---

  // 1. Target vs Target
  try runTest("union", r_target_split, r_target_split, r_target_split);
  try runTest("union", r_target_split, r_target_split, r_target_split);

  // 2. Target vs r_in_in [(50, 170)] -> Fills gap
  try runTest("union", r_target_split, r_in_in, Set.init(&.{r(30, 180)}));
  try runTest("union", r_in_in, r_target_split, Set.init(&.{r(30, 180)}));

  // 3. Target vs r_in_out [(50, 200)] -> Extends end
  try runTest("union", r_target_split, r_in_out, Set.init(&.{r(30, 200)}));
  try runTest("union", r_in_out, r_target_split, Set.init(&.{r(30, 200)}));

  // 4. Target vs r_out_in [(10, 160)] -> Extends start
  try runTest("union", r_target_split, r_out_in, Set.init(&.{r(10, 180)}));
  try runTest("union", r_out_in, r_target_split, Set.init(&.{r(10, 180)}));

  // 5. Target vs r_out_out [(50, 200)] -> Same as r_in_out
  try runTest("union", r_target_split, r_out_out, Set.init(&.{r(30, 200)}));
  try runTest("union", r_out_out, r_target_split, Set.init(&.{r(30, 200)}));

  // 6. Target vs r_in_in_border [(30, 150)] -> Fills gap
  try runTest("union", r_target_split, r_in_in_border, Set.init(&.{r(30, 180)}));
  try runTest("union", r_in_in_border, r_target_split, Set.init(&.{r(30, 180)}));

  // 7. Target vs r_in_out_border [(30, 180)] -> Fills gap
  try runTest("union", r_target_split, r_in_out_border, Set.init(&.{r(30, 180)}));
  try runTest("union", r_in_out_border, r_target_split, Set.init(&.{r(30, 180)}));

  // 8. Target vs r_out_in_border [(120, 180)] -> Merges
  try runTest("union", r_target_split, r_out_in_border, Set.init(&.{r(30, 180)}));
  try runTest("union", r_out_in_border, r_target_split, Set.init(&.{r(30, 180)}));

  // 9. Target vs r_out_out_border [(120, 180)] -> Same as #8
  try runTest("union", r_target_split, r_out_out_border, Set.init(&.{r(30, 180)}));
  try runTest("union", r_out_out_border, r_target_split, Set.init(&.{r(30, 180)}));


  // --- Exhaustive Permutations (Intersect - 18 permutations) ---

  // 1. Target vs Target
  try runTest("intersect", r_target_split, r_target_split, r_target_split);
  try runTest("intersect", r_target_split, r_target_split, r_target_split);

  // 2. Target vs r_in_in [(50, 170)]
  try runTest("intersect", r_target_split, r_in_in, Set.init(&.{ r(50, 120), r(150, 170) }));
  try runTest("intersect", r_in_in, r_target_split, Set.init(&.{ r(50, 120), r(150, 170) }));

  // 3. Target vs r_in_out [(50, 200)]
  try runTest("intersect", r_target_split, r_in_out, Set.init(&.{ r(50, 120), r(150, 180) }));
  try runTest("intersect", r_in_out, r_target_split, Set.init(&.{ r(50, 120), r(150, 180) }));

  // 4. Target vs r_out_in [(10, 160)]
  try runTest("intersect", r_target_split, r_out_in, Set.init(&.{ r(30, 120), r(150, 160) }));
  try runTest("intersect", r_out_in, r_target_split, Set.init(&.{ r(30, 120), r(150, 160) }));

  // 5. Target vs r_out_out [(50, 200)]
  try runTest("intersect", r_target_split, r_out_out, Set.init(&.{ r(50, 120), r(150, 180) }));
  try runTest("intersect", r_out_out, r_target_split, Set.init(&.{ r(50, 120), r(150, 180) }));

  // 6. Target vs r_in_in_border [(30, 150)]
  try runTest("intersect", r_target_split, r_in_in_border, Set.init(&.{r(30, 120)}));
  try runTest("intersect", r_in_in_border, r_target_split, Set.init(&.{r(30, 120)}));

  // 7. Target vs r_in_out_border [(30, 180)]
  try runTest("intersect", r_target_split, r_in_out_border, r_target_split);
  try runTest("intersect", r_in_out_border, r_target_split, r_target_split);

  // 8. Target vs r_out_in_border [(120, 180)]
  try runTest("intersect", r_target_split, r_out_in_border, Set.init(&.{r(150, 180)}));
  try runTest("intersect", r_out_in_border, r_target_split, Set.init(&.{r(150, 180)}));

  // 9. Target vs r_out_out_border [(120, 180)]
  try runTest("intersect", r_target_split, r_out_out_border, Set.init(&.{r(150, 180)}));
  try runTest("intersect", r_out_out_border, r_target_split, Set.init(&.{r(150, 180)}));


  // --- Exhaustive Permutations (Subtract - 18 permutations) ---

  // 1. Target vs Target
  try runTest("subtract", r_target_split, r_target_split, s_empty);
  try runTest("subtract", r_target_split, r_target_split, s_empty);

  // 2. Target vs r_in_in [(50, 170)]
  try runTest("subtract", r_target_split, r_in_in, Set.init(&.{ r(30, 50), r(170, 180) }));
  try runTest("subtract", r_in_in, r_target_split, Set.init(&.{r(120, 150)})); // The gap

  // 3. Target vs r_in_out [(50, 200)]
  try runTest("subtract", r_target_split, r_in_out, Set.init(&.{r(30, 50)}));
  try runTest("subtract", r_in_out, r_target_split, Set.init(&.{ r(120, 150), r(180, 200) }));

  // 4. Target vs r_out_in [(10, 160)]
  try runTest("subtract", r_target_split, r_out_in, Set.init(&.{r(160, 180)}));
  try runTest("subtract", r_out_in, r_target_split, Set.init(&.{ r(10, 30), r(120, 150) }));

  // 5. Target vs r_out_out [(50, 200)]
  try runTest("subtract", r_target_split, r_out_out, Set.init(&.{r(30, 50)}));
  try runTest("subtract", r_out_out, r_target_split, Set.init(&.{ r(120, 150), r(180, 200) }));

  // 6. Target vs r_in_in_border [(30, 150)]
  try runTest("subtract", r_target_split, r_in_in_border, Set.init(&.{r(150, 180)}));
  try runTest("subtract", r_in_in_border, r_target_split, Set.init(&.{r(120, 150)})); // The gap

  // 7. Target vs r_in_out_border [(30, 180)]
  try runTest("subtract", r_target_split, r_in_out_border, s_empty);
  try runTest("subtract", r_in_out_border, r_target_split, Set.init(&.{r(120, 150)})); // The gap

  // 8. Target vs r_out_in_border [(120, 180)]
  try runTest("subtract", r_target_split, r_out_in_border, Set.init(&.{r(30, 120)}));
  try runTest("subtract", r_out_in_border, r_target_split, Set.init(&.{r(120, 150)})); // The gap

  // 9. Target vs r_out_out_border [(120, 180)]
  try runTest("subtract", r_target_split, r_out_out_border, Set.init(&.{r(30, 120)}));
  try runTest("subtract", r_out_out_border, r_target_split, Set.init(&.{r(120, 150)}));


  // --- Exhaustive Permutations (Subset - 18 permutations) ---

  // 1. Target vs Target (Strict subset check in Lua logic returned false for equality)
  try runTest("subset", r_target_split, r_target_split, false);
  try runTest("subset", r_target_split, r_target_split, false);

  // 2. Target vs r_in_in
  try runTest("subset", r_target_split, r_in_in, false);
  try runTest("subset", r_in_in, r_target_split, false);

  // 3. Target vs r_in_out
  try runTest("subset", r_target_split, r_in_out, false);
  try runTest("subset", r_in_out, r_target_split, false);

  // 4. Target vs r_out_in
  try runTest("subset", r_target_split, r_out_in, false);
  try runTest("subset", r_out_in, r_target_split, false);

  // 5. Target vs r_out_out
  try runTest("subset", r_target_split, r_out_out, false);
  try runTest("subset", r_out_out, r_target_split, false);

  // 6. Target vs r_in_in_border
  try runTest("subset", r_target_split, r_in_in_border, false);
  try runTest("subset", r_in_in_border, r_target_split, false);

  // 7. Target vs r_in_out_border [(30, 180)]
  // Target is strict subset (it lacks the gap 120-150)
  try runTest("subset", r_target_split, r_in_out_border, true);
  try runTest("subset", r_in_out_border, r_target_split, false);

  // 8. Target vs r_out_in_border
  try runTest("subset", r_target_split, r_out_in_border, false);
  try runTest("subset", r_out_in_border, r_target_split, false);

  // 9. Target vs r_out_out_border
  try runTest("subset", r_target_split, r_out_out_border, false);
  try runTest("subset", r_out_out_border, r_target_split, false);


  // --- Exhaustive Permutations (SubsetEq - 18 permutations) ---

  // 1. Target vs Target
  try runTest("subseteq", r_target_split, r_target_split, true);
  try runTest("subseteq", r_target_split, r_target_split, true);

  // 2. Target vs r_in_in
  try runTest("subseteq", r_target_split, r_in_in, false);
  try runTest("subseteq", r_in_in, r_target_split, false);

  // 3. Target vs r_in_out
  try runTest("subseteq", r_target_split, r_in_out, false);
  try runTest("subseteq", r_in_out, r_target_split, false);

  // 4. Target vs r_out_in
  try runTest("subseteq", r_target_split, r_out_in, false);
  try runTest("subseteq", r_out_in, r_target_split, false);

  // 5. Target vs r_out_out
  try runTest("subseteq", r_target_split, r_out_out, false);
  try runTest("subseteq", r_out_out, r_target_split, false);

  // 6. Target vs r_in_in_border
  try runTest("subseteq", r_target_split, r_in_in_border, false);
  try runTest("subseteq", r_in_in_border, r_target_split, false);

  // 7. Target vs r_in_out_border
  try runTest("subseteq", r_target_split, r_in_out_border, true);
  try runTest("subseteq", r_in_out_border, r_target_split, false);

  // 8. Target vs r_out_in_border
  try runTest("subseteq", r_target_split, r_out_in_border, false);
  try runTest("subseteq", r_out_in_border, r_target_split, false);

  // 9. Target vs r_out_out_border
  try runTest("subseteq", r_target_split, r_out_out_border, false);
  try runTest("subseteq", r_out_out_border, r_target_split, false);


  // --- Exhaustive Permutations (Overlap - 18 permutations) ---

  // 1. Target vs Target
  try runTest("overlap", r_target_split, r_target_split, true);
  try runTest("overlap", r_target_split, r_target_split, true);

  // 2. Target vs r_in_in
  try runTest("overlap", r_target_split, r_in_in, true);
  try runTest("overlap", r_in_in, r_target_split, true);

  // 3. Target vs r_in_out
  try runTest("overlap", r_target_split, r_in_out, true);
  try runTest("overlap", r_in_out, r_target_split, true);

  // 4. Target vs r_out_in
  try runTest("overlap", r_target_split, r_out_in, true);
  try runTest("overlap", r_out_in, r_target_split, true);

  // 5. Target vs r_out_out
  try runTest("overlap", r_target_split, r_out_out, true);
  try runTest("overlap", r_out_out, r_target_split, true);

  // 6. Target vs r_in_in_border
  try runTest("overlap", r_target_split, r_in_in_border, true);
  try runTest("overlap", r_in_in_border, r_target_split, true);

  // 7. Target vs r_in_out_border
  try runTest("overlap", r_target_split, r_in_out_border, true);
  try runTest("overlap", r_in_out_border, r_target_split, true);

  // 8. Target vs r_out_in_border
  try runTest("overlap", r_target_split, r_out_in_border, true);
  try runTest("overlap", r_out_in_border, r_target_split, true);

  // 9. Target vs r_out_out_border
  try runTest("overlap", r_target_split, r_out_out_border, true);
  try runTest("overlap", r_out_out_border, r_target_split, true);


  // --- Canonize ---
  // [(1, 5), (5, 8), (0, 1), (15, 20), (13, 17), (1, 5)] -> [(0, 8), (13, 20)]
  const c_input = Set.init(&.{ r(1, 5), r(5, 8), r(0, 1), r(15, 20), r(13, 17), r(1, 5) });
  try runTest("canonize", c_input, null, Set.init(&.{ r(0, 8), r(13, 20) }));
}

fn r(a: u32, b: u32) R32 {
  return .{ .start = a, .end = b };
}

/// Takes two sets (assumed valid), runs ALL api functions on them.
/// For each result, asserts:
/// 1. res.isCanonical() returns true (Structural Check)
/// 2. canonize(res) is identical to res (Mathematical Stability Check)
fn checkCanonized(a: Set, b: Set) !void {
  const gpa = std.testing.allocator;

  // List of operations to test
  const Ops = enum { @"union", intersect, sub_ab, sub_ba };

  inline for (std.meta.fields(Ops)) |op| {
    const res = switch (@field(Ops, op.name)) {
      .@"union" => try Set.unionAlloc(a, b, gpa),
      .intersect => try Set.intersectAlloc(a, b, gpa),
      .sub_ab => try Set.subtractAlloc(a, b, gpa),
      .sub_ba => try Set.subtractAlloc(b, a, gpa),
    };
    defer gpa.free(res.ranges);

    if (!res.isCanonical()) {
      std.debug.print("FAIL ({s}): Result is not canonical.\n", .{op.name});
      return error.TestNotCanonical;
    }

    // 2. Mathematical Stability Check
    // Canonize the result again manually and ensure it didn't change.
    // This proves that the operation output was indeed fully reduced.
    const re_canon = try res.canonizeAlloc(gpa);
    defer gpa.free(re_canon.ranges);

    if (!res.equal(re_canon)) {
      std.debug.print("FAIL ({s}): Output unstable. Re-canonization produced different set.\n", .{op.name});
      return error.TestNotCanonical;
    }
  }
}

test "IntegerSet: Canonization Invariants & Edge Cases" {
  // Helper to quickly make sets from tuples
  const empty = Set.init(&.{});

  // Case 1: Empty Sets
  try checkCanonized(empty, empty);

  // Case 2: Identity (Full vs Empty)
  const single = Set.init(&.{r(0, 10)});
  try checkCanonized(single, empty);
  try checkCanonized(empty, single);

  // Case 3: Identity (Self)
  try checkCanonized(single, single);

  // Case 4: Disjoint (Gap exists)
  const disjoint = Set.init(&.{r(20, 30)});
  try checkCanonized(single, disjoint);

  // Case 5: Abutting (Critical for merge logic)
  // [0, 10) and [10, 20) -> Union must merge to [0, 20)
  const abut = Set.init(&.{r(10, 20)});
  try checkCanonized(single, abut);

  // Case 6: Overlapping (Partial)
  // [0, 10) and [5, 15) -> Union must merge to [0, 15)
  const overlap = Set.init(&.{r(5, 15)});
  try checkCanonized(single, overlap);

  // Case 7: Subset / Nested
  // [0, 20) and [5, 15)
  const big = Set.init(&.{r(0, 20)});
  const small = Set.init(&.{r(5, 15)});
  try checkCanonized(big, small);

  // Case 8: Multi-range gaps
  // A: [0, 10), [20, 30)
  // B: [5, 25) (Bridges the gap)
  const gap_set = Set.init(&.{ r(0, 10), r(20, 30) });
  const bridger = Set.init(&.{r(5, 25)});
  try checkCanonized(gap_set, bridger);

  // Case 9: Complex Interleaving
  // A: [0, 10), [20, 30), [40, 50)
  // B: [5, 25), [35, 45)
  const complex_a = Set.init(&.{ r(0, 10), r(20, 30), r(40, 50) });
  const complex_b = Set.init(&.{ r(5, 25), r(35, 45) });
  try checkCanonized(complex_a, complex_b);

  // Case 10: "Enveloping" (B consumes A completely with margins)
  const inner = Set.init(&.{ r(10, 12), r(14, 16), r(18, 20) });
  const outer = Set.init(&.{r(0, 100)});
  try checkCanonized(inner, outer);

  // Case 11: Single Point Interactions (Ranges of size 1)
  const point_a = Set.init(&.{r(10, 11)});
  const point_b = Set.init(&.{r(11, 12)}); // Abutting points
  try checkCanonized(point_a, point_b);
}
const AsciiSet = IntegerSet(u8);
const Ra = pzre.structures.range.Range(u8);

fn ra(a: u8, b: u8) Ra {
  return .{ .start = a, .end = b };
}

fn testFind(text: []const u8, set_ascii: AsciiSet, set_ext: Set, expected: ?usize, inverted: bool) !void {
  if (inverted) {
    try testing.expectEqual(expected, set_ascii.findNot(u8, text));
    try testing.expectEqual(expected, set_ext.findNot(u8, text));
  } else {
    try testing.expectEqual(expected, set_ascii.find(u8, text));
    try testing.expectEqual(expected, set_ext.find(u8, text));
  }
}

test "IntegerSet: find not (SIMD)" {
  // Construct a set equivalent to: ALPHA, WHITESPACE, '(', ')', ',', '.'
  // ALPHA: 'A' (65) to 'Z' (90), 'a' (97) to 'z' (122)
  // WHITESPACE: 9..14 (\t\n\v\f\r), 32 (space)
  // Punctuation: '(' (40), ')' (41), ',' (44), '.' (46)
  const ranges_ext = comptime &.{
    r(9, 14),   // Whitespace control chars
    r(32, 33),  // Space
    r(40, 42),  // ( )
    r(44, 45),  // ,
    r(46, 47),  // .
    r(65, 91),  // A-Z
    r(97, 123), // a-z
  };
  const ranges_ascii = comptime &.{
    ra(9, 14),   // Whitespace control chars
    ra(32, 33),  // Space
    ra(40, 42),  // ( )
    ra(44, 45),  // ,
    ra(46, 47),  // .
    ra(65, 91),  // A-Z
    ra(97, 123), // a-z
  };
  
  const set_ascii = comptime AsciiSet.init(ranges_ascii).canonizeComptime();
  const set_ext = comptime Set.init(ranges_ext).canonizeComptime();

  { // 1. Success Case (Long Text)
    const text = 
      \\YEP This is a test of letter and ONLY letters, (even some commas and parenthesis)
      \\This text will continue for some time and then end unfortunately, I just have to make sure this actually
      \\ triggers the SIMD version of the subset function, and I think now it does.
      \\Yay
    ;
    try testFind(text, set_ascii, set_ext, null, true);
  }

  { // 2. Failure at Start (Index 0)
    const text = "1YEP This is a test...";
    try testFind(text, set_ascii, set_ext, 0, true);
  }

  { // 3. Failure in Middle
    const text = "This is a valid start but then comes a number 1 inside.";
    try testFind(text, set_ascii, set_ext, 46, true);
  }

  { // 4. Failure at End
    const text = "This ends with a number 5";
    try testFind(text, set_ascii, set_ext, 24, true);
  }

  { // 5. Boundary Checks (16/17/32 bytes)
    const t16 = "SixteenBytesText";
    try testFind(t16, set_ascii, set_ext, null, true);

    const m16 = "SixteenBytesTex1";
    try testFind(m16, set_ascii, set_ext, 15, true);

    const t17 = "SeventeenBytesTxt";
    try testFind(t17, set_ascii, set_ext, null, true);

    const m17 = "SeventeenBytesTx1";
    try testFind(m17, set_ascii, set_ext, 16, true);

    const t32 = "ThirtyTwoBytesTextForSimdTesting";
    try testFind(t32, set_ascii, set_ext, null, true);

    const f32m = "ThirtyTwoBytesTe1tForSimdTesting"; 
    try testFind(f32m, set_ascii, set_ext, 16, true);
  }

  { // 6. Tiny inputs (Scalar fallback check)
    try testFind("Hi", set_ascii, set_ext, null, true);
    try testFind("1", set_ascii, set_ext, 0, true);
    try testFind("a1", set_ascii, set_ext, 1, true);
  }
}

test "IntegerSet: find (SIMD)" {
  // Construct a set of DIGITS only.
  // 0-9 (48-57)
  const ranges_ext = comptime &.{
    r(48, 58),
  };

  const ranges_ascii = comptime &.{
    ra(48, 58),
  };

  const set_ascii = comptime AsciiSet.init(ranges_ascii).canonizeComptime();
  const set_ext = comptime Set.init(ranges_ext).canonizeComptime();

  { // 1. No Term (Long Text)
    const text = 
      \\This is a text with absolutely no numbers in it.
      \\It checks that the find function returns null correctly.
      \\..........
    ;
    try testFind(text, set_ascii, set_ext, null, false);
  }

  { // 2. Match at Start (Index 0)
    const text = "1st place";
    try testFind(text, set_ascii, set_ext, 0, false);
  }

  { // 3. Match in Middle
    const text = "The year is 2024";
    try testFind(text, set_ascii, set_ext, 12, false);
  }

  { // 4. Match at End
    const text = "Countdown: 5";
    try testFind(text, set_ascii, set_ext, 11, false);
  }

  { // 5. Multiple Term (Must find FIRST)
    const text = "a1b2c3";
    try testFind(text, set_ascii, set_ext, 1, false);
  }

  { // 6. Boundary Checks (16/17/32 bytes)
    // Ensure SIMD overlaps don't double count or miss indices

    const m16 = "FifteenCharsTx1"; 
    try testFind(m16, set_ascii, set_ext, 14, false);

    const m17 = "SixteenCharsTxt1";
    try testFind(m17, set_ascii, set_ext, 15, false);

    const m18 = "abajiegiajeijawb2";
    try testFind(m18, set_ascii, set_ext, 16, false);

    const m32 = "ThisTextIsExactlyThirtyTwoBytes1";
    try testFind(m32, set_ascii, set_ext, 31, false);
  }

  { // 7. Tiny inputs (Scalar fallback check)
    try testFind("No", set_ascii, set_ext, null, false);
    try testFind("1", set_ascii, set_ext, 0, false);
    try testFind("a1", set_ascii, set_ext, 1, false);
  }
}

test "IntegerSet: toSequence" {
  const gpa = std.testing.allocator;

  comptime {
    const a = Set.init(&.{r(10, 12)});
    const seq2 = a.toSequenceComptime(u32);
    try testing.expectEqualSlices(u32, &.{10, 11}, seq2);
  }
  {
    const a = Set.init(&.{r(10, 12)});
    const seq = try a.toSequence(u32, gpa);
    defer gpa.free(seq);
    try testing.expectEqualSlices(u32, &.{10, 11}, seq);
  }
}

test "IntegerSet: shift" {
  const gpa = std.testing.allocator;

  const original = Set.init(&.{ r(10, 20), r(30, 40) });

  // Add shift
  const shifted_up = try original.shiftAlloc(.add, 5, gpa);
  defer gpa.free(shifted_up.ranges);
  try expectEqualSets(Set.init(&.{ r(15, 25), r(35, 45) }), shifted_up);

  // Sub shift
  const shifted_down = try original.shiftAlloc(.sub, 5, gpa);
  defer gpa.free(shifted_down.ranges);
  try expectEqualSets(Set.init(&.{ r(5, 15), r(25, 35) }), shifted_down);

  const new = comptime Set.init(&.{ r(10, 20), r(30, 40) });

  // Comptime shift
  const comp_shifted = comptime new.shiftComptime(.add, 10);
  try expectEqualSets(Set.init(&.{ r(20, 30), r(40, 50) }), comp_shifted);
  
  // Case folding simulation (a-z to A-Z)
  const lower = Set.init(&.{ r(97, 123) });
  const upper = try lower.shiftAlloc(.sub, 32, gpa);
  defer gpa.free(upper.ranges);
  try expectEqualSets(Set.init(&.{ r(65, 91) }), upper);
}
