//! Library for parsing a regex to NFA or AST
//! Regexes are typically extremely small in size so an unoptimized simple parser is going to be fine
const std = @import("std");
const assert = std.debug.assert;
const Allocator = std.mem.Allocator;
const ArrayList = std.ArrayList;

const pzre = @import("../root.zig");

const lens = pzre.lens;
const debug = lens.debug;
const misc = pzre.misc;
const arch = pzre.arch;

const meta = pzre.meta;
const ascii = pzre.encoding.ascii;
const Range = ascii.Range;
const Set = ascii.IntegerSet;
const integer_set = pzre.structures.integer_set;
const Ast = pzre.Ast;

const polymorphic_memory = pzre.structures.polymorphic_memory;
const MemoryModel = polymorphic_memory.MemoryModel;
const pse = pzre.pse;

const lexer = compile.lexer;
const TokenType = lexer.TokenType;
const Token = lexer.Token;

const Repeat = misc.Repeat;
const Assertion = misc.Assertion;

const ast = pzre.ast;

const nfa = pzre.arch.minimal_nfa;

const compile = pzre.compile;
const MetaData = compile.parse_node.MetaData;
pub const ParseNode = compile.ParseNode;
const addWithOverflow = pzre.misc.addWithOverflow;
const mulWithOverflow = pzre.misc.mulWithOverflow;

const CountingAllocator = pzre.CountingAllocator;
const Sets = pzre.compile.Sets;
const Semantics = pzre.compile.Semantics;
const Limits = pzre.compile.Limits;
const Config = pzre.compile.Config;
const Features = pzre.compile.Features;

/// Whether to make an ast, nfa or both
pub const Action = enum {
  nfa,
  ast,
  metadata,
  sets,
};

pub const ParseError = error{
  /// A language object was in the process of being parsed, but an unexpected token was met
  UnexpectedToken,
  /// A language object was in the process of being parsed, but the stream ended unexpectedly
  UnexpectedEof,
  /// An empty set was encountered ([]). Note that this is different from epsilon: a{0,0}
  EmptySet,
  /// Dynamic allocator reached the defined upper bound on memory usage
  AllocationUpperbound,
  /// Zig std allocator error
  OutOfMemory,
  /// a{5,2}
  InvalidRepeat,
  /// a{999999999999999999999999999999}
  /// This is returned whenever an integer overflow was detected
  Overflow,
  /// Too many parenthesis, or AST too deep
  TooDeep,
  /// (a
  UnmatchedParenthesis,
  /// The pattern required too many states which would have violated config.max_states
  /// This has higher precedence than ContextTooSmall
  TooManyStates,
  /// The pattern required too many states which would have violated the fixed context definition
  /// This can never trigger for dynamic contexts
  ContextTooSmall,
  /// When arbitrary repetition user defined cap was encountered a{300000}
  TooHighArbitraryRepeat,
  /// The pattern contained maxInt
  /// The engine does not allow for matching against the encoding maxInt value
  MaxInt,
  /// [a-b-c]
  IllegalHyphenChain,
  /// [b-a]
  ReversedHyphenRange,
  /// [a-a]
  RedundantRange,
  /// [\s-b]
  IllegalHyphenOperand,
  /// config.max_unique_sets violation
  TooManySets,
};

