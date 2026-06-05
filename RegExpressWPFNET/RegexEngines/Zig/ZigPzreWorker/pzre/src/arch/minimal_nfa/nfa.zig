//! An extremely minimal and optimal matching machine with limited features

const std = @import("std");
const Allocator = std.mem.Allocator;
const Io = std.Io;

const assert = std.debug.assert;
const pzre = @import("../../root.zig");
const arch = pzre.arch;
const ArchResolved = pzre.arch.ArchResolved;
const RelativeBreakpoint = arch.RelativeBreakpoint;
const dispatch = pzre.compile.dispatch;
const Manifest = pzre.ast.Manifest;
const ManifestField = pzre.ast.ManifestField;
const Bid = dispatch.Bid;
const AbsoluteBreakpoint = pzre.arch.AbsoluteBreakpoint;
const Ast = pzre.Ast;
const MemoryModel = pzre.MemoryModel;
const Limits = pzre.compile.Limits;
const compile = pzre.compile;

const lens = pzre.lens;
const debug = lens.debug;
const regex = pzre.regex;
const search = arch.search;
const context = pzre.arch.context;

const strategy = compile.strategy;

const ascii = pzre.encoding.ascii;
const Set = ascii.IntegerSet;

const misc = pzre.misc;
pub const lists = @import("lists.zig");
pub const state = @import("state.zig");
pub const fragment = @import("fragment.zig");
pub const linker = @import("linker.zig");
pub const analysis = @import("analysis.zig");

pub const Fragment = fragment.Fragment;

comptime {
  if (@import("builtin").is_test) {
    _ = @import("fragment.zig");
    _ = @import("lists.zig");
    _ = @import("state.zig");
    _ = @import("analysis.zig");
    _ = @import("linker.zig");
  }
}

pub const Config = struct {
  /// The maximum size a submachine is allowed to be
  /// 
  /// Defines the offset (signed) integer type and by extension the State size. 
  /// This is the arch-local breakpoint, and it has nothing to do with context shareability
  /// 
  /// The larger the jumps in the statemachine, the larger the offset_bp has to be
  /// 
  /// If left null, then its value is interpreted from the context type
  ///   - fixed contexts: smallest unsigned integer that can span the entire length
  ///   - dynamic: safe offset to index conversion, e.g. u8 -> i16
  /// 
  /// Useful to set manually when you wish to declare a wider context for context reuse. For example, you 
  /// might have many different machines, some large, some small. And define a fixed context of 512 that is 
  /// valid between all machines, and then set i8 for the smaller patterns. Similar though process for 
  /// dynamic contexts.
  /// 
  /// There is no strict invariant with offset_bp vs context.breakpoint. See arch.zig: castAltPath
  /// 
  /// Returns error.TooManyStates on violation
  offset_bp: ?RelativeBreakpoint = null,
  /// Defines the type of context being used
  ///
  context: context.Mode = .{ .dynamic = .u8 },

  // TODO: compute proper offset_bp with manifest: write AST algorithm to detect the maximum jump size

  /// Resolves when pattern is known at comptime
  pub fn resolveWithManifest(comptime config: Config, comptime compile_config: compile.Config, comptime manifest: pzre.ast.Manifest) ConfigResolved {
    comptime {
      const ctx = out: switch (config.context) {
        .dynamic, => |c| context.ModeResolved{.dynamic = c},
        .fixed, => |c| context.ModeResolved{.fixed = c},
        .compact_fixed => {
          const strat = if (compile_config.strategy) |strat| strat else strategyFromRouting(manifest.routing);
          break :out context.ModeResolved{ .fixed = maxSubmachineSize(manifest, strat) };
        },
      };

      const offset_bp = if (config.offset_bp) |bp| bp else switch (ctx) {
        .dynamic => |d| d.toRelative(),
        .fixed => |c| arch.RelativeBreakpoint.define(c),
      };

      return .{ .context = ctx, .offset_bp = offset_bp };
    }
  }
 
  /// Resolves when pattern is not known at comptime
  pub fn resolve(comptime config: Config) ConfigResolved {
    comptime {
      const ctx = switch (config.context) {
        .dynamic, => |c| context.ModeResolved{.dynamic = c},
        .fixed, => |c| context.ModeResolved{.fixed = c},
        .compact_fixed => @compileError("Compact fixed context resolution not available at runtime"),
      };

      const offset_bp = if (config.offset_bp) |bp| bp else switch (ctx) {
        .dynamic => |d| d.toRelative(),
        .fixed => |c| arch.RelativeBreakpoint.define(c),
      };

      return .{ .context = ctx, .offset_bp = offset_bp };
    }
  }
 
  /// Whether resolution can be performed immediately, or if resolution requires partial execution of the 
  /// compilation pipeline
  pub fn resolutionRequiresManifest(comptime config: Config) bool {
    comptime {
      return config.context == .compact_fixed;
    }
  }
};

