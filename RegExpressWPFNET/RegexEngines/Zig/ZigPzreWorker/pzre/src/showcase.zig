const std = @import("std");

const pzre = @import("root.zig");
const Allocator = std.mem.Allocator;
const Io = std.Io;
const Match = pzre.Match;
const Context = pzre.nfa.context.Context;
const Replacement = pzre.Replacement;
const Config = pzre.Config;
const ManyReplacements = pzre.ManyReplacements;
const expect = std.testing.expect;
const expectEqual = std.testing.expectEqual;
const expectDeeplyEqual = pzre.lens.testing.expectDeeplyEqual;
const expectEqualStrings = std.testing.expectEqualStrings;
const compile = pzre.compile;

test "Showcase: Basic Matching" {
  const gpa = std.testing.allocator;

  var re = try compile.nfa(.{}, gpa, "[A-Za-z][a-z_]+");
  defer re.deinit(gpa);

  // Each machine requires a mutable context
  // In single threaded environments a single context should be used 
  //  that is shared between all compiled machines
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);

  // The core matching api cannot error, and will not perform allocations
  if (re.match(&ctx, ":: Mark ?")) |match| {
    const expected = Match{ .loc = .init(3, 7), .str = "Mark" };
    try expectDeeplyEqual(expected, match);
  } else unreachable;


  // Iteration
  var it = re.matchIter(&ctx, "123; snake_case; PascalCase");
  try expectEqualStrings("snake_case", it.next().?.str);
  try expectEqualStrings("Pascal", it.next().?.str);
  try expectEqualStrings("Case", it.next().?.str);
  try expectEqual(null, it.next());


  // All examples above compile and match at runtime
  // The entire compilation pipeline, and matching API are legal for comptime
  // 
  // The Nfa object returned by comptime compilation is the exact same type 
  //  as returned by runtime compilation. As such, both provide the same matching api

  comptime { // compiling and matching at comptime
    var hex = compile.nfaComptime(.{.context = .compact_fixed}, "abc\\b");
    var comptime_ctx = hex.initContextFixed();
    // initContextFixed is a wrapper for: initContext(undefined) catch undefined

    const input = "abc abcabc abc";
    var cit = hex.matchIter(&comptime_ctx, input);
    try expect(cit.next() != null);
    try expect(cit.next() != null);
    try expect(cit.next() != null);
    try expect(cit.next() == null);
  }

  { // compiling at comptime and matching at runtime
    var hex = comptime compile.nfaComptime(.{
      // We need to set this in order to use the context from the runtime pattern
      // See 'Showcase: context'
      .limits = .{ .context_breakpoint = .i16 }
    }, "abc\\b");
    try ctx.update(gpa, hex);

    try expect(hex.matches(&ctx, "abc"));
  }
}

test "Showcase: Configuration" {
  const gpa = std.testing.allocator;

  // Multiline semantics
  var re = try compile.nfa(.{ .semantics = .{ .multiline = true } }, gpa, "^\\s*\\.\\w+");
  defer re.deinit(gpa);
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);

  var field_it = re.matchIter(&ctx, 
  \\struct {
  \\  .name = ".mark",
  \\.word = "true",
  \\}
  );
  try expectEqualStrings("  .name", field_it.next().?.str);
  try expectEqualStrings(".word", field_it.next().?.str);
  try expectEqual(null, field_it.next());


  // Whitespace is ignored in the pattern, allowing for highly readable, 
  // vertically aligned multiline syntax.
  const config: Config = .{
    .semantics = .{ .pat_ignore_whitespace = true },
  };
  
  var lpat = try compile.nfa(
    config,
    gpa,
    \\ ^
    \\   [A-Z]{3,9}
    \\   \s+
    \\   / [a-zA-Z0-9_./-]*
    \\   \s+
    \\   HTTP / [0-9] \. [0-9]
    \\ $
  );
  defer lpat.deinit(gpa);
  try ctx.update(gpa, lpat);
  
  try expect(lpat.matches(&ctx, "POST /api/v1/users HTTP/1.1"));
  try expect(lpat.matches(&ctx, "GET / HTTP/1.0"));
  try expect(!lpat.matches(&ctx, "UPDATE / HTTP/2"));
}

