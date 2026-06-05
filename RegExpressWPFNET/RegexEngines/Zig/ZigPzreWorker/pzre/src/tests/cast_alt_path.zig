//! Exhaustive tests for `arch.integer_utils(Idx, Offset).castAltPath`.
//!
//! castAltPath is the hot-path jump resolver: given an absolute state index
//! `base` and a signed intra-submachine jump `offset`, it returns the
//! destination index. It deliberately uses wrapping modular arithmetic and
//! abuses a linker invariant, so its correctness is non-obvious and worth
//! pinning down directly rather than hoping a matched pattern happens to emit
//! a large jump.
//!
//! These tests do NOT go through the regex harness. castAltPath is a pure
//! integer function, so we test it directly against a wide-signed oracle over
//! every input the system can actually produce.
//!
//! ---- WHAT "REACHABLE" MEANS ----
//! For a submachine of size N:
//!   Idx    = AbsoluteBreakpoint.define(N).Index()   (smallest unsigned >= N)
//!   Offset = RelativeBreakpoint.define(N).Offset()  (smallest signed >= N-1)
//! The linker guarantees base in [0, N-1], offset in [-(N-1), +(N-1)], and
//! base + offset in [0, N-1]. We only feed inputs inside that invariant; the
//! function is explicitly not required to behave outside it.
//!
//! ---- THE SOUNDNESS HINGE ----
//! The current implementation does `@as(SignedIdx, @truncate(offset))`, which
//! only compiles/behaves when bits(Offset) >= bits(Idx). We assert at comptime
//! that every N produces such a pairing (test "invariant: offset never narrower
//! than index"). If a future change ever derives an offset narrower than its
//! index (e.g. a small submachine in a declared-wide context), that test fails
//! at compile time -- turning a latent silent-corruption landmine into a build
//! error at the exact moment it becomes reachable.

const std = @import("std");
const pzre = @import("../root.zig");

const AbsoluteBreakpoint = pzre.arch.AbsoluteBreakpoint;
const RelativeBreakpoint = pzre.arch.RelativeBreakpoint;
const integer_utils = pzre.arch.integer_utils;

const expect = std.testing.expect;
const expectEqual = std.testing.expectEqual;

/// The wide-signed oracle: the mathematically correct destination, computed in
/// a type that cannot overflow for any input we test. castAltPath must equal
/// this for every in-invariant (base, offset).
fn oracle(comptime Idx: type, base: Idx, offset: i128) Idx {
  const truth: i128 = @as(i128, base) + offset;
  // Caller only ever passes in-invariant inputs, so the truth is a valid Idx.
  return @intCast(truth);
}

/// Resolve the real (Idx, Offset) types the system would pick for a submachine
/// of size N, then exhaustively (or, for large N, boundary-sample) verify
/// castAltPath against the oracle over the full linker-invariant input set.
fn checkSize(comptime N: usize) !void {
  const Idx = comptime AbsoluteBreakpoint.define(N).Index();
  const Offset = comptime RelativeBreakpoint.define(N).Offset();
  const cast = integer_utils(Idx, Offset).castAltPath;

  // The soundness precondition the implementation relies on.
  comptime std.debug.assert(@bitSizeOf(Offset) >= @bitSizeOf(Idx));

  const last: usize = N - 1; // highest valid index

  // For small N, enumerate EVERY valid (base, offset) pair. For large N this
  // is too many, so we sample the boundaries where wrapping behavior changes:
  // base at {0, 1, mid, last-1, last}, offset spanning its full legal range
  // but sampled at the extremes and around zero.
  const exhaustive = N <= 512;

  if (exhaustive) {
    var base: usize = 0;
    while (base <= last) : (base += 1) {
      // offset must keep base + offset in [0, last]
      const lo: i128 = -@as(i128, @intCast(base));
      const hi: i128 = @as(i128, @intCast(last - base));
      var off: i128 = lo;
      while (off <= hi) : (off += 1) {
        const b: Idx = @intCast(base);
        const o: Offset = @intCast(off);
        const got = cast(b, o);
        const want = oracle(Idx, b, off);
        if (got != want) {
          std.debug.print(
            "castAltPath mismatch: N={} Idx={s} Offset={s} base={} offset={} got={} want={}\n",
            .{ N, @typeName(Idx), @typeName(Offset), base, off, got, want },
          );
          return error.CastMismatch;
        }
      }
    }
    return;
  }

  // Boundary sampling for large N.
  const bases = [_]usize{ 0, 1, N / 2, last - 1, last };
  for (bases) |base| {
    const lo: i128 = -@as(i128, @intCast(base));
    const hi: i128 = @as(i128, @intCast(last - base));
    // Sample offsets: the extreme legal jumps, +/-1, 0, and the max-magnitude
    // jumps the breakpoint type itself can express (still clamped to invariant).
    const candidates = [_]i128{
      lo, lo + 1, -1, 0, 1, hi - 1, hi,
    };
    for (candidates) |off| {
      if (off < lo or off > hi) continue;
      const b: Idx = @intCast(base);
      const o: Offset = @intCast(off);
      const got = cast(b, o);
      const want = oracle(Idx, b, off);
      if (got != want) {
        std.debug.print(
          "castAltPath mismatch (sampled): N={} Idx={s} Offset={s} base={} offset={} got={} want={}\n",
          .{ N, @typeName(Idx), @typeName(Offset), base, off, got, want },
        );
        return error.CastMismatch;
      }
    }
  }
}