/// The sub of all submachines
pub fn statesLength(manifest: pzre.ast.Manifest, strat: strategy.Name) usize {
  const nfa_len = if (!strat.requires(.nfa)) 0 else manifest.nfa_states_count.?;
  const rnfa_len = if (!strat.requires(.rnfa)) 0 else manifest.rnfa_states_count.?;
  const unfa_len = if (!strat.requires(.unfa)) 0 else nfa_len + 2;
  return nfa_len + rnfa_len + unfa_len;
}

/// Maximum submachine size
pub fn maxSubmachineSize(manifest: pzre.ast.Manifest, strat: strategy.Name) usize {
  const nfa_len = if (!strat.requires(.nfa)) 0 else manifest.nfa_states_count.?;
  const rnfa_len = if (!strat.requires(.rnfa)) 0 else manifest.rnfa_states_count.?;
  const unfa_len = if (!strat.requires(.unfa)) 0 else nfa_len + 2;
  return @max(nfa_len, unfa_len, rnfa_len);
}

pub const ConfigResolved = struct {
  offset_bp: RelativeBreakpoint,
  /// This 
  context: context.ModeResolved,

  /// Checks whether the submachine size is valid given the limits and configuration
  /// Respects limits
  /// Asserts that the submachine is not larger than what the context can support
  pub fn isValidLength(comptime config: ConfigResolved, comptime limits: Limits, submachine_size: usize) error{ContextTooSmall, TooManyStates}!void {
    const max_submachine_size = comptime switch (config.context) {
      .dynamic => |bp| @min(std.math.maxInt(bp.Index()), limits.max_states),
      .fixed => |c| b: { 
        if (c > limits.max_states) 
          @compileError(std.fmt.comptimePrint("Fixed context was defined with length {d}, but it is redundantly large as config.max_states is {d}", .{c, limits.max_states}));
        break :b @min(limits.max_states, c);
      },
    };

    if (submachine_size > max_submachine_size) {
      assert(max_submachine_size <= limits.max_states);
      return if (submachine_size > limits.max_states) error.TooManyStates else {
        // @compileLog(max_submachine_size, submachine_size);
        return error.ContextTooSmall;
      };
    }
  }
};

/// Does not include sets or context
pub fn memoryFootprint(
  comptime config: ConfigResolved,
  comptime sets_bp: arch.AbsoluteBreakpoint,
  states_len: usize,
) usize {
  const State = state.State(config.offset_bp, sets_bp);
  // The context type does not affect its size
  return @sizeOf(State) * states_len + machineSize();
}

/// Does not include sets or context
pub fn contextMode(comptime config: ConfigResolved) ?context.Mode {
  comptime return config.context;
}

pub fn ContextData(comptime config: ConfigResolved) type {
  comptime return lists.Data(config);
}

pub fn interests(comptime config: anytype, comptime compile_config: compile.Config) std.EnumSet(ManifestField) {
  comptime {
    const T = @TypeOf(config);
    assert(T == ConfigResolved or T == Config);
    if (compile_config.strategy) |strat| {
      if (strat.requires(.rnfa) and strat.requires(.nfa)) {
        return .initMany(&.{.rnfa_states_count, .nfa_states_count});
      } else if (strat.requires(.nfa)) {
        return .initMany(&.{.nfa_states_count});
      } else if (strat.requires(.rnfa)) {
        return .initMany(&.{.rnfa_states_count});
      }
    }
    return .initMany(&.{.rnfa_states_count, .nfa_states_count});
  }
}

