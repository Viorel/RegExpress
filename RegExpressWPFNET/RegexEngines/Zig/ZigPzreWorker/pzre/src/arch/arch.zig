const std = @import("std");
const Allocator = std.mem.Allocator;
const assert = std.debug.assert;

const pzre = @import("../root.zig");

const strategy = pzre.compile.strategy;

const polymorphic_memory = pzre.structures.polymorphic_memory;
const MemoryModel = pzre.structures.polymorphic_memory.MemoryModel;
const Limits = pzre.compile.Limits;
const compile = pzre.compile;
const debug = pzre.lens.debug;

const dispatch = pzre.compile.dispatch;
const Manifest = pzre.ast.Manifest;
const ManifestField = pzre.ast.ManifestField;
const Bid = dispatch.Bid;
const Ast = pzre.Ast;
const Set = pzre.encoding.ascii.IntegerSet;

pub const search = @import("search.zig");
pub const minimal_nfa = @import("minimal_nfa/nfa.zig");
pub const context = @import("context.zig");
pub const Context = context.Context;
pub const Cache = context.Cache;
pub const WarmupError = context.WarmupError;
pub const CacheConfig = context.CacheConfig;
pub const maxStates = context.maxStates;

comptime {
  if (@import("builtin").is_test) {
    _ = @import("search.zig");
    _ = @import("context.zig");
    _ = @import("minimal_nfa/nfa.zig");
  }
}

const arch = @This();
pub const context_field_name = "context";

pub fn archUniverseSweep(comptime ctx: context.Mode) []const Arch {
  comptime return pzre.meta.unionDiscreetSweep(Arch, .{ctx});
}

/// A union over all implemented architectures
/// User facing Arch
/// Needs to be resolved before the system can use it
pub const Arch = union(enum) {
  minimal_nfa: minimal_nfa.Config,

  const Self = @This();
 
  fn call(comptime self: Self, comptime fn_name: []const u8, args: anytype, comptime R: type) R {
    const arch_folder = @field(arch, @tagName(self));
    const f = @field(arch_folder, fn_name);
    return @call(.auto, f, args);
  }
 
  /// Returns the strategies this arch can support
  pub fn strategies(comptime self: Self) std.EnumSet(strategy.Name) {
    return switch (self) {
      inline else => return self.call("strategies", .{}, std.EnumSet(strategy.Name)),
    };
  }

  pub fn resolveWithManifest(comptime self: Self, comptime compile_config: compile.Config, comptime manifest: pzre.ast.Manifest) ArchResolved {
    return switch (self) {
      .minimal_nfa => |user_conf| ArchResolved{ .minimal_nfa = user_conf.resolveWithManifest(compile_config, manifest) },
    };
  }
 
  pub fn resolve(comptime self: Self) ArchResolved {
    return switch (self) {
      .minimal_nfa => |user_conf| ArchResolved{ .minimal_nfa = user_conf.resolve() },
    };
  }
 
  pub fn resolutionRequiresManifest(comptime self: Self) bool {
    return switch (self) {
      .minimal_nfa => |user_conf| user_conf.resolutionRequiresManifest(),
    };
  }
 
  /// Returns the manifest fields the architecture requires for bidding
  pub fn interests(comptime self: Self, comptime compile_config: compile.Config) std.EnumSet(ManifestField) {
    comptime return switch (self) {
      inline else => |conf| self.call("interests", .{conf, compile_config}, std.EnumSet(ManifestField)),
    };
  }
};

