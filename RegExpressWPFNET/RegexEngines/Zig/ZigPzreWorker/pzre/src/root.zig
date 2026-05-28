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

pub const compile = @import("compile.zig");

pub const tests = @import("tests/test.zig");
pub const ast = @import("ast/ast.zig");
pub const Ast = ast.Ast;

pub const nfa = @import("nfa/nfa.zig");
pub const Sate = nfa.state.State;
pub const Context = nfa.context.Context;
pub const Pool = nfa.context.Pool;
pub const Match = nfa.Match;
pub const Replacement = nfa.Replacement;
pub const ManyReplacements = nfa.ManyReplacements;
pub const Config = language.Config;

pub const parse = @import("parse.zig");
pub const parse_node = @import("parse_node.zig");
pub const Parser = parse.Parser;
pub const ParseError = parse.ParseError;
pub const ParseResult = parse.ParseResult;
pub const MetaData = parse_node.MetaData;

pub const lexer = @import("lexer.zig");
pub const Lexer = lexer.Lexer;
pub const pse = @import("polymorphic_set_extensions.zig");
pub const misc = @import("misc.zig");

pub const language = @import("language.zig");
pub const Semantics = language.Semantics;
pub const Limits = language.Limits;
pub const Sets = language.Sets;

comptime {
  if (@import("builtin").is_test) {
    _ = @import("compile.zig");
    _ = @import("ast/ast.zig");
    _ = @import("nfa/nfa.zig");
    _ = @import("parse.zig");
    _ = @import("parse_node.zig");
    _ = @import("lexer.zig");
    _ = @import("polymorphic_set_extensions.zig");
    _ = @import("misc.zig");
    _ = @import("language.zig");
    _ = @import("tests/test.zig");
    _ = @import("showcase.zig");
  }
}