pub fn ParseResult(
  comptime limits: Limits,
  comptime actions: std.EnumSet(Action),
  comptime model: MemoryModel,
  /// Null when an nfa is not being parsed
  comptime rbp: ?arch.RelativeBreakpoint,
  comptime sets_bp: ?arch.AbsoluteBreakpoint,
  ) type {

  const has_ast = actions.contains(.ast);
  const has_nfa = actions.contains(.nfa);
  const has_sets = actions.contains(.sets);

  const State = if (rbp != null) nfa.state.State(rbp.?, sets_bp.?);

  assert(if (rbp == null) !actions.contains(.nfa) else true);

  const SetList = polymorphic_memory.presets.single_ended.Create(model, null, Set);
  const AstList = polymorphic_memory.presets.single_ended.Create(model, null, ast.Node);
  const Node = ParseNode(limits, actions, model, rbp, sets_bp);

  return struct {
    meta_data:  MetaData,
    sets:       if (has_sets) []const Set      else void,
    ast:        if (has_ast)  ast.Ast          else void,
    nfa_states: if (has_nfa)  []const State    else void,

    const Self = @This();

    fn nfaStatesPostCompilationSanityCheck(states: []const State) void {
      // debug.prettyPrint(.{.sanity_states = states});
      for (states) |state| {
        switch (state.tag) { // check that alt jumps truly jump, and other dont
          .jump, .term_char_alt_jump, .term_set_alt_jump, .split, .word_boundary_alt_jump, .not_word_boundary_alt_jump, .line_start_alt_jump, .line_end_alt_jump, .text_end_alt_jump, .text_start_alt_jump => {
            assert(state.alt_jump != 0 and state.alt_jump != 1);
          },
          .term_set, .term_char, .line_start, .line_end, .text_start, .text_end, .word_boundary, .not_word_boundary => {
            assert(state.alt_jump == 0);
          }
        }
      }
    }

    /// Grabs ownership over all instances
    /// Consumes setlist, astlist and the result
    /// Assumes 'result' is an untouched, fresh result of a parse
    pub fn fromParseNode(gpa: Allocator, setlist: *SetList, astlist: *AstList, result: *Node) ParseError!Self {
      result.data = result.data.accept();
      var setlist_freed = false;
      var astlist_freed = false;
      var statelist_freed = false;
      errdefer {
        if ((comptime has_sets) and !setlist_freed) misc.destroySetslist(model, gpa, setlist);
        if ((comptime has_ast) and !astlist_freed) astlist.deinit(gpa);
        if ((comptime has_nfa) and !statelist_freed) result.nfa.destroy(gpa);
      }
      try result.data.validate(limits);

      const states = if (comptime has_nfa) b: {
        const states = try result.nfa.accept(gpa);
        statelist_freed = true;
        nfaStatesPostCompilationSanityCheck(states);
        break :b states;
      } else {};
      errdefer if (has_nfa and !@inComptime()) gpa.free(states);
      // Result is deinited from this point onwards

      const sets_slice = if (comptime has_sets) b: {
        const slice = try setlist.toOwnedConstSlice(gpa);
        setlist_freed = true;
        break :b slice;
      } else {};
      errdefer if (has_sets) misc.destroySets(gpa, sets_slice);

      const ast_nodes = if (comptime has_ast) b: {
        const nodes = try astlist.toOwnedConstSlice(gpa);
        defer astlist_freed = true;
        break :b nodes;
      } else {};
      errdefer if (has_ast and !@inComptime()) gpa.free(ast_nodes);

      return Self{
        .meta_data = result.data,
        .nfa_states = states,
        .ast = if (comptime has_ast) Ast{.nodes = ast_nodes, .root = result.ast},
        .sets = sets_slice,
      };
    }

    pub fn destroy(self: *Self, gpa: Allocator) void {
      if (!@inComptime()) {
        if (comptime has_sets) self.destroySets(gpa);
        self.destroyAst(gpa);
        self.destroyNfa(gpa);
      }
    }

    pub fn destroyAst(self: *Self, gpa: Allocator) void {
      if (!@inComptime()) {
        gpa.free(self.ast_nodes);
      }
    }

    pub fn destroyNfa(self: *Self, gpa: Allocator) void {
      if (!@inComptime()) {
        gpa.free(self.nfa_states);
      }
    }

    pub fn destroySets(self: *Self, gpa: Allocator) void {
      if (@inComptime()) return;
      comptime assert(has_sets);
      pzre.misc.destroySets(gpa, self.sets);
    }
  };
}