test "Showcase: Resource Limits" {
  const gpa = std.testing.allocator;

  // Limits are designed to enforce strict resource contracts,
  // guaranteeing the engine never hangs or exhausts memory on untrusted input.

  {
    // Scenario 1: Limiting AST recursion depth
    // Deeply nested patterns can cause stack overflows during parsing.
    const depth_config: pzre.Config = .{
      .limits = .{
        .max_depth = 2,
      }
    };

    const r1 = compile.nfa(depth_config, gpa, "(((a)))");
    try expectEqual(error.TooDeep, r1);

    // This field limits all recursion, including during AST optimization
    const r2 = compile.nfa(depth_config, gpa, "aaaaaa");
    try expectEqual(error.TooDeep, r2);
  }

  {
    // Malicious users might submit patterns like a{10000} to force 
    // the compiler to generate massive submachines.
    const rep_config: pzre.Config = .{
      .limits = .{
        .max_arbitrary_repetition = 50,
      }
    };

    const result = compile.nfa(rep_config, gpa, "a{10000}");
    try expectEqual(error.TooHighArbitraryRepeat, result);
  }

  {
    // You can define a hard ceiling on the total number of states the 
    // contiguous memory region is allowed to hold.
    const state_config: pzre.Config = .{
      .limits = .{
        .max_states = 10,
      }
    };

    const r1 = compile.nfa(state_config, gpa, "this_is_too_long");
    const r2 = compile.nfa(state_config, gpa, "a{1000000}");
    try expectEqual(error.TooManyStates, r1);
    try expectEqual(error.TooManyStates, r2);
  }
}

test "Showcase: search and replace" {
  const gpa = std.testing.allocator;
  var re = try compile.nfa(.{}, gpa, "[A-Za-z][a-z_]+");
  defer re.deinit(gpa);

  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);

  // Replacement string is allocated
  if (try re.replaceAll(&ctx, gpa, "Brother Man Bill", "xx")) |match| {
    defer match.deinit(gpa);
    try expectEqualStrings("xx xx xx", match.new);
  } else {
    // no match -> no allocations
    unreachable;
  }


  // With multiline semantics
  var re2 = try compile.nfa(.{
    .semantics = .{ .multiline = true },
  }, gpa, "^\\s*[*+]\\s*");
  defer re2.deinit(gpa);

  const input = \\* item one
                \\  + item two
                \\*   item three
                \\*item four
                ;

  const expected = \\- item one
                   \\- item two
                   \\- item three
                   \\- item four
                   ;

  try ctx.update(gpa, re2);
  var result = try re2.replaceAll(&ctx, gpa, input, "- ");

  if (result) |*replacements| {
    defer replacements.deinit(gpa);
    try expectEqualStrings(expected, replacements.new);
  } else unreachable;
}