/// Returns the strategies this arch can support
pub fn strategies() std.EnumSet(strategy.Name) {
  return .initMany(&.{.start_anchor_full_pass, .start_anchor_pass, .end_anchor_reverse_pass, .bi_directional_pass, .start_set_pass});
}

pub fn strategyFromRouting(routing: pzre.ast.Routing) strategy.Name {
  return switch (routing) {
    .exact_match => .start_anchor_full_pass,
    .prefix_match => .start_anchor_pass,
    .suffix_match => .end_anchor_reverse_pass,
    .unanchored_search => .bi_directional_pass,
  };
}

pub fn bid(
  comptime config: ConfigResolved,
  comptime sets_bp: AbsoluteBreakpoint,
  manifest: pzre.ast.Manifest,
  user_strat: ?strategy.Name,
) ?Bid {
  // The returned strategy (and therefore the length/size) is always correctly deduced from the
  // manifest, due to it early using the routing given the maybe-forced-strategy
  // 
  // 1. manifest has user strategy implied routing
  //  - due to arch filtering, we support this strategy
  //  - use the forced strategy
  //  - what about state counts/memory usage? this depends on the strategy
  //  - use the combination of user_strat + manifest
  //  - the AST state calculations do not depend on strategy
  //  - or does it? what about routingLowering ?
  //  - the routing passed to manifest creation respects the user_strat. So all good?
  //  - another apprach is to not require the user_strat field and just trust manifest.routing to return user 
  //    strat. But this would require strategy-routing functions to be invertible: would this always be the 
  //    case? I believe there is no sensible way to refactor invertibility
  //    Probably best to just pass the user strat and ensure that its always returned
  //
  // 2. manifest has AST implied routing
  //  - we are free to choose any strategy

  const State = state.State(config.offset_bp, sets_bp);
  if (manifest.features.capture_groups) return null;
 
  const strat = if (user_strat) |strat| strat else strategyFromRouting(manifest.routing);
  const states_count = statesLength(manifest, strat);
 
  const max_offset_coverage = config.offset_bp.max_states();
  if (max_offset_coverage < maxSubmachineSize(manifest, strat)) return null;

  return Bid{
    .tier = .no_redos_nfa,
    .memory_scaling_profile = @sizeOf(State),
    .total_memory_footprint = memoryFootprint(config, sets_bp, states_count),
    .strategy = strat,
  };
}

pub fn Internals(
  comptime config: ConfigResolved,
  comptime problem_bp: AbsoluteBreakpoint,
  comptime sets_bp: AbsoluteBreakpoint,
) type {
  return struct {
    /// 
    /// The compiled machine
    states: []const State,
    /// The sets (integer ranges) of the original pattern
    sets: []const Set,
    /// The word set tied to the \b \B assertions
    word_set: Set,
    /// The problem this NFA is compiled to solve
    formulation: strategy.Formulation(problem_bp),

    const Self = @This();

    pub const M = Machine(config, sets_bp);
    pub const State = state.State(config.offset_bp, sets_bp);
    pub const Context = context.Context(ContextData(config));
    pub const non_allocator_context = config.context == .fixed;

    pub fn build(
      comptime limits: Limits,
      comptime model: MemoryModel,
      gpa: Allocator,
      artifacts: *compile.Artifacts,
      word_set: Set,
      strat: strategy.Name,
    ) compile.Error!Self {
      const l = linker.linker(config, limits, model, problem_bp, sets_bp);
      return try l.build(gpa, artifacts, word_set, strat);
    }

    /// Checks whether the context is supported for this particular nfa
    fn assertValidCtx(self: Self, ctx: *Context) void {
      const states_len = self.formulation.requiredContextLen();
      const max_conc = analysis.determineMaxConcurrency(states_len);

      switch (config.context) {
        .fixed => {
          // @compileLog(ctx.data.last_list_idxs.len, states_len);
          assert(ctx.data.last_list_idxs.len >= states_len);
          assert(ctx.data.lists[0].buffer.len >= max_conc);
        }, 
        .dynamic => {
          assert(ctx.data.last_list_idxs.items.len >= states_len);
          assert(ctx.data.lists[0].capacity >= max_conc);
        },
      }
    }

    pub inline fn find(self: Self, ctx: *Context, str: []const u8, start_idx: usize, max_base: usize) ?regex.Match {
      self.assertValidCtx(ctx);
      return search.find(
        problem_bp,
        self.formulation,
        Machine(config, sets_bp),
        ctx,
        self.states,
        self.sets,
        self.word_set,
        str,
        start_idx,
        max_base,
        null,
      );
    }

    pub fn requiredContextLen(self: Self) usize {
      return self.formulation.requiredContextLen();
    }

    pub fn deinit(self: *@This(), gpa: Allocator) void {
      gpa.free(self.states);
      pzre.misc.destroySets(gpa, self.sets);
      self.formulation.deinit(gpa);
    }
  };
}

