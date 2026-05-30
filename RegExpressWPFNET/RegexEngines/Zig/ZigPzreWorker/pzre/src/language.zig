const std = @import("std");

const pzre = @import("root.zig");
const ascii = pzre.encoding.ascii;
const Set = ascii.IntegerSet;
const state = pzre.nfa.state;
const context = pzre.nfa.context;
const search_problem = pzre.nfa.search_problem;

/// Non-automata generation config
/// 
pub const BaseConfig = struct {
  /// Language builtin set definitions
  ///
  semantics: pzre.language.Semantics = .{},
  /// Language feature definitions
  ///
  sets: pzre.language.Sets = .{},
  /// Resource usage limits.
  ///
  limits: pzre.language.Limits = .{},

  const Self = @This();

  pub fn normalize(comptime self: Self) Self {
    comptime {
      var r: Self = self;

      // max_submachine_states cannot be larger than context size
      if (r.limits.context_breakpoint) |b| {
        if (@bitSizeOf(self.limits.max_submachine_states.Offset()) > @bitSizeOf(b.Offset())) {
          r.limits.max_submachine_states = b;
        }
      }
      
      return r;
    }
  }
};

/// When an automata is also generated
///
pub const Config = struct {
  /// Language feature definitions
  ///
  semantics: Semantics = .{},
  /// Language builtin set definitions
  ///
  sets: Sets = .{},
  /// Resource usage limits.
  ///
  limits: Limits = .{},
  /// The type of context the machine uses
  /// 
  /// The context is the mutable core of the nfa. The rest of the machine is completely immutable
  /// 
  /// Contexts can be shared between machines if their definition match
  /// 
  context: context.Mode = .dynamic,
  /// Whether AST optimizations are enabled
  ///
  /// Note that for the default bi_directional_pass problem, an AST is still generated in order
  ///   to reverse the NFA. This flag will simply disable the AST optimization passes
  /// 
  optimize: bool = true,
  /// The search problem formulation the NFA will assume
  /// 
  /// Choosing an incompatible problem for the pattern may (most likely will) 
  ///   cause semantically incorrect matches
  /// Improves compilation performance as the compilation pipeline already knows the target problem.
  /// 
  /// Useful for picking a solver when you know the types of patterns that will be compiled. 
  ///   If you will only ever perform full matches, you can pick .exact_match. 
  ///   If you know the matching start set will always be highly restrictive, you can pick .start_pass, 
  ///   for better matching performance over the default .bi_directional_pass solver
  ///   etc...
  /// 
  /// Performs no checks on whether the problem is valid. Certain problems require additional
  ///   object generations. If a required generation is impossible, error.FormulationImpossible is returned
  /// 
  /// For information on each formulation, see src/nfa/search_problem.zig
  /// 
  /// NOTE: experimental
  /// 
  problem: ?search_problem.Name = null,

  const Self = @This();

  pub fn toBaseConfig(comptime self: Self) BaseConfig {
    comptime {
      const b: pzre.compile.BaseConfig = .{ .limits = self.limits, .semantics = self.semantics, .sets = self.sets };
      return b;
    }
  }

  pub fn normalize(comptime self: Self) Self {
    comptime {
      var r: Config = self;
      const base = self.toBaseConfig();
      const nbase = base.normalize();
      r.limits = nbase.limits;
      r.semantics = nbase.semantics;
      r.sets = nbase.sets;

      return r;
    }
  }
};

