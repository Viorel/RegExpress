pub const lens = @import("lens");

pub const structures = @import("structures/structures.zig");
pub const MemoryModel = structures.polymorphic_memory.MemoryModel;
pub const List = structures.polymorphic_memory.presets.single_ended.Create;

pub const encoding = @import("encoding/encoding.zig");
pub const ascii = encoding.ascii;
pub const Set = ascii.IntegerSet;
pub const Range = ascii.Range;

pub const counting_allocator = @import("mem/counting_allocator.zig");
/// Allocator that enforces a memory usage upper-ceiling
/// The 'compile.zig' api automatically wraps allocators with this
pub const CountingAllocator = counting_allocator.CountingAllocator;

pub const meta = @import("meta/meta.zig");

pub const compile = @import("compile/compile.zig");

pub const tests = @import("tests/test.zig");
pub const ast = @import("ast/ast.zig");
pub const Ast = ast.Ast;

pub const arch = @import("arch/arch.zig");
pub const Arch = arch.Arch;
pub const ArchResolved = arch.ArchResolved;

pub const minimal_nfa = @import("arch/minimal_nfa/nfa.zig");

pub const regex = @import("regex.zig");
pub const Regex = regex.Regex;
pub const Match = regex.Match;
pub const Replacement = regex.Replacement;
pub const ManyReplacements = regex.ManyReplacements;

pub const anyregex = @import("anyregex.zig");
pub const AnyRegex = anyregex.AnyRegex;

pub const pse = @import("polymorphic_set_extensions.zig");
pub const misc = @import("misc.zig");

comptime {
  if (@import("builtin").is_test) {
    _ = @import("arch/arch.zig");
    _ = @import("ast/ast.zig");
    _ = @import("compile/compile.zig");
    _ = @import("encoding/encoding.zig");
    _ = @import("mem/counting_allocator.zig");
    _ = @import("meta/meta.zig");
    _ = @import("misc.zig");
    _ = @import("polymorphic_set_extensions.zig");
    _ = @import("regex.zig");
    _ = @import("anyregex.zig");
    _ = @import("structures/structures.zig");
    _ = @import("tests/test.zig");
  }
}
