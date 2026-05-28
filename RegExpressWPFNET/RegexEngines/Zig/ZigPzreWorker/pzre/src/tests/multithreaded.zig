const tst = @import("test.zig");
const std = @import("std");
const assert = std.debug.assert;

const testMatchExact = tst.testMatchExact;
const testFindAll = tst.testFindAll;
const testFindAllMultiline = tst.testFindAllMultiline;
const testFind = tst.testFind;
const pzre = @import("../root.zig");
const Match = pzre.nfa.Match;
const compile = pzre.compile;
const Allocator = std.mem.Allocator;
const Io = std.Io;

const Config = pzre.compile.Config;
const context = pzre.nfa.context;

const match_thread = struct {
  fn run(
    comptime NfaType: type,
    comptime PoolType: type,
    nfa_obj: NfaType,
    pool: *PoolType,
    str: []const u8,
    expected: bool,
    seed: u64,
  ) !void {
    const io = std.testing.io;
    const gpa = std.testing.allocator;
    var prng = std.Random.DefaultPrng.init(seed);
    const random = prng.random();

    for (0..10_000) |_| {
      if (random.boolean()) try std.Thread.yield();

      const ctx = try pool.acquire(gpa, io, nfa_obj.states.len);
      defer pool.release(gpa, io, ctx);

      if (random.intRangeAtMost(u8, 0, 10) > 8) {
        try std.Io.sleep(io, .{ .nanoseconds = random.intRangeAtMost(u64, 1_000, 10_000)}, .cpu_thread);
      }

      const is_match = nfa_obj.matchesExact(ctx, str);
      try std.testing.expectEqual(expected, is_match);

      if (random.intRangeAtMost(u8, 0, 10) > 8) {
        try std.Io.sleep(io, .{ .nanoseconds = random.intRangeAtMost(u64, 1_000, 10_000)}, .cpu_thread);
      }
    }
  }
};