pub const ArchResolved = union (enum) {
  minimal_nfa: minimal_nfa.ConfigResolved,

  const Self = @This();

  fn call(comptime self: Self, comptime fn_name: []const u8, args: anytype, comptime R: type) R {
    const arch_folder = @field(arch, @tagName(self));
    const f = @field(arch_folder, fn_name);
    return @call(.auto, f, args);
  }
 
  /// Returns the strategies this arch can support
  pub fn strategies(comptime self: Self) std.EnumSet(strategy.Name) {
    return switch (self) {
      inline else => return self.call("strategies", .{}, std.EnumSet(strategy.Name)),
    };
  }

  /// How well the architecture can solve the problem layed out in manifest
  pub fn bid(comptime self: Self, comptime sets_bp: arch.AbsoluteBreakpoint, manifest: Manifest, user_strat: ?strategy.Name) ?Bid {
    return switch (self) {
      inline else => |conf| return self.call("bid", .{conf, sets_bp, manifest, user_strat}, ?Bid),
    };
  }

  /// Builds this specific architecture:
  /// - given the pattern AST and sets
  /// - using a specific memory model
  /// - using the solution strategy
  ///
  /// Grabs ownership over SETS. Do not cover this call with SETS DESTRUCTION
  /// 
  pub fn build(
    comptime self: Self,
    comptime limits: Limits,
    comptime model: MemoryModel,
    comptime problem_bp: AbsoluteBreakpoint,
    comptime sets_bp: AbsoluteBreakpoint,
    gpa: Allocator,
    artifacts: *compile.Artifacts,
    word_set: Set,
    strat: strategy.Name,
  ) compile.Error!self.Internals(problem_bp, sets_bp) {
    const internals = try self.Internals(problem_bp, sets_bp).build(limits, model, gpa, artifacts, word_set, strat);
    return internals;
  }

  /// How well the architecture can solve the problem layed out in manifest
  pub fn contextMode(comptime self: Self) ?context.Mode {
    comptime return switch (self) {
      inline else => |conf| return self.call("contextMode", .{conf}, ?context.Mode),
    };
  }

  /// Returns the manifest fields the architecture requires for bidding
  pub fn interests(comptime self: Self, comptime compile_config: compile.Config) std.EnumSet(ManifestField) {
    comptime return switch (self) {
      inline else => |conf| self.call("interests", .{conf, compile_config}, std.EnumSet(ManifestField)),
    };
  }

  /// Returns the Context type
  pub fn Internals(
    comptime self: Self,
    comptime problem_bp: AbsoluteBreakpoint,
    comptime sets_bp: arch.AbsoluteBreakpoint,
  ) type {
    comptime return switch (self) {
      inline else => |conf| self.call("Internals", .{conf, problem_bp, sets_bp}, type),
    };
  }

  /// Returns the Context parameter type
  pub fn ContextData(comptime self: Self) type {
    comptime return switch (self) {
      inline else => |conf| self.call("ContextData", .{conf}, type),
    };
  }
};

/// Asserts that all variants of the same base architecture share the exact same context mode.
/// Passing multiple context modes for the same base architecture is redundant and causes 
/// unnecessary memory bloat in the composite Regex.Context struct.
pub fn assertUniqueContextsPerArch(comptime archs: []const ArchResolved) void {
  comptime {
    for (archs, 0..) |arch_a, i| {
      const tag_a = std.meta.activeTag(arch_a);
      const config_a = @field(arch_a, @tagName(tag_a));
      
      if (!@hasField(@TypeOf(config_a), context_field_name)) continue;
      const ctx_a: context.ModeResolved = config_a.context;

      for (archs[i + 1 ..]) |arch_b| {
        const tag_b = std.meta.activeTag(arch_b);
        
        if (tag_a == tag_b) {
          const config_b = @field(arch_b, @tagName(tag_b));
          const ctx_b = config_b.context;
          
          if (!ctx_a.eql(ctx_b)) {
            @compileError(
              "Redundant context definitions detected for architecture: " ++ 
              @tagName(tag_a) ++ 
              ". All variants of the same base architecture must share the exact same context mode."
            );
          }
        }
      }
    }
  }
}

