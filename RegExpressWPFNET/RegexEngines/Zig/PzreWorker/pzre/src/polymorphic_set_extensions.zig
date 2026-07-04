//! This entire file along with the "polymorphic_memory" module is garbage and will be nuked when zig finally supports comptime allocators
const std = @import("std");
const Allocator = std.mem.Allocator;
const assert = std.debug.assert;

const pzre = @import("root.zig");
const nfa = pzre.nfa;
const ascii = pzre.encoding.ascii;
const Range = ascii.Range;
const Set = ascii.IntegerSet;

const lens = pzre.lens;
const debug = lens.debug;

const polymorphic_memory = pzre.structures.polymorphic_memory;
const List = polymorphic_memory.presets.single_ended.Create;
const MemoryModel = polymorphic_memory.MemoryModel;

pub fn polymorphicSetUnionInplace(
  comptime model: MemoryModel,
  gpa: Allocator,
  self: *List(model, null, Range),
  other: ?Set,
) Allocator.Error!void {
  try polymorphic_memory.polymorphicSetOperationInplace(
    model,
    ascii.Int,
    self,
    other,
    gpa,
    .binary,
    Set.@"union",
    Set.buf_size.@"union",
  );
}

pub fn polymorphicSetSubtractInplace(
  comptime model: MemoryModel,
  gpa: Allocator,
  self: *List(model, null, Range),
  other: ?Set,
) Allocator.Error!void {
  try polymorphic_memory.polymorphicSetOperationInplace(
    model,
    ascii.Int,
    self,
    other,
    gpa,
    .binary,
    Set.subtract,
    Set.buf_size.subtract,
  );
}

pub fn polymorphicSetComplementInplace(
  comptime model: MemoryModel,
  gpa: Allocator,
  self: *List(model, null, Range),
) Allocator.Error!void {
  try polymorphic_memory.polymorphicSetOperationInplace(
    model,
    ascii.Int,
    self,
    null,
    gpa,
    .unary,
    Set.complement,
    Set.buf_size.complement,
  );
}