// ───────────────────────────────────────────────────────────────────────────
// The soundness invariant, checked at comptime for every breakpoint boundary.
// ───────────────────────────────────────────────────────────────────────────

test "castAltPath: invariant, derived offset is never narrower than derived index" {
  // If this ever fails, the @truncate-based castAltPath is unsound for that N:
  // @truncate cannot widen, and naive widening would zero-extend (corrupting
  // negative jumps). This is the exact configuration to forbid.
  @setEvalBranchQuota(100_000);
  const sizes = [_]usize{
    1,     2,     3,     63,    64,    126,   127,   128,   129,
    199,   200,   254,   255,   256,   257,   1000,  32766, 32767,
    32768, 32769, 65534, 65535, 65536, 65537, 1 << 20, 1 << 31, (1 << 32) - 1, 1 << 32,
  };
  inline for (sizes) |N| {
    const Idx = comptime AbsoluteBreakpoint.define(N).Index();
    const Offset = comptime RelativeBreakpoint.define(N).Offset();
    comptime std.debug.assert(@bitSizeOf(Offset) >= @bitSizeOf(Idx));
  }
}

// ───────────────────────────────────────────────────────────────────────────
// Exhaustive correctness at and around every breakpoint boundary (small N:
// every valid (base, offset); the most bug-prone region since the offset type
// transitions width here).
// ───────────────────────────────────────────────────────────────────────────

test "castAltPath: exhaustive, tiny submachines (1..3)" {
  inline for ([_]usize{ 1, 2, 3 }) |N| try checkSize(N);
}

test "castAltPath: exhaustive across the i8/i16 offset boundary (126..129)" {
  // N=127: Offset still i8 (max +-126). N=128: Offset widens to i16 while Idx
  // stays u8. This is the wider-offset case the comment calls out, where
  // truncation must remain exact via mod-256 arithmetic.
  inline for ([_]usize{ 126, 127, 128, 129 }) |N| try checkSize(N);
}

test "castAltPath: exhaustive across the u8 capacity boundary (199, 200, 255)" {
  // N=200 is the documented worked example (u8 index, i16 offset, jumps +-199).
  inline for ([_]usize{ 199, 200, 255 }) |N| try checkSize(N);
}

test "castAltPath: exhaustive just past u8 (256, 257, 300, 512)" {
  // Idx widens to u16 here; Offset stays i16. bits(Offset) == bits(Idx).
  inline for ([_]usize{ 256, 257, 300, 512 }) |N| try checkSize(N);
}

// ───────────────────────────────────────────────────────────────────────────
// Boundary-sampled correctness for large submachines (full enumeration is
// infeasible; we hit the extremes where wrapping is most likely to be wrong).
// ───────────────────────────────────────────────────────────────────────────

test "castAltPath: sampled across the i16/i32 offset boundary (32767, 32768, 32769)" {
  inline for ([_]usize{ 32767, 32768, 32769 }) |N| try checkSize(N);
}

test "castAltPath: sampled across the u16 capacity boundary (65535, 65536, 65537)" {
  inline for ([_]usize{ 65535, 65536, 65537 }) |N| try checkSize(N);
}

test "castAltPath: sampled large submachines (1<<20, 1<<24)" {
  inline for ([_]usize{ 1 << 20, 1 << 24 }) |N| try checkSize(N);
}

// ───────────────────────────────────────────────────────────────────────────
// Targeted extreme-jump cases: the largest legal forward and backward jumps,
// which exercise the full wrap. These are the jumps most able to crash an
// indexing bug (they were the source of the historical `index 256` panic).
// ───────────────────────────────────────────────────────────────────────────

test "castAltPath: maximal forward jump from index 0 lands on the last state" {
  inline for ([_]usize{ 2, 3, 127, 128, 200, 255, 256, 1000, 65535, 65536 }) |N| {
    const Idx = comptime AbsoluteBreakpoint.define(N).Index();
    const Offset = comptime RelativeBreakpoint.define(N).Offset();
    const cast = integer_utils(Idx, Offset).castAltPath;

    const base: Idx = 0;
    const offset: Offset = @intCast(N - 1); // +(N-1)
    try expectEqual(@as(Idx, @intCast(N - 1)), cast(base, offset));
  }
}

test "castAltPath: maximal backward jump from the last state lands on index 0" {
  inline for ([_]usize{ 2, 3, 127, 128, 200, 255, 256, 1000, 65535, 65536 }) |N| {
    const Idx = comptime AbsoluteBreakpoint.define(N).Index();
    const Offset = comptime RelativeBreakpoint.define(N).Offset();
    const cast = integer_utils(Idx, Offset).castAltPath;

    const base: Idx = @intCast(N - 1);
    const offset: Offset = -@as(Offset, @intCast(N - 1)); // -(N-1)
    try expectEqual(@as(Idx, 0), cast(base, offset));
  }
}

test "castAltPath: zero offset is identity for representative sizes" {
  inline for ([_]usize{ 1, 127, 128, 200, 256, 65536 }) |N| {
    const Idx = comptime AbsoluteBreakpoint.define(N).Index();
    const Offset = comptime RelativeBreakpoint.define(N).Offset();
    const cast = integer_utils(Idx, Offset).castAltPath;

    const probes = [_]usize{ 0, 1, N / 2, N - 1 };
    for (probes) |p| {
      const b: Idx = @intCast(p);
      try expectEqual(b, cast(b, 0));
    }
  }
}