/// How big do indexing integers have to be
pub const AbsoluteBreakpoint = enum {
  u8,
  u16,
  u32,
  u64,

  const Self = @This();

  pub fn max_states(comptime rbp: Self) usize {
    return std.math.maxInt(rbp.Index());
  }

  pub fn Index(comptime rbp: Self) type {
    comptime return switch (rbp) {
      .u8 => u8,
      .u16 => u16,
      .u32 => u32,
      .u64 => u64,
    };
  }

  pub fn toRelative(comptime rbp: Self) RelativeBreakpoint {
    comptime return switch (rbp) {
      .u8 => .i16,
      .u16 => .i32,
      .u32 => .i64,
      .u64 => @compileError("Cannot safely convert u64 absolute breakpoint to relative; exceeds i64 capacity"),
    };
  }

  pub fn define(comptime states_len: usize) Self {
    comptime {
      if (states_len <= std.math.maxInt(u8)) return .u8;
      if (states_len <= std.math.maxInt(u16)) return .u16;
      if (states_len <= std.math.maxInt(u32)) return .u32;
      if (states_len <= std.math.maxInt(u64)) return .u64;
      @compileError("Defined states_len is too large. Upperbound is std.math.maxInt(u64)");
    }
  }
};

/// How big do offset integers have to be
/// These are jumps. And they need to cover the entire list length
/// 
/// an i8 cannot cover an u8 list
pub const RelativeBreakpoint = enum {
  i8,
  i16,
  i32,
  i64,

  const Self = @This();

  pub fn max_states(comptime rbp: Self) usize {
    return std.math.maxInt(rbp.Offset());
  }

  pub fn Offset(comptime rbp: Self) type {
    comptime return switch (rbp) {
      .i8 => i8,
      .i16 => i16,
      .i32 => i32,
      .i64 => i64,
    };
  }

  pub fn toAbsolute(comptime rbp: Self) AbsoluteBreakpoint {
    comptime return switch (rbp) {
      .i8 => .u8,
      .i16 => .u16,
      .i32 => .u32,
      .i64 => .u64,
    };
  }

  pub fn define(comptime states_len: usize) Self {
    comptime {
      if (states_len <= std.math.maxInt(i8)) return .i8;
      if (states_len <= std.math.maxInt(i16)) return .i16;
      if (states_len <= std.math.maxInt(i32)) return .i32;
      if (states_len <= std.math.maxInt(i64)) return .i64;
      @compileError("Defined states_len is too large. Upperbound is std.math.maxInt(i64)");
    }
  }
};