test "Showcase: context" {
  const gpa = std.testing.allocator;

  // Contexts are the mutable part of a machine. These are managed separately in order to:
  //  1. Ensure threadsafety for multithreading
  //  2. Reduce memory usage
  //  3. Make the core matching api allocator-free (and error-result free)
  // 

  // Runtime machines by default assume a maximum state count of intMax(i16)
  // This is reflected in them assuming a context_breakpoint of i16
  var runtime_pat = try compile.nfa(.{.limits = .{ .context_breakpoint = .i16 }}, gpa, "\\w+");
  defer runtime_pat.deinit(gpa);

  var rctx = try runtime_pat.initContext(gpa);
  defer rctx.deinit(gpa);
  try expect(@TypeOf(runtime_pat).Context == Context(.dynamic, .i16));

  // By default, comptime compilation calculates the number of states before compiling
  // This often results in the machine assuming the smallest integer sizes .i8
  {
    var hex = comptime compile.nfaComptime(.{.limits = .{ .context_breakpoint = null }}, "#[0-9a-fA-F]{6}");
    try expect(@TypeOf(hex).Context == Context(.dynamic, .i8));

    var ctx = try hex.initContext(gpa); // cannot be shared
    defer ctx.deinit(gpa);

    const match = hex.matchStart(&ctx, "#FFFFFF");
    try expect(match != null);
  }

  { // To allow the comptime compilation to share contexts with the runtime compilation, 
    //   we downgrade its context
    var hex = comptime compile.nfaComptime(.{.limits = .{ .context_breakpoint = .i16 }}, "#[0-9a-fA-F]{6}");
    try expect(@TypeOf(hex).Context == Context(.dynamic, .i16));

    // The context was initialized for the runtime machine
    // Dynamic contexts always have to be updated to match new requirements
    // Failing to update is caught at runtime in Debug mode (assert)
    try rctx.update(gpa, hex);

    try expect(hex.matches(&rctx, "#0F0aff"));
  }

  { // We could have also compiled the runtime pattern with a more optimal context
    var pat = try compile.nfa(.{.limits = .{ .context_breakpoint = .i8 }}, gpa, "\\w+");
    defer pat.deinit(gpa);
    try expect(@TypeOf(pat).Context == Context(.dynamic, .i8));

    const cpat = comptime compile.nfaComptime(.{}, "\\w+");
    try expect(@TypeOf(cpat).Context == Context(.dynamic, .i8));

    var ctx = try pat.initContext(gpa);
    defer ctx.deinit(gpa);

    try expect(pat.matches(&ctx, "word"));
    try expect(cpat.matches(&ctx, "word"));

    // Updating is not strictly required if a newly introduced machine 
    //  is not larger than what it expects
  }

  // Context misuse is caught at (zig) comptime due to type mismatch
  // A context can only be shared if the breakpoint and type match
  //
  // The default .dynamic type is a dynamically allocated list
  // This is a more forgiving type as it allows for contexts to be resized 
  //  at runtime to match new requirements

  { // Fixed contexts are static arrays
    const len = 7;
    var pat = try compile.nfa(.{
      .context = .{ .fixed = len },
      .limits = .{ .context_breakpoint = .i8 },
    }, gpa, "[abcd]+");
    defer pat.deinit(gpa);

    // Allocators are not used. Updating and destroying is a no-op
    var fctx = pat.initContextFixed();
    try expect(@TypeOf(fctx) == Context(.{ .fixed = len }, .i8));

    // They always consume the memory as defined by the type
    // bounded above by: sizeOf(State) * len * 3

    const pat2 = comptime compile.nfaComptime(.{
      .context = .{ .fixed = len },
    }, "abcd");

    try expect(pat.matches(&fctx, "abcd"));
    try expect(pat2.matches(&fctx, "abcd"));
  }

  { // Compact fixed contexts are fixed contexts determined dynamically at (zig) comptime
    // Cannot be used for runtime compilation
    const pat = comptime compile.nfaComptime(.{ .context = .compact_fixed }, "abc");
    var fctx = pat.initContextFixed();
    try expect(@TypeOf(fctx) == Context(.{ .fixed = 6 }, .i8));

    // Naturally, these cannot be shared unless the other automata
    //   requires the exact same context

    const pat2 = comptime compile.nfaComptime(.{ .context = .compact_fixed }, "cba");
    try expect(pat.matches(&fctx, "abc"));
    try expect(pat2.matches(&fctx, "cba"));
  }
}