test "pzre multithreaded context pooling and warmup" {
  const gpa = std.testing.allocator;
  const io = std.testing.io;

  {
    const pat1 = "a";
    const pat2 = "b";
    
    const nfa1 = comptime compile.nfaComptime(.{ .context = .{ .fixed = 40 } }, pat1);
    const nfa2 = comptime compile.nfaComptime(.{ .context = .{ .fixed = 40 } }, pat2);
    
    comptime assert(nfa1.states.len == nfa2.states.len);

    var pool = try nfa1.initContextPool(.{ .initial_capacity = 4 }, gpa, io, 4, &.{});
    defer pool.deinit(gpa);

    var threads: [4]std.Thread = undefined;
    threads[0] = try std.Thread.spawn(.{}, match_thread.run, .{ @TypeOf(nfa1), @TypeOf(pool), nfa1, &pool, "a", true, 1 });
    threads[1] = try std.Thread.spawn(.{}, match_thread.run, .{ @TypeOf(nfa2), @TypeOf(pool), nfa2, &pool, "b", true, 2 });
    threads[2] = try std.Thread.spawn(.{}, match_thread.run, .{ @TypeOf(nfa1), @TypeOf(pool), nfa1, &pool, "b", false, 3 });
    threads[3] = try std.Thread.spawn(.{}, match_thread.run, .{ @TypeOf(nfa2), @TypeOf(pool), nfa2, &pool, "a", false, 4 });

    for (threads) |t| t.join();
    try std.testing.expectEqual(@as(usize, 4), pool.pool.items.len);
  }

  { // Comptime fixed mode with dedicated pools
    const nfa_short = comptime compile.nfaComptime(.{ .context = .compact_fixed }, "a");
    const nfa_long = comptime compile.nfaComptime(.{ .context = .compact_fixed }, "a|b|c|d|e");

    var pool_short = try nfa_short.initContextPool(.{ .initial_capacity = 2 }, gpa, io, 2, &.{});
    defer pool_short.deinit(gpa);

    var pool_long = try nfa_long.initContextPool(.{ .initial_capacity = 2 }, gpa, io, 2, &.{});
    defer pool_long.deinit(gpa);

    var t1 = try std.Thread.spawn(.{}, match_thread.run, .{ @TypeOf(nfa_short), @TypeOf(pool_short), nfa_short, &pool_short, "a", true, 5 });
    var t2 = try std.Thread.spawn(.{}, match_thread.run, .{ @TypeOf(nfa_long), @TypeOf(pool_long), nfa_long, &pool_long, "c", true, 6 });
    
    t1.join();
    t2.join();
  }

  { // Runtime dynamic mode with shared pools
    var nfa_rt1 = try compile.nfa(.{ .context = .dynamic }, gpa, "foo");
    defer nfa_rt1.deinit(gpa);
    var nfa_rt2 = try compile.nfa(.{ .context = .dynamic }, gpa, "(bar)+");
    defer nfa_rt2.deinit(gpa);

    const nfa_family = [_]@TypeOf(nfa_rt1){ nfa_rt2 };

    const PoolType = context.Pool(.dynamic, .i16, .{ .initial_capacity = 2 });
    var shared_pool = try PoolType.init(@TypeOf(nfa_rt1), gpa, io, 4, nfa_rt1, &nfa_family);
    defer shared_pool.deinit(gpa);

    var threads: [4]std.Thread = undefined;
    for (0..4) |i| {
      const target_nfa = if (i % 2 == 0) nfa_rt1 else nfa_rt2;
      const target_str = if (i % 2 == 0) "foo" else "barbar";
      threads[i] = try std.Thread.spawn(.{}, match_thread.run, .{ @TypeOf(nfa_rt1), PoolType, target_nfa, &shared_pool, target_str, true, 7 });
    }

    for (threads) |t| t.join();
  }

  { // warmup constraints and scaling
    const config: Config = .{};
    var n_a = try compile.nfa(config, gpa, "123");
    defer n_a.deinit(gpa);
    
    const PoolType = context.Pool(.dynamic, .i16, .{ .initial_capacity = 0 });
    var pool = try PoolType.init(@TypeOf(n_a), gpa, io, 2, n_a, &.{});
    defer pool.deinit(gpa);

    try std.testing.expectEqual(@as(usize, 2), pool.pool.items.len);

    var n_b = try compile.nfa(config, gpa, "123|456|789");
    defer n_b.deinit(gpa);
    var n_c = try compile.nfa(config, gpa, "1234(56)+");
    defer n_c.deinit(gpa);

    const new_workload = [_]@TypeOf(n_a){ n_b, n_c };
    
    // Scale up to 6
    try pool.warmup(@TypeOf(n_a), gpa, io, 6, n_a, &new_workload);
    try std.testing.expectEqual(@as(usize, 6), pool.pool.items.len);
    
    // Ensure capacity is exact
    const max = context.maxStates(@TypeOf(n_a), n_a, &new_workload);
    for (pool.pool.items) |ctx| {
      try std.testing.expect(ctx.data.last_list_idxs.capacity == max);
    }

    // Scale up to 1 more
    try pool.warmup(@TypeOf(n_a), gpa, io, 7, n_a, &new_workload);
    try std.testing.expectEqual(@as(usize, 7), pool.pool.items.len);
    for (pool.pool.items) |ctx| {
      try std.testing.expect(ctx.data.last_list_idxs.capacity == max);
    }

    // Scale back down to 3 workers
    try pool.warmup(@TypeOf(n_a), gpa, io, 3, n_a, &new_workload);
    try std.testing.expectEqual(@as(usize, 3), pool.pool.items.len);
  }
}

fn testPoolConfig(
  gpa: std.mem.Allocator,
  nfa_obj: anytype,
  comptime pool_cfg: context.PoolConfig,
  comptime workers: usize,
  target_str: []const u8,
  expected_initial_len: usize,
  expected_final_len: ?usize,
) !void {
  const io = std.testing.io;
  const NfaType = @TypeOf(nfa_obj);
  const ConfiguredPool = context.Pool(.dynamic, .i16, pool_cfg);

  var pool = try ConfiguredPool.init(NfaType, gpa, io, workers, nfa_obj, &.{});
  defer pool.deinit(gpa);

  if (!pool_cfg.perform_warmup_routine) 
    try std.testing.expectEqual(expected_initial_len, pool.pool.items.len);

  var threads: [workers]std.Thread = undefined;
  for (0..workers) |i| {
    threads[i] = try std.Thread.spawn(.{}, match_thread.run, .{ NfaType, ConfiguredPool, nfa_obj, &pool, target_str, true, i });
  }
  for (threads) |t| t.join();

  if (expected_final_len) |final_len| {
    try std.testing.expectEqual(final_len, pool.pool.items.len);
  }
}