pub fn integer_utils(comptime Idx: type, comptime Offset: type) type {
  return struct {
    /// Applies a signed intra-submachine jump `offset` to an absolute state
    /// index `base`, returning the destination index in `Idx` space.
    ///
    /// This is the hot-path address computation: every state transition runs it,
    /// so it must be branchless. It abuses a hard invariant
    /// from the linker to stay a couple of integer ops.
    ///
    /// ---- THE INVARIANT (supplied by the linker, never checked here) ----
    /// All jumps stay inside the currently executing submachine, and the context
    /// is reset between submachines, so a submachine of size N occupies indices
    /// [0, N-1]. For any state at `base` in [0, N-1], `alt_jump` is constrained
    /// such that `base + offset` is ALWAYS another valid index in [0, N-1]:
    ///   - the largest forward jump is +(N-1)  (from base 0 to the last state)
    ///   - the largest backward jump is -(N-1) (from the last state back to 0)
    /// A jump can never address a state outside [0, N-1]. If it ever did, the
    /// linker is bugged; this function does not defend against that.
    ///
    /// ---- THE TWO WIDTHS, AND WHY THEY DIFFER ----
    /// `Idx`    : the UNSIGNED context index type. Chosen as the smallest type
    ///            that can hold N, i.e. AbsoluteBreakpoint.define(N). It spans
    ///            [0, 2^bits(Idx) - 1], which always covers [0, N-1].
    /// `Offset` : the SIGNED jump type. Chosen as the smallest type whose range
    ///            covers the magnitude (N-1), i.e. RelativeBreakpoint.define(N).
    ///            Because it is signed and must hold +/-(N-1), it is frequently
    ///            WIDER than `Idx`:
    ///              N = 200 -> Idx = u8  (200 fits in 0..255)
    ///                      -> Offset = i16 (199 does NOT fit in i8's 0..127)
    /// The two types are sized against different things (an unsigned capacity vs
    /// a signed magnitude), so NO fixed ordering between bits(Idx) and
    /// bits(Offset) holds. Both relationships occur in practice:
    ///   - Offset WIDER  than Idx: e.g. fixed=200  -> u8  index, i16 offset
    ///   - Offset NARROWER than Idx: e.g. a small submachine living in a wide
    ///     shared/declared context (.dynamic = .u16) -> u16 index, i8 offset
    /// castAltPath must be correct for either, with no information loss.
    ///
    /// ---- WHY MODULAR (mod 2^bits(Idx)) ARITHMETIC IS EXACT HERE ----
    /// The true destination D = base + offset is guaranteed (by the invariant)
    /// to be in [0, N-1], and N <= 2^bits(Idx). So D is representable in `Idx`
    /// and equals `(base + offset) mod 2^bits(Idx)`. That means we may reduce the
    /// whole computation modulo 2^bits(Idx) at every step without changing the
    /// result, because the final answer is already in range. This is what lets us
    /// truncate a wider `Offset` down to `Idx` width before adding:
    ///   - reinterpret/sign-fit the offset into `Idx` bits (its low bits ARE
    ///     `offset mod 2^bits(Idx)`, including the correct two's-complement
    ///     pattern for negative jumps),
    ///   - wrapping-add to `base`.
    /// Example, fixed=200, Idx=u8, Offset=i16, base=0, offset=+199:
    ///   +199 truncated to 8 bits = 0xC7 (= -57 as i8, = 199 as u8). Whether you
    ///   read it as -57 or +199, base +% it = 199 (mod 256) = the real target.
    /// Example, base=199, offset=-199 (Idx=u8):
    ///   -199 mod 256 = 0x39 = 57; 199 +% 57 = 256 mod 256 = 0 = the real target.
    /// The intermediate `i8`/`u8` value may look "wrong" in isolation; it is the
    /// correct residue mod 256, and since the true target is in range, the wrap
    /// recovers it exactly. This is only valid BECAUSE N <= 2^bits(Idx); it would
    /// silently corrupt if a jump could land outside [0, 2^bits(Idx)-1], which
    /// the invariant forbids.
    ///
    /// ---- THE IMPLEMENTATION ----
    /// `@truncate(offset)` reduces `Offset` to `Idx` width (a no-op when Offset is
    /// narrower or equal; a low-bits keep when wider). The result is reinterpreted
    /// as the signed-`Idx`-width pattern via the SignedIdx alias purely so the
    /// bit pattern is unambiguous, then `@bitCast` to unsigned `Idx`, then
    /// wrapping-added. `+%` (not `+`) is mandatory: backward jumps produce a
    /// large unsigned addend whose wrap is the subtraction we want; plain `+`
    /// would trip overflow checks.
    pub inline fn castAltPath(base: Idx, offset: Offset) Idx {
      const SignedIdx = @Int(.signed, @bitSizeOf(Idx));
      const widened: Idx = @bitCast(@as(SignedIdx, @truncate(offset)));
      return base +% widened;
    }
  };
}

/// Returns true if 'a' fully covers 'b'
pub fn hasCoverageOver(comptime a: anytype, comptime b: anytype) bool {
  return coverageOf(a) >= coverageOf(b);
}

fn coverageOf(comptime bp: anytype) comptime_int {
  const T = @TypeOf(bp);
  if (T == AbsoluteBreakpoint) {
    return std.math.maxInt(bp.Index());
  } else if (T == RelativeBreakpoint) {
    return std.math.maxInt(bp.Offset());
  } else {
    @compileError("Expected AbsoluteBreakpoint or RelativeBreakpoint");
  }
}