pub const MatchOpts = struct {
  /// The machine exists immediately when an accept state is encountered
  non_greedy: bool = false,

  /// The machine should iterate the input in reverse
  /// 
  /// Not the same thing as a reversed machine
  /// 
  iterate_reverse: bool = false,

  /// The machine is a reversed NFA, and all assertions should be treated as such
  /// As an example, input.len match start of line
  ///
  reversed_machine: bool = false,
};

/// Returns the size of the machine
/// The size is homogeneous over parametrization
/// 
/// WARNING:SUPER IMPORTANT
///
/// When implementing new machines. Their sizes have to be homogeneous over parametrization
/// This is because during dispatch phase, each arch returns their bid on how well they can solve the problem
/// This bid contains memory usage information.
/// The problem is that this same dispatch pipeline is also run during type resolution
/// i.e. when we do not know the types fully. So we cannot parametrize Machine yet in order to instantiate 
/// the type and get its size with @sizeOf
pub fn machineSize() usize {
  // It has two slices, one pointer and a Set
  return 2 * @sizeOf([]const u8) + @sizeOf(*u8) + @sizeOf(Set);
}

/// Pattern maching machine logic
/// 
/// Does not own any fields, no deinit
///
pub fn Machine(
  comptime conf: ConfigResolved,
  comptime sets_bp: arch.AbsoluteBreakpoint,
) type {
  return struct {
    ctx: *Context,
    states: []const State,
    sets: []const Set,
    word_set: Set,

    pub const Context = pzre.arch.Context(ContextData(conf));
    pub const State = state.State(conf.offset_bp, sets_bp);
    pub const Offset = conf.offset_bp.Offset();

    // The index is defined by the context
    // The minimal_nfa machine is fully absolute-index free
    pub const Idx = conf.context.breakpoint().Index();
    const Self = @This();
   
    pub const castAltPath = arch.integer_utils(Idx, Offset).castAltPath;

    /// Expects ctx to be reset
    pub fn init(ctx: *Context, states: []const State, sets: []const Set, word_set: Set) Self {
      return .{.states = states, .ctx = ctx, .sets = sets, .word_set = word_set};
    }

    /// Attempts to match input[start_idx..]
    /// We pass the start_idx separately, so that certain assertions behave correctly
    /// Returns null for no match, and an end exclusive index for the end of the match: str = input[start_idx .. return_val]
    /// 
    /// Note that it might match the empty string, in which case return_val == start_idx
    ///
    /// When iterating in reverse, instead a start inclusive index is returned, therefore:
  
    ///   match :=  input[return_val .. start_idx]
    /// 
    /// In order to match the end of a string, with a reverse nfa, you pass start_idx = input.len
    ///   with iterate_reverse
    /// 
    pub fn matches(
      self: *const Self,
      comptime opts: MatchOpts,
      input: []const u8,
      start_idx: usize,
      iteration_bound: usize,
      captures: ?[]usize, 
    ) ?usize {
      _ = captures;
      self.ctx.reset();
      const start_list_id = self.ctx.data.list_id;
      var input_idx: usize = start_idx;

      switch (comptime conf.context) {
        .dynamic => {
          if (self.states.len == 0) {
            @branchHint(.cold);
            return self.emptyMachine(opts, start_idx, start_list_id);
          }
        }, 
        .fixed => |d| {
          if (comptime d == 0) {
            assert(self.states.len == 0);
            return self.emptyMachine(opts, start_idx, start_list_id);
          }
        },
      }

      // debug.prettyPrint(.{
      //   .starting = start_idx,
      //   .reversed = opts.iterate_reverse,
      // });
      // if (!@inComptime()) debug.prettyPrint(.{
        // .opts = opts,
        // .matching = input,
        // .start_idx = start_idx,
        // .start_list_id = start_list_id,
        // .start_state = self,
        // .sets = self.sets,
        // .states = self.states
      // });
      //
      // if (!@inComptime()) debug.prettyPrint(.{.post_start = self});
      if (comptime !opts.iterate_reverse) {
        self.startlist(opts, input, input_idx);
        while (input_idx < iteration_bound and self.ctx.data.currentlist().len > 0) {
          @branchHint(.likely);
          // debug.prettyPrint(.{.current_lists = self.ctx.data.currentlist()});
          if (comptime opts.non_greedy) {
            if (self.ctx.data.previous_accept_append_list_id != null) break;
          }

          defer input_idx += 1;
          self.step(opts, input, input_idx);
          self.ctx.data.swaplists();
          // debug.prettyPrint(.{.lists_post = self.ctx.data.currentlist()});
        }

      } else {
        self.startlist(opts, input, input_idx);
        while (input_idx > iteration_bound and self.ctx.data.currentlist().len > 0) {
          @branchHint(.likely);
          if (comptime opts.non_greedy) {
            if (self.ctx.data.previous_accept_append_list_id != null) break;
          }

          input_idx -= 1;
          self.step(opts, input, input_idx);
          self.ctx.data.swaplists();
        }
      }

      const is_match = self.ismatch(opts, start_idx, start_list_id);
      // debug.prettyPrint(.{
      //   .is_match = is_match,
      //   .post = self,
      // });
      return is_match;
    }

    /// Returns the number of characters matched, or null for no match
    fn ismatch(self: *const Self, comptime opts: MatchOpts, start_idx: usize, start_list_id: usize) ?usize {

      if (self.ctx.data.previous_accept_append_list_id) |absolute_idx| {
        const prev_append_consumed = absolute_idx - start_list_id - 1;
        if (comptime !opts.iterate_reverse) {
          return start_idx + prev_append_consumed;
        } else {
          return start_idx - prev_append_consumed;
        }

      }
      return null;
    }

    fn emptyMachine(self: *const Self, comptime opts: MatchOpts, start_idx: usize, start_list_id: anytype) ?usize {
      self.ctx.data.incrementListId();
      self.ctx.data.previous_accept_append_list_id = self.ctx.data.list_id;
      return self.ismatch(opts, start_idx, start_list_id);
    }

    fn startlist(self: *const Self, comptime opts: MatchOpts, input: []const u8, input_idx: usize) void {
      self.ctx.data.incrementListId();
      self.addstate(opts, input, input_idx, self.ctx.data.current_list_idx, 0);
    }

    /// input_idx is the next index that has not been consumed yet.
    /// 0 implies that no chars have been consumed yet. input_idx == input.len implies that no more chars will be consumed
    fn addstate(self: *const Self, comptime opts: MatchOpts, input: []const u8, input_idx: usize, list_idx: u1, state_idx: Idx) void {
      if (state_idx == self.states.len) {
        @branchHint(.unlikely);
        self.ctx.data.previous_accept_append_list_id = self.ctx.data.list_id;
        return;
      }

      if (self.ctx.data.stateLastlist(state_idx) == self.ctx.data.list_id) {
        @branchHint(.unlikely);
        return;
      }
      self.ctx.data.setStateLastlist(state_idx, self.ctx.data.list_id);
      // NOTE: any reason to remove recursion from here? Limits already make the 
      //  machine ReDoS/stack overflow immune
      // NOTE: assertions should never be evaluated twice without the machine stepping due to AST optimizations

      const st = self.states.ptr[state_idx];
      switch (st.tag) {
        // How common tags are:
        // Most common: splits and non-jump terms
        // Semi common: jumping terms
        // Unlikely: .jump and assertions

        .split => {
          self.addstate(opts, input, input_idx, list_idx, state_idx + 1);
          const jump_idx = castAltPath(state_idx, st.alt_jump);
          self.addstate(opts, input, input_idx, list_idx, jump_idx);
        },
        .jump => {
          @branchHint(.unlikely);
          const jump_idx = castAltPath(state_idx, st.alt_jump);
          self.addstate(opts, input, input_idx, list_idx, jump_idx);
        },
        .term_char, .term_set, => {
          @branchHint(.likely);
          self.ctx.data.appendList(list_idx, state_idx);
        },

        .term_char_alt_jump, .term_set_alt_jump => {
          self.ctx.data.appendList(list_idx, state_idx);
        },

        .text_start => {
          @branchHint(.unlikely);
          const passes = if (opts.reversed_machine and opts.iterate_reverse) 
            input_idx == input.len else input_idx == 0;
          if (passes) self.addstate(opts, input, input_idx, list_idx, state_idx + 1);
        },
        .text_start_alt_jump => {
          @branchHint(.unlikely);
          const passes = if (opts.reversed_machine and opts.iterate_reverse) 
            input_idx == input.len else input_idx == 0;
          if (passes) {
            const jump_idx = castAltPath(state_idx, st.alt_jump);
            self.addstate(opts, input, input_idx, list_idx, jump_idx);
          }
        },

        .text_end => {
          @branchHint(.unlikely);
          const passes = if (opts.reversed_machine and opts.iterate_reverse) 
            input_idx == 0 else input_idx == input.len;
          if (passes) self.addstate(opts, input, input_idx, list_idx, state_idx + 1);
        },
        .text_end_alt_jump => {
          @branchHint(.unlikely);
          const passes = if (opts.reversed_machine and opts.iterate_reverse) 
            input_idx == 0 else input_idx == input.len;
          if (passes) {
            const jump_idx = castAltPath(state_idx, st.alt_jump);
            self.addstate(opts, input, input_idx, list_idx, jump_idx);
          }
        },

        .word_boundary => {
          @branchHint(.unlikely);
          const left = if (input_idx == 0) false else self.word_set.contains(u8, input[input_idx - 1]);
          const right = if (input_idx == input.len) false else self.word_set.contains(u8, input[input_idx]);
          if (left != right) self.addstate(opts, input, input_idx, list_idx, state_idx + 1);
        },
        .word_boundary_alt_jump => {
          @branchHint(.unlikely);
          const left = if (input_idx == 0) false else self.word_set.contains(u8, input[input_idx - 1]);
          const right = if (input_idx == input.len) false else self.word_set.contains(u8, input[input_idx]);
          if (left != right) {
            const jump_idx = castAltPath(state_idx, st.alt_jump);
            self.addstate(opts, input, input_idx, list_idx, jump_idx);
          }
        },

        .not_word_boundary => {
          @branchHint(.unlikely);
          const left = if (input_idx == 0) false else self.word_set.contains(u8, input[input_idx - 1]);
          const right = if (input_idx == input.len) false else self.word_set.contains(u8, input[input_idx]);
          if (left == right) self.addstate(opts, input, input_idx, list_idx, state_idx + 1);
        },
        .not_word_boundary_alt_jump => {
          @branchHint(.unlikely);
          const left = if (input_idx == 0) false else self.word_set.contains(u8, input[input_idx - 1]);
          const right = if (input_idx == input.len) false else self.word_set.contains(u8, input[input_idx]);
          if (left == right) {
            const jump_idx = castAltPath(state_idx, st.alt_jump);
            self.addstate(opts, input, input_idx, list_idx, jump_idx);
          }
        },

        .line_start => {
          @branchHint(.unlikely);
          const passes = if (opts.iterate_reverse and opts.reversed_machine) 
            input_idx == input.len or input[input_idx] == '\r' or (input[input_idx] == '\n' and (input_idx == 0 or input[input_idx - 1] != '\r'))
            else input_idx == 0 or input[input_idx - 1] == '\n' or (input[input_idx - 1] == '\r' and (input_idx == input.len or input[input_idx] != '\n'));
          if (passes) self.addstate(opts, input, input_idx, list_idx, state_idx + 1);
        },
        .line_start_alt_jump => {
          @branchHint(.unlikely);
          const passes = if (opts.iterate_reverse and opts.reversed_machine) 
            input_idx == input.len or input[input_idx] == '\r' or (input[input_idx] == '\n' and (input_idx == 0 or input[input_idx - 1] != '\r'))
            else input_idx == 0 or input[input_idx - 1] == '\n' or (input[input_idx - 1] == '\r' and (input_idx == input.len or input[input_idx] != '\n'));
          if (passes) {
            const jump_idx = castAltPath(state_idx, st.alt_jump);
            self.addstate(opts, input, input_idx, list_idx, jump_idx);
          }
        },

        .line_end => {
          @branchHint(.unlikely);
          const passes = if (opts.iterate_reverse and opts.reversed_machine) 
            input_idx == 0 or input[input_idx - 1] == '\n' or (input[input_idx - 1] == '\r' and (input_idx == input.len or input[input_idx] != '\n'))
            else input_idx == input.len or input[input_idx] == '\r' or (input[input_idx] == '\n' and (input_idx == 0 or input[input_idx - 1] != '\r'));
          if (passes) self.addstate(opts, input, input_idx, list_idx, state_idx + 1);
        },
        .line_end_alt_jump => {
          @branchHint(.unlikely);
          const passes = if (opts.iterate_reverse and opts.reversed_machine) 
            input_idx == 0 or input[input_idx - 1] == '\n' or (input[input_idx - 1] == '\r' and (input_idx == input.len or input[input_idx] != '\n'))
            else input_idx == input.len or input[input_idx] == '\r' or (input[input_idx] == '\n' and (input_idx == 0 or input[input_idx - 1] != '\r'));
          if (passes) {
            const jump_idx = castAltPath(state_idx, st.alt_jump);
            self.addstate(opts, input, input_idx, list_idx, jump_idx);
          }
        },
      }
    }

    /// Steps the automata one step forward, checking against an input character
    /// When 'step' begins executing, we say that input[input_idx] has been consumed.
    /// Each step consumes exactly one character from the input
    fn step(self: *const Self, comptime opts: MatchOpts, input: []const u8, input_idx: usize) void {
      // debug.prettyPrint(.{
      //   "stepping", input_idx, input,
      // });
      const c = input.ptr[input_idx];
      self.ctx.data.incrementListId();
      self.ctx.data.clearNext();
      const nlist = self.ctx.data.nextlistIdx();
      const next_input_idx = if (comptime opts.iterate_reverse) input_idx else input_idx + 1;
      // Alt jumps are generated to be rare, we aim for term_char and term_set being likely
      // Usually, the NFA is in multiple states at once, evaluating all possibilities
      // The current input character is fixed, and the states point to various different terms
      // This is especially true when you consider AST optimizations
      // As such, it makes sense to think that it is unlikely for a term if statement to match

      for (self.ctx.data.currentlist()) |i| {
        const st = self.states.ptr[i];


        // debug.prettyPrint(.{.in = st, .comparing = c});
        // NOTE: should the matching if statements be @branchHint(.unlikely)?

        switch (st.tag) {
          .term_char => {
            @branchHint(.likely); // term_char and term_set are the likelies tags to encounter
            if (c == st.term.char.value) {
              // @branchHint(.unlikely);
              self.addstate(opts, input, next_input_idx, nlist, @truncate(i + 1));
            }
          },
          .term_char_alt_jump => {
            if (c == st.term.char.value) {
              // @branchHint(.unlikely);
              assert(i <= std.math.maxInt(Idx));
              const jump_idx = castAltPath(@truncate(i), st.alt_jump);
              self.addstate(opts, input, next_input_idx, nlist, jump_idx);
            }
          },
          .term_set => {
            @branchHint(.likely);
            // debug.prettyPrint(.{set});
            if (self.sets.ptr[st.term.set_idx].contains(u8, c)) {
              // @branchHint(.unlikely);
              self.addstate(opts, input, next_input_idx, nlist, @truncate(i + 1));
            }
          },
          .term_set_alt_jump => {
            // debug.prettyPrint(.{set});
            if (self.sets.ptr[st.term.set_idx].contains(u8, c)) {
              // @branchHint(.unlikely);
              assert(i <= std.math.maxInt(Idx));
              const jump_idx = castAltPath(@truncate(i), st.alt_jump);
              self.addstate(opts, input, next_input_idx, nlist, jump_idx);
            }
          },
          // splits and assertions are never added
          .jump, .split, .word_boundary, .text_start, .text_end, .not_word_boundary, .line_end, .line_start => unreachable,
          .word_boundary_alt_jump, .text_start_alt_jump, .text_end_alt_jump, .not_word_boundary_alt_jump, .line_end_alt_jump, .line_start_alt_jump => unreachable,
        }
      }
    }
  };
}