test "pzre pool configuration edge cases and clamping" {
  const gpa = std.testing.allocator;

  const config: Config = .{};
  var nfa_obj = try compile.nfa(config, gpa, "test|pattern+");
  defer nfa_obj.deinit(gpa);

  // Strict bounds
  try testPoolConfig(
    gpa, nfa_obj,
    .{ .initial_capacity = 2, .max_capacity = 2, .perform_warmup_routine = false },
    4, "test", 2, 2,
  );

  // Warmup overrides initial capacity
  try testPoolConfig(
    gpa, nfa_obj,
    .{ .initial_capacity = 100, .max_capacity = null, .perform_warmup_routine = true },
    4, "test", 4, 4,
  );

  // Initial capacity clamps to max capacity
  try testPoolConfig(
    gpa, nfa_obj,
    .{ .initial_capacity = 10, .max_capacity = 3, .perform_warmup_routine = false },
    8, "pattern", 3, 3,
  );

  // Zero initial capacity expands on demand
  try testPoolConfig(
    gpa, nfa_obj,
    .{ .initial_capacity = 0, .max_capacity = null, .perform_warmup_routine = false },
    4, "test", 0, null,
  );

  // Warmup clamped by max capacity
  try testPoolConfig(
    gpa, nfa_obj,
    .{ .initial_capacity = 0, .max_capacity = 2, .perform_warmup_routine = true },
    4, "test", 2, 2,
  );
  
  // High max capacity with zero initial does not force allocations
  try testPoolConfig(
    gpa, nfa_obj,
    .{ .initial_capacity = 0, .max_capacity = 100, .perform_warmup_routine = false },
    2, "test", 0, null,
  );
}

test "pzre multithreaded pool dynamic resizing leak check" {
  const gpa = std.testing.allocator;

  const S = struct {
    fn f(_gpa: std.mem.Allocator) !void {
      const io = std.testing.io;
      const config: pzre.compile.Config = .{
        .context = .dynamic,
        .limits = .{
          .max_submachine_states = .i16,
          .gpa_upper_bound = 8 << 30,
        },
      };

      // Two NFAs with different size
      var nfa_small = compile.nfa(config, _gpa, "a") catch |err| {
        if (err == error.OutOfMemory) return err;
        if (_gpa.vtable != std.testing.allocator.vtable) return error.OutOfMemory;
        return;
      };
      defer nfa_small.deinit(_gpa);

      var nfa_large = compile.nfa(config, _gpa, "a{10,20}|b{5,15}") catch |err| {
        if (err == error.OutOfMemory) return err;
        if (_gpa.vtable != std.testing.allocator.vtable) return error.OutOfMemory;
        return;
      };
      defer nfa_large.deinit(_gpa);

      // Non perfect wramup
      const PoolType = context.Pool(.dynamic, .i16, .{ .initial_capacity = 2 });
      var pool = PoolType.init(@TypeOf(nfa_small), _gpa, io, 2, nfa_small, &.{ nfa_small }) catch |err| {
        if (err == error.OutOfMemory) return err;
        if (_gpa.vtable != std.testing.allocator.vtable) return error.OutOfMemory;
        return;
      };
      defer pool.deinit(_gpa);

      { // do shit
        const ctx1 = try pool.acquire(_gpa, io, nfa_small.requiredContextLen());
        defer pool.release(_gpa, io, ctx1);

        const ctx2 = try pool.acquire(_gpa, io, nfa_small.requiredContextLen());
        defer pool.release(_gpa, io, ctx2);

        try ctx1.data.updateExact(_gpa, nfa_large.requiredContextLen());
        try ctx1.data.updateExact(_gpa, nfa_small.requiredContextLen());
      }

      // scale up
      const large_workload = [_]@TypeOf(nfa_small){ nfa_large };
      try pool.warmup(@TypeOf(nfa_small), _gpa, io, 5, nfa_large, &large_workload);

      // scale down
      const small_workload = [_]@TypeOf(nfa_small){ nfa_small };
      try pool.warmup(@TypeOf(nfa_small), _gpa, io, 1, nfa_small, &small_workload);

      // bunch of contexts triggering dynamic resizing (incorrect warmup)
      {
        const ctx3 = try pool.acquire(_gpa, io, nfa_large.requiredContextLen());
        defer pool.release(_gpa, io, ctx3);
        const ctx4 = try pool.acquire(_gpa, io, nfa_large.requiredContextLen());
        defer pool.release(_gpa, io, ctx4);
        const ctx5 = try pool.acquire(_gpa, io, nfa_large.requiredContextLen());
        defer pool.release(_gpa, io, ctx5);
        const ctx6 = try pool.acquire(_gpa, io, nfa_large.requiredContextLen());
        defer pool.release(_gpa, io, ctx6);
        const ctx7 = try pool.acquire(_gpa, io, nfa_large.requiredContextLen());
        defer pool.release(_gpa, io, ctx7);
      }
    }
  }.f;

  try S(gpa);
  try std.testing.checkAllAllocationFailures(gpa, S, .{});
}