test "Showcase: intermediaries" {
  const gpa = std.testing.allocator;

  { // Other objects can be compiled aswell, such as metadata for pattern validation
    // or an AST for syntax analysis
    // Parsing will generate all requested objects on a single pass in parallel
    var out = try compile.generate(.{}, .{ .ast = true, .metadata = true }, gpa, "^abc");
    defer out.deinit(gpa);

    try expect(out.metadata.states_count == 5);
    try expect(!out.metadata.is_variable_len);
  }

  { // We can define a config that will always use specific solver
    // Allowing for faster compilation and less memory usage
    const minimal = comptime compile.nfaComptime(.{ .problem = .start_set_pass, .optimize = true }, 
      "^[A-Z]{3,9}\\s+/[a-zA-Z0-9_./-]*\\s+HTTP/[0-9]\\.[0-9]$");

    const State = @TypeOf(minimal).State;

    // A single state is 4 bytes long
    const machine_size = @sizeOf(State) * minimal.states.len;
    try expectEqual(machine_size, 132);
    try expectEqual(@sizeOf(State), 4);

    // This does not include the sets and the context the machine uses
    // In the future, machines can be compiled together into a 'family' object 
    // that will use the same sets and context as a shared resource. 
    // This allows for the memory use to converge to 4 bytes per state as the number of machines increase
  }

  // The NFA can also be generated on a single pass without an AST, 
  // for that use the ast generation function with a problem that does not require an AST
}

test "Showcase: advanced" {
  const gpa = std.testing.allocator;
  var hex = comptime compile.nfaComptime(.{}, "abc\\b");
  var ctx = try hex.initContext(gpa);
  defer ctx.deinit(gpa);

  // For advanced usage the compiled immutable machine can be extracted for custom algorithms
  // The default solver is the bi_directional_pass algorithm
  // The 'nfa' part is the forward thompson submachine which can be used to 
  //   simply match starting from the beginning of a string
  const fragment = hex.formulation.bi_directional_pass.nfa;
  const machine = hex.makeMachine(&ctx, fragment);
  const match = machine.matches(.{}, hex.sets, "abc", 0, 3);
  try expect(match != null);

  const match2 = machine.matches(.{}, hex.sets, "qabc", 0, 3);
  try expect(match2 == null);
}

fn workerThread(pool: anytype, gpa: Allocator, io: Io, nfa_obj: anytype) !void {
  const ctx = try pool.acquire(gpa, io, nfa_obj.requiredContextLen());
  // Contexts have to be released back to the pool
  defer pool.release(gpa, io, ctx);
}

test "Multithreaded" {
  const gpa = std.testing.allocator;
  const io = std.testing.io;

  const config: pzre.Config = .{};
  var nfa_alpha = try pzre.compile.nfa(config, gpa, "alpha_[0-9]+");
  const T = @TypeOf(nfa_alpha);
  defer nfa_alpha.deinit(gpa);

  // A context pool is initiated for the workload
  // Acquiring and releasing contexts will never perform allocations post warmup
  // Pools warmup on init by default
  const PoolType = pzre.nfa.context.Pool(.dynamic, .i16, .{ .initial_capacity = 2 });
  var pool = try PoolType.init(T, gpa, io, 2, nfa_alpha, &.{});
  defer pool.deinit(gpa);

  // Do some work
  var threads: [2]std.Thread = undefined;
  threads[0] = try std.Thread.spawn(.{}, workerThread, .{ &pool, gpa, io, nfa_alpha});
  threads[1] = try std.Thread.spawn(.{}, workerThread, .{ &pool, gpa, io, nfa_alpha});

  for (threads) |t| t.join();

  // Lets say we encounter new runtime requirements: more workers and a new machine
  // The pool is warmed up to match this requirement
  var nfa_beta = try pzre.compile.nfa(config, gpa, "beta_[a-z]{4,8}");
  defer nfa_beta.deinit(gpa);

  const new_machines = [_]T{ nfa_alpha, nfa_beta };
  try pool.warmup(T, gpa, io, 4, nfa_alpha, &new_machines);
  const t = try std.Thread.spawn(.{}, workerThread, .{ &pool, gpa, io, nfa_beta});
  t.join();

  // The warmup for a new requirement is not strictly always required
  // Here not doing so would crash as the new 'beta' pattern would be the new longest pattern
  // An unexpected number of new simultaneous workers would simply trigger an allocation
}