/// Resource usage limits respected during compilation
/// Relevant fields are also respected when comptime compiled
pub const Limits = struct {
  /// Determines how much memory the passed allocator is allowed to hold onto at once
  /// 
  /// returns error.AllocationUpperbound when violated
  ///
  gpa_upper_bound: comptime_int = 1 << 14,
  /// The maximum size a submachine is allowed to be
  /// 
  /// Defines the underlying integer types and by extension the State size. 
  /// Runtime known patterns will use this breakpoint directly; comptime compilation will attempt
  /// to aggressively lower the breakpoint to .i8
  /// 
  /// This is the machine-local breakpoint, and it has nothing to do with context shareability
  /// 
  /// Returns error.TooManyStates on violation
  /// 
  max_submachine_states: state.Breakpoint = .i16,
  /// The maximum size a submachine is allowed to be
  /// 
  /// Defines the underlying integer types and by extension the context size. 
  /// 
  /// 'null' means that 'max_submachine_states' is used as the breakpoint. As a result,
  ///   comptime compilation will try to lower this breakpoint aggressively to .i8 if null
  /// 
  /// -- Example --
  /// Consider we have a system with 3 patterns A, B and C
  /// A and B are runtime patterns
  /// C is a comptime pattern with smallish size <100
  /// 
  /// If all 3 patterns are compiled with the default config, 
  ///   A and B will have a context breakpoint of .i16,
  ///   C will have a context breakpoint of .i8
  /// 
  /// A and B can share contexts, but they cannot share with C; the system requires 2 contexts
  /// 
  /// In order to make the system require only one context, the C config sets this to .i16
  /// 
  context_breakpoint: ?state.Breakpoint = null,
  /// The maximum number of states the machine is allowed to be
  /// 
  /// The compiled machine is often composed of multiple submachines that exist 
  ///   in the same contiguous memory region. This is the maximum length of that region
  /// 
  /// If breakpoint i8, then the max size of the region is 4 * max_states
  /// 
  /// If patterns do not contain arbitary repetition a{n,m}, then state count is bounded above by
  ///   pattern length and very closely follow it. Estimating size directly from pattern length
  ///   is effective in most practical cases
  /// 
  /// Returns error.TooManyStates on violation
  ///
  max_states: comptime_int = 1 << 12,
  /// Prevents recursive algorithm stack overflow
  /// 
  /// Defines maximum parenthesis nesting count (((((a)))))
  ///   and maximum AST depth
  /// 
  /// default max depth is max u8
  /// 
  /// Returns error.TooDeep on violation
  ///
  max_depth: comptime_int = 255,
  /// Maximum number of arbitrary repetitions
  /// a{3000} expands to 3000 literal states when unbound
  /// 
  /// When off, state count is bounded above by pattern length. Turning this off
  ///   makes automata size more predictable
  ///
  /// null for unbound
  /// 
  /// Returns error.TooHighArbitraryRepeat on too high repeat
  ///
  max_arbitrary_repetition: ?usize = null,
};

pub const Sets = struct {
  /// definition of \s  default: [ \t\n\r\f\v]
  /// 
  whitespace_set: Set = ascii.Set.WHITESPACE,
  /// definition of .  default: [^\n]
  /// 
  dot_set: Set = ascii.Set.DOT_SET,
  /// universe set the .dotall uses
  /// 
  dotall_set: Set = ascii.Set.ALL,
  /// definition of \d  default: [0-9]
  /// 
  digit_set: Set = ascii.Set.DIGIT,
  /// definition of \w  default: [0-9A-Za-z_]
  /// This defines behavior for \b (word boundary) assertions.
  /// 
  word_set: Set = ascii.Set.WORD,
};

pub const Semantics = struct {
  /// Interpret '^' and '$' as start/end of line
  ///
  multiline: bool = false,
  /// All letters [a-zA-Z] are interpreted as sets, e.g. a = A = [aA]
  /// 
  /// This is respected on all levels, even in hex sequences \xNN
  ///
  ignore_case: bool = false,
  /// All unescaped whitespace outside of sets in the pattern is ignored by the lexer
  /// Sets are parsed normally
  /// 
  /// Allows complex patterns to be defined over multiple lines
  ///
  pat_ignore_whitespace: bool = false,
  /// All unescaped whitespace in the pattern is ignored by the lexer
  /// Including unescaped whitespace in sets
  /// 
  /// Allows complex patterns to be defined over multiple lines
  ///
  pat_ignore_all_whitespace: bool = false,
  /// Disables all forms of implicit newlines, meaning:
  /// 
  /// 1. Disables newline from all builtin sets (including the dot operator set)
  /// 2. Newlines are automatically removed from inverted sets [^a]
  /// 
  /// The only way to match a newline is if it is explicitly present in the pattern
  ///
  never_implicit_newline: bool = false,
  /// dot_set '.' is ignored and universe [^] is used in its place
  /// Equivalent to defining the dot_set manually to []
  /// 
  dotall: bool = false,
};