/// We assume trusted input for comptime analyzed/compiled patterns 
/// 
pub fn Parser(
  comptime sets: Sets,
  comptime semantics: Semantics,
  comptime limits: Limits,
  comptime features: Features,
  /// The objects being built
  comptime actions: std.EnumSet(Action),
  comptime model: MemoryModel,
  /// Null when an nfa is not being parsed
  comptime rbp: ?arch.RelativeBreakpoint,
  comptime sets_bp: ?arch.AbsoluteBreakpoint,
) type {
  _ = features;

  return struct {
    lexer: Lexer = .empty,
    /// The characters seen while parsing a set [abc...]
    /// Resets each time a set has finished parsing
    set_chars: CharList = .empty,
    /// The set currently being parsed
    /// Resets each time a set has finished parsing
    set: RangeList = .empty,
    depth: usize = 0,

    const Self = @This();
    pub const SetList = polymorphic_memory.presets.single_ended.Create(model, null, Set);
    pub const RangeList = polymorphic_memory.presets.single_ended.Create(model, null, Range);
    pub const CharList = polymorphic_memory.presets.single_ended.Create(model, null, u8);
    pub const AstList = polymorphic_memory.presets.single_ended.Create(model, null, ast.Node);

    pub const Node = ParseNode(limits, actions, model, rbp, sets_bp);
    pub const Result = ParseResult(limits, actions, model, rbp, sets_bp);

    pub const Lexer = lexer.Lexer(semantics);

    pub const State = if (rbp != null) nfa.state.State(rbp.?, sets_bp.?);

    pub const new: Self = .{};

    /// Prepares the parser for a new parse
    inline fn prepareNewParse(self: *Self, pattern: []const u8) void {
      // if (!@inComptime()) debug.prettyPrint(.{.new_parse = pattern});
      self.depth = 0;
      self.lexer = .init(pattern);
    }

    pub fn parseComptime(comptime self: *Self, comptime pattern: []const u8) ParseError!Result {
      comptime return self.parseUnbounded(undefined, pattern);
    }

    /// Parser an nfa, ast or both
    /// Parses without bounding memory usage
    /// Parses at runtime or comptime
    pub fn parseUnbounded(self: *Self, gpa: Allocator, pattern: []const u8) ParseError!Result {

      var setlist: SetList = .empty;
      var astlist: AstList = .empty;

      var result = b: {

        self.prepareNewParse(pattern);

        if (self.lexer.peek() == null) {
          var epsilon = try Node.createEpsilon(gpa, &astlist);
          return try Result.fromParseNode(gpa, &setlist, &astlist, &epsilon);
        }

        errdefer if (!@inComptime()) {
          for (setlist.getConstSlice()) |set| set.deinit(gpa);
          setlist.deinit(gpa);
          astlist.deinit(gpa);
        };

        var result =  try self.parseUnion(gpa, &setlist, &astlist);
        // if (!@inComptime()) debug.prettyPrint(.{result, astlist.getConstSlice()});
        errdefer result.destroy(gpa);

        if (self.lexer.peek()) |_| return error.UnexpectedToken;
        break :b result;
      };

      return try Result.fromParseNode(gpa, &setlist, &astlist, &result);
    }

    fn parseUnion(self: *Self, gpa: Allocator, setlist: *SetList, astlist: *AstList) ParseError!Node {
      var lhs = try self.parseConcat(gpa, setlist, astlist);

      while (true) {
        var rhs = b: {
          errdefer lhs.destroy(gpa);
          const t = self.lexer.peek() orelse return lhs;
          switch (t.type) {
            .rpar => return lhs,
            .pipe => self.discard(),
            else => return error.UnexpectedToken,
          }
          break :b try self.parseConcat(gpa, setlist, astlist);
        };

        // the union function grabs ownership over lhs and rhs
        // it will free both on error
        lhs = try lhs.@"union"(gpa, &rhs, astlist);
      }
    }

    /// parses a concatenation: aa
    fn parseConcat(self: *Self, gpa: Allocator, setlist: *SetList, astlist: *AstList) ParseError!Node {
      // This is only called from parseUnion, therefore we can ensure all epsilon-implied-by-union
      // is correctly handled here
      if (self.lexer.peek()) |peek| {
        switch (peek.type) {
          .pipe, .rpar => return Node.createEpsilon(gpa, astlist),
          else => {},
        }
      } else return Node.createEpsilon(gpa, astlist);

      var lhs = try self.parseQuantifier(gpa, setlist, astlist);

      while (true) {
        const t = self.lexer.peek() orelse return lhs;
        switch (t.type) {
          .pipe, .rpar => return lhs,
          else => {}
        }

        var rhs = b: {
          errdefer lhs.destroy(gpa);
          break :b try self.parseQuantifier(gpa, setlist, astlist);
        };

        // the concat function grabs ownership over lhs and rhs
        // it will free both on error
        lhs = try lhs.concat(gpa, &rhs, astlist);
      }
    }

    /// Parses a quantifiers such as: a* , a{1,2}
    /// Expects that there is a term to be parsed.
    fn parseQuantifier(self: *Self, gpa: Allocator, setlist: *SetList, astlist: *AstList) ParseError!Node {
      var term = try self.parseTerm(gpa, setlist, astlist);

      while (self.lexer.peek()) |peek| {
        const repeat_amount: Repeat = switch (peek.type) {
          .repeat_sym => b: {
            const op = self.lexer.next().?;
            break :b switch (op.lexeme[0]) {
              '*' => Repeat{.min = 0, .max = null},
              '+' => Repeat{.min = 1, .max = null},
              '?' => Repeat{.min = 0, .max = 1},
              else => unreachable,
            };
          },
          .lbrace => b: {
            errdefer term.destroy(gpa);
            self.discard();
            const start = try self.parseNumber(usize) orelse {
              return if (self.lexer.peek() == null) error.UnexpectedEof else error.UnexpectedToken;
            };
            const maybe_comma = try self.lexerNextAssert();
            if (maybe_comma.type == .comma) {
              const end = try self.parseNumber(usize) orelse {
                try self.consume(.rbrace);
                break :b Repeat{.min = start, .max = null};
              };

              try self.consume(.rbrace);
              break :b Repeat{.min = start, .max = end};
            } else if (maybe_comma.type == .rbrace) {
              break :b Repeat{.min = start, .max = start};
            } else return error.UnexpectedToken;
          },
          else => break, 
        };

        try term.repeatExact(gpa, repeat_amount, astlist);
      }

      return term;
    }

    /// Parses the base unit type: Literal | Set | ...
    /// Expects that there is a term to be parsed.
    fn parseTerm(self: *Self, gpa: Allocator, setlist: *SetList, astlist: *AstList) ParseError!Node {
      const t = try self.lexerNextAssert();
      switch (t.type) {
        .set_start => {
         
          defer self.set_chars.clearRetainingCapacity();
          defer self.set.clearRetainingCapacity();

          const second_char = try self.lexerPeekAssert();

          // FIRST caret is negation
          const is_complement = second_char.type == .caret;
          if (is_complement) {
             self.discard();
          }
          var is_first = true;

          var perl_set_expects_hyphen_ends = false;

          while (true) {
            defer is_first = false;
            const next = try self.lexerNextAssert();
           
            switch (next.type) {
              .set_end => {
                if (is_first) {
                  try self.setContextReadChar(gpa, next.lexeme[0]);
                  continue;
                }
               
                if (self.set_chars.len() > 0) {
                  const chars_set = if (@inComptime()) b: {
                    break :b Set.fromSliceComptime(u8, self.set_chars.getConstSlice());
                  } else b: {
                    break :b try Set.fromSlice(u8, gpa, self.set_chars.getConstSlice());
                  };
                  defer if (!@inComptime()) chars_set.deinit(gpa);
                  try pse.polymorphicSetUnionInplace(model, gpa, &self.set, chars_set);
                }
                const parsed_set = Set{.ranges = self.set.getConstSlice()};
                return createSetFragment(gpa, setlist, astlist, parsed_set, is_complement);
              },
              .hyphen => {
                // hyphens are treated as literals if and only if one of its operands is missing
                // e.g. [-az] or [az-]
                // [a-z] here it is interpreted as the range operator
                // [a-b-z] this is a syntax error
                // [a-b\-z] correct
               
                // The setContextReadChar function handles the hyphen ranges
                // Therefore, only the following cases can hold

                const char = next.lexeme[0];
                if (is_first) {
                  try self.setContextReadChar(gpa, char);
                } else if (self.lexer.peek()) |peek| {
                  if (peek.type == .set_end) {
                    try self.setContextReadChar(gpa, char);
                  } else if (perl_set_expects_hyphen_ends) {
                    return error.IllegalHyphenOperand;
                  } else {
                    // The hyphen is not in the end or the start
                    // Therefore its a range operator
                    // BUT the previous character is already an operand
                    //    or some other non-char character
                   
                    return error.IllegalHyphenChain;
                  }
                } else {
                  return error.UnexpectedEof;
                }
              },
              .perl_set => {
                if (self.lexer.peek()) |peek| {
                  if (peek.type == .hyphen) {
                    @branchHint(.cold);
                    perl_set_expects_hyphen_ends = true;
                  }
                }
               
                const decoded = decodePerlSet(next);
                try pse.polymorphicSetUnionInplace(model, gpa, &self.set, decoded);
              },
              else => { // try to read char
                const char = try parseSetChar(next);
                try self.setContextReadChar(gpa, char);
              }
            }
          }
        },
        .perl_set => {
          const decoded = decodePerlSet(t);
          return createSetFragment(gpa, setlist, astlist, decoded, false);
        },
        .lpar => {
          if (self.depth >= limits.max_depth) return error.TooDeep;
          self.depth += 1;

          if (self.lexer.peek()) |n| {
            if (n.type == .rpar) {
              _ = self.discard();
              return try Node.createEpsilon(gpa, astlist);
            }
          } else return error.UnmatchedParenthesis;

          var inner = try self.parseUnion(gpa, setlist, astlist);
          errdefer inner.destroy(gpa);
          self.consume(.rpar) catch return error.UnmatchedParenthesis;
          return inner;
        },
        .char => {
          if (t.lexeme[0] == std.math.maxInt(u8)) {
            @branchHint(.cold);
           return error.MaxInt;
          }
          return createCharFragment(gpa, setlist, astlist, t.lexeme[0]);
        },
        .digit, .comma, .hyphen => {
          return createCharFragment(gpa, setlist, astlist, t.lexeme[0]);
        },
        .hex_sequence => {
          const val = try parseHexSequence(t);
          if (val == std.math.maxInt(u8)) {
            @branchHint(.cold);
           return error.MaxInt;
          }
          return createCharFragment(gpa, setlist, astlist, val);
        },
        .escape_sequence => {
          const translated = translateEscapeSequence(t.lexeme[0..2]);
          return createCharFragment(gpa, setlist, astlist, translated);
        },
        .assert_escape_sequence => {
          const ass: Assertion = switch (t.lexeme[1]) {
            'b' => .word_boundary,
            'B' => .not_word_boundary,
            'A' => .text_start,
            'z' => .text_end,
            else => unreachable,
          };
          return createAssertionFragment(gpa, astlist, ass);
        },
        .caret => {
          const ass: Assertion = if (semantics.multiline) .line_start else .text_start;
          return createAssertionFragment(gpa, astlist, ass);
        },
        .dollar => {
          const ass: Assertion = if (semantics.multiline) .line_end else .text_end;
          return createAssertionFragment(gpa, astlist, ass);
        },
        .any => {
          const dot_set = if (semantics.dotall) sets.dotall_set else sets.dot_set;
          return createSetFragment(gpa, setlist, astlist, dot_set, false);
        },
        .unexpected_eof => return error.UnexpectedEof,
        .not_recognized => return error.UnexpectedToken,
        .set_end, .rpar, .lbrace, .rbrace, .pipe, .repeat_sym => return error.UnexpectedToken,
      }
    }
   
    fn setContextReadChar(self: *Self, gpa: Allocator, char: u8) ParseError!void {
      const peek = try self.lexerPeekAssert();

      if (peek.type == .hyphen) {
        const hyphen = try self.lexerNextAssert();
        const end_token_peek = try self.lexerPeekAssert();
        switch (end_token_peek.type) {
          .set_end => {
            try appendCharList(&self.set_chars, gpa, char);
            try appendCharList(&self.set_chars, gpa, hyphen.lexeme[0]);
            return;
          },
          .perl_set => {
            return error.IllegalHyphenOperand;
          },
          else => {},
        }

        const end_token = try self.lexerNextAssert();
        const end = try parseSetChar(end_token);

        const start = char;
        if (end < start) return error.ReversedHyphenRange;
        if (end == start) return error.RedundantRange;
        try pse.polymorphicSetUnionInplace(
          model,
          gpa,
          &self.set,
          Set{ .ranges = &.{ Range.init(start, try addWithOverflow(end, 1)) } },
        );
      } else {
        try appendCharList(&self.set_chars, gpa, char);
      }
    }


    inline fn appendCharList(list: *CharList, gpa: Allocator, char: u8) ParseError!void {
      if (char == std.math.maxInt(u8)) {
        @branchHint(.cold);
        return error.MaxInt;
      }
      try list.append(gpa, char);
    }

    inline fn createAssertionFragment(gpa: Allocator, astlist: *AstList, assertion: Assertion) ParseError!Node {
      return Node.create(gpa, astlist, .{ .assertion = assertion });
    }

    inline fn createCharFragment(gpa: Allocator, setlist: *SetList, astlist: *AstList, char: u8) ParseError!Node {
      return if (semantics.ignore_case) {
       
        const range: Range = .init(char, try addWithOverflow(char, 1));
        const set: Set = .init(&.{range});
        return try createSetFragment(gpa, setlist, astlist, set, false);
      } else Node.create(gpa, astlist, .{ .char = char });
    }

    /// Creates a new fragment and adds the set to the sets list if needed
    /// 'set' is untouched
    fn createSetFragment(gpa: Allocator, setlist: *SetList, astlist: *AstList, set: Set, is_complement: bool) ParseError!Node {

      if (comptime actions.contains(.sets)) {

        var owned: bool = false;

        // Case ignoration is supported on the lowest level
        var modified: Set = if (comptime semantics.ignore_case) IncludeCases: {
          const lower_mask = ascii.Set.LOWER;
          const upper_mask = ascii.Set.UPPER;

          const ascii_distance = 32;

          if (@inComptime()) {
            const lower_cut = set.intersectComptime(lower_mask);
            const upper_cut = set.intersectComptime(upper_mask);

            const missing_upper = lower_cut.shiftComptime(.sub, ascii_distance);
            const missing_lower = upper_cut.shiftComptime(.add, ascii_distance);

            const missing = missing_upper.unionComptime(missing_lower);
            const extended = set.unionComptime(missing);
            break :IncludeCases extended;
          } else {
            var missing_upper = b: {
              var lower_cut = try set.intersectAlloc(lower_mask, gpa);
              errdefer lower_cut.deinit(gpa);
              try lower_cut.shiftInplace(.sub, ascii_distance, gpa);
              break :b lower_cut;
            };

            var missing_lower = b: {
              errdefer missing_upper.deinit(gpa);

              var upper_cut = try set.intersectAlloc(upper_mask, gpa);
              errdefer upper_cut.deinit(gpa);
              try upper_cut.shiftInplace(.add, ascii_distance, gpa);
              break :b upper_cut;
            };

            var missing = b: {
              errdefer missing_upper.deinit(gpa);
              errdefer missing_lower.deinit(gpa);

              try missing_lower.unionInplace(missing_upper, gpa);
              break :b missing_lower;
            };
            defer missing_upper.deinit(gpa);
            defer missing.deinit(gpa);

            if (!missing.isEmptySet()) {
              const extended = try set.unionAlloc(missing, gpa);
              owned = true;
              break :IncludeCases extended;
            } else break :IncludeCases set;
          }
        } else set;
        errdefer if ((!@inComptime()) and owned) modified.deinit(gpa);

        modified = if (is_complement) b: {
          if (@inComptime()) {
            const complement = modified.complementComptime();
            break :b complement;
          } else {
            const complement = try modified.complementAlloc(gpa);
            if (owned) modified.deinit(gpa);
            owned = true;
            break :b complement;
          }
        } else modified;

        modified = if ((semantics.never_implicit_newline) and is_complement) b: {
          const nl = comptime Set.fromSliceComptime(u8, "\n\r");

          if (@inComptime()) {
            const nonl = modified.subtractComptime(nl);
            break :b nonl;
          } else {
            const nonl = try modified.subtractAlloc(nl, gpa);
            if (owned) modified.deinit(gpa);
            owned = true;
            break :b nonl;
          }
        } else modified;

        if (modified.isEmptySet()) {
          if ((!@inComptime()) and owned) modified.deinit(gpa);
          return error.EmptySet;
        }

        var set_idx: ?usize = for (setlist.getConstSlice(), 0..) |existing, i| {
          if (existing.equal(modified)) break i;
        } else null;

        if (set_idx == null) {
          set_idx = setlist.len();
          if ((!@inComptime()) and !owned) {
            modified = try modified.dupe(gpa);
            owned = true;
          }

          var node = try Node.create(gpa, astlist, .{ .set_idx = set_idx.? });
          errdefer node.destroy(gpa);
          try setlist.append(gpa, modified);
          return node;
        } else {

          // Avoid double free above
          const node = try Node.create(gpa, astlist, .{ .set_idx = set_idx.? });
          if ((!@inComptime()) and owned) modified.deinit(gpa);
          return node;
        }

      } else {
        return try Node.create(gpa, astlist, .{ .set_idx = 0 });
      }
    }

    /// Returns null on nothing read
    fn parseNumber(self: *Self, comptime T: type) ParseError!?T {
      const start_idx = self.lexer.head;
      var amount_read: usize = 0;
      while (self.lexer.peek()) |d| {
        if (d.type == .digit) {
          self.discard();
          amount_read += 1;
        } else break;
      }
      if (amount_read == 0) return null;
      return std.fmt.parseInt(T, self.lexer.pattern[start_idx..start_idx + amount_read], 10) catch |err| switch (err) {
        error.Overflow => error.Overflow,
        else => error.UnexpectedToken,
      };
    }

    fn decodePerlSet(perl_set: Token) Set {
      assert(perl_set.type == .perl_set);
      const c = perl_set.lexeme[1];
      inline for (lexer.perl_set_letters_string) |comptime_c| {

        if (c == comptime_c) {
          if (comptime semantics.never_implicit_newline) {

            const set = comptime decodePerlSetInner(comptime_c);
            const no_nl = comptime set.subtractComptime(.fromSliceComptime(u8, "\r\n"));
            return no_nl;

          } else {
            return comptime decodePerlSetInner(comptime_c);
          }
        }

      } else unreachable; // perl sets internal bug
    }

    fn decodePerlSetInner(comptime c: u8) Set {
      comptime return switch (c) {
        'd' => sets.digit_set,
        'D' => sets.digit_set.complementComptime(),
        's' => sets.whitespace_set,
        'S' => sets.whitespace_set.complementComptime(),
        'w' => sets.word_set,
        'W' => sets.word_set.complementComptime(),
        else => unreachable, // unhandled case
      };
    }

    // Consumes a token while asserting its type
    inline fn consume(self: *Self, t: TokenType) ParseError!void {
      const next = try self.lexerNextAssert();
      if (next.type != t) return error.UnexpectedToken;
    }

    // Discards a token blindly
    inline fn discard(self: *Self) void {
      _ = self.lexer.next();
    }

    inline fn translateEscapeSequence(seq: *const [2]u8) u8 {
      assert(seq[0] == '\\');
      const target = seq[1];
      const char = switch (target) {
        ' ' => ' ',  // whitespace
        '0' => 0x00, // null
        'a' => 0x07, // bell
        'f' => 0x0C, // form feed
        'e' => 0x1B, // escape
        't' => 0x09, // tabulator
        'n' => 0x0A, // newline
        'r' => 0x0D, // carriage return
        'v' => 0x0B, // vertical tab
        else => target,
      };
      return char;
    }

    // normalizes almost all tokens into basic characters
    // for set context only
    // hyphen is not handled by this function in order to only allow hyphens in certain cases
    inline fn parseSetChar(token: Token) ParseError!u8 {
      const EU = ParseError!u8;
     
      const t = try switch (token.type) {
        .escape_sequence => @as(EU, translateEscapeSequence(token.lexeme[0..2])),
        .hex_sequence => parseHexSequence(token),
        .hyphen, .dollar, .char, .digit, .rbrace, .lbrace, .lpar, .rpar, .set_start, .comma, .caret, .pipe, .repeat_sym, .any => @as(EU, token.lexeme[0]),
        .set_end, .not_recognized, .perl_set, .assert_escape_sequence => error.UnexpectedToken,
        .unexpected_eof => error.UnexpectedEof
      };
      if (t == std.math.maxInt(u8)) {
        @branchHint(.cold);
       return error.MaxInt;
      }
      return t;
    }

    inline fn parseHexSequence(token: Token) ParseError!u8 {
      assert(token.lexeme.len == 4);
      const hex = token.lexeme[2..4];
      return std.fmt.parseInt(u8, hex, 16) catch |err| switch (err) {
        error.Overflow => error.Overflow,
        else => error.UnexpectedToken,
      };
    }

    inline fn lexerNextAssert(self: *Self) ParseError!Token {
      return self.lexer.next() orelse error.UnexpectedEof;
    }

    inline fn lexerPeekAssert(self: *Self) ParseError!Token {
      return self.lexer.peek() orelse error.UnexpectedEof;
    }

    pub fn deinit(self: *Self, gpa: Allocator) void {
      if (@inComptime()) return;
      self.set_chars.deinit(gpa);
      self.set.deinit(gpa);
    }
  };
}
