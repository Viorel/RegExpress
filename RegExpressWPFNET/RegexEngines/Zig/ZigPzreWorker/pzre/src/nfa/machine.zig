//! An extremely minimal and optimal matching machine with limited features
const std = @import("std");
const assert = std.debug.assert;

const pzre = @import("../root.zig");

const nfa = pzre.nfa;

const lens = pzre.lens;
const debug = lens.debug;
const lists = nfa.lists;

const ascii = pzre.encoding.ascii;
const Set = ascii.IntegerSet;
const Tag = nfa.state.Tag;

const misc = pzre.misc;

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

  /// Comptime check for branching improvement
  ///
  /// When the NFA the iteration bound, it treats it as the final iteration.
  ///   Works in reverse
  use_iteration_bound: bool = false,
};

/// Pattern maching machine logic
/// 
/// Does not own any fields, no deinit
///
pub fn Machine(
  comptime word_set: Set,
  comptime context_breakpoint: nfa.state.Breakpoint,
  comptime context_mode: nfa.context.Mode,
  comptime breakpoint: nfa.state.Breakpoint,
) type {
  const State = nfa.state.State(breakpoint);
  const Idx = State.Idx;

  return struct {
    ctx: *Context,
    states: []const State,

    pub const Context = nfa.context.Context(context_mode, context_breakpoint);
    const Self = @This();

    /// Expects ctx to be reset
    pub fn init(ctx: *Context, states: []const State) Self {
      return .{.states = states, .ctx = ctx};
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
    pub fn matches(self: Self, comptime opts: MatchOpts, sets: []const Set, input: []const u8, start_idx: usize, iteration_bound: usize) ?usize {
      self.ctx.reset();
      const start_list_id = self.ctx.list_id;

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
        // .sets = sets,
        // .states = self.states
      // });
      //
      // if (!@inComptime()) debug.prettyPrint(.{.post_start = self});
      var input_idx = start_idx;
      
      if (comptime !opts.iterate_reverse) {
        self.startlist(opts, input, input_idx);

        while (input_idx < iteration_bound and self.ctx.currentlist().len > 0) {
          @branchHint(.likely);
          // debug.prettyPrint(.{.current_lists = self.ctx.currentlist()});
          if (comptime opts.non_greedy) {
            if (self.ctx.previous_accept_append_list_id != null) break;
          }

          defer input_idx += 1;
          self.step(opts, sets, input, input_idx);
          self.ctx.swaplists();
          // debug.prettyPrint(.{.lists_post = self.ctx.currentlist()});
        }

      } else {
        self.startlist(opts, input, input_idx);


        while (input_idx > iteration_bound and self.ctx.currentlist().len > 0) {
          @branchHint(.likely);
          if (comptime opts.non_greedy) {
            if (self.ctx.previous_accept_append_list_id != null) break;
          }

          input_idx -= 1;
          self.step(opts, sets, input, input_idx);
          self.ctx.swaplists();
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
    fn ismatch(self: Self, comptime opts: MatchOpts, start_idx: usize, start_list_id: usize) ?usize {

      if (self.ctx.previous_accept_append_list_id) |absolute_idx| {
        const prev_append_consumed = absolute_idx - start_list_id - 1;

        if (comptime !opts.iterate_reverse) {
          return start_idx + prev_append_consumed;
        } else {
          return start_idx - prev_append_consumed;
        }

      }
      return null;
    }

    fn startlist(self: Self, comptime opts: MatchOpts, input: []const u8, input_idx: usize) void {
      self.ctx.incrementListId();
      self.addstate(opts, input, input_idx, self.ctx.current_list_idx, 0);
    }

    /// input_idx is the next index that has not been consumed yet. 0 implies that no chars have been consumed yet. input_idx == input.len implies that no more chars will be consumed
    fn addstate(self: Self, comptime opts: MatchOpts, input: []const u8, input_idx: usize, list_idx: u1, state_idx: Idx) void {
      if (self.ctx.stateLastlist(state_idx) == self.ctx.list_id) return;
      self.ctx.setStateLastlist(state_idx, self.ctx.list_id);

      // NOTE: any reason to remove recursion from here? Limits already make the 
      //  machine ReDoS/stack overflow immune
      // NOTE: assertions should never be evaluated twice without the machine stepping due to AST optimizations

      const state = self.states.ptr[state_idx];
      switch (state.tag) {
        .split => {
          @branchHint(.likely);
          self.addstate(opts, input, input_idx, list_idx, state_idx + 1);
          const jump_idx = State.castAltPath(state_idx, state.alt_jump);
          self.addstate(opts, input, input_idx, list_idx, jump_idx);
        },
        .jump => {
          @branchHint(.unlikely);
          const jump_idx = State.castAltPath(state_idx, state.alt_jump);
          self.addstate(opts, input, input_idx, list_idx, jump_idx);
        },
        .accept => {
          // NOTE: ACCEPT IS NEVER ADDED TO THE LISTS
          @branchHint(.unlikely);
          self.ctx.previous_accept_append_list_id = self.ctx.list_id;
        },
        .term_char, .term_set, => {
          @branchHint(.likely);
          self.ctx.appendList(list_idx, state_idx);
        },

        .term_char_alt_jump, .term_set_alt_jump => {
          self.ctx.appendList(list_idx, state_idx);
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
            const jump_idx = State.castAltPath(state_idx, state.alt_jump);
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
            const jump_idx = State.castAltPath(state_idx, state.alt_jump);
            self.addstate(opts, input, input_idx, list_idx, jump_idx);
          }
        },

        .word_boundary => {
          @branchHint(.unlikely);
          const left = if (input_idx == 0) false else word_set.contains(u8, input[input_idx - 1]);
          const right = if (input_idx == input.len) false else word_set.contains(u8, input[input_idx]);
          
          if (left != right) self.addstate(opts, input, input_idx, list_idx, state_idx + 1);
        },
        .word_boundary_alt_jump => {
          @branchHint(.unlikely);
          const left = if (input_idx == 0) false else word_set.contains(u8, input[input_idx - 1]);
          const right = if (input_idx == input.len) false else word_set.contains(u8, input[input_idx]);
          
          if (left != right) {
            const jump_idx = State.castAltPath(state_idx, state.alt_jump);
            self.addstate(opts, input, input_idx, list_idx, jump_idx);
          }
        },

        .not_word_boundary => {
          @branchHint(.unlikely);
          const left = if (input_idx == 0) false else word_set.contains(u8, input[input_idx - 1]);
          const right = if (input_idx == input.len) false else word_set.contains(u8, input[input_idx]);
          
          if (left == right) self.addstate(opts, input, input_idx, list_idx, state_idx + 1);
        },
        .not_word_boundary_alt_jump => {
          @branchHint(.unlikely);
          const left = if (input_idx == 0) false else word_set.contains(u8, input[input_idx - 1]);
          const right = if (input_idx == input.len) false else word_set.contains(u8, input[input_idx]);
          
          if (left == right) {
            const jump_idx = State.castAltPath(state_idx, state.alt_jump);
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
            const jump_idx = State.castAltPath(state_idx, state.alt_jump);
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
            const jump_idx = State.castAltPath(state_idx, state.alt_jump);
            self.addstate(opts, input, input_idx, list_idx, jump_idx);
          }
        },
      }
    }

    /// Steps the automata one step forward, checking against an input character
    /// When 'step' begins executing, we say that input[input_idx] has been consumed.
    /// Each step consumes exactly one character from the input
    fn step(self: Self, comptime opts: MatchOpts, sets: []const Set, input: []const u8, input_idx: usize) void {
      // debug.prettyPrint(.{
      //   "stepping", input_idx, input,
      // });

      const c = input.ptr[input_idx];
      self.ctx.incrementListId();
      self.ctx.clearNext();
      const nlist = self.ctx.nextlistIdx();
      const next_input_idx = if (comptime opts.iterate_reverse) input_idx else input_idx + 1;

      // Alt jumps are generated to be rare, we aim for term_char and term_set being likely
      // Usually, the NFA is in multiple states at once, evaluating all possibilities
      // The current input character is fixed, and the states point to various different terms
      // This is especially true when you consider AST optimizations
      // As such, it makes sense to think that it is unlikely for a term if statement to match

      for (self.ctx.currentlist()) |i| {
        const state = self.states.ptr[i];


        // debug.prettyPrint(.{.in = state, .comparing = c});

        // NOTE: should the matching if statements be @branchHint(.unlikely)?

        switch (state.tag) {
          .term_char => {
            @branchHint(.likely);
            if (c == state.term.char.value) {
              @branchHint(.unlikely);
              self.addstate(opts, input, next_input_idx, nlist, @truncate(i + 1));
            }
          },
          .term_char_alt_jump => {
            if (c == state.term.char.value) {
              @branchHint(.unlikely);
              assert(i <= std.math.maxInt(Idx));
              const jump_idx = State.castAltPath(@truncate(i), state.alt_jump);
              self.addstate(opts, input, next_input_idx, nlist, jump_idx);
            }
          },
          .term_set => {
            @branchHint(.likely);
            // debug.prettyPrint(.{set});
            if (sets.ptr[state.term.set_idx].contains(u8, c)) {
              @branchHint(.unlikely);
              self.addstate(opts, input, next_input_idx, nlist, @truncate(i + 1));
            }
          },
          .term_set_alt_jump => {
            // debug.prettyPrint(.{set});
            if (sets.ptr[state.term.set_idx].contains(u8, c)) {
              @branchHint(.unlikely);
              assert(i <= std.math.maxInt(Idx));
              const jump_idx = State.castAltPath(@truncate(i), state.alt_jump);
              self.addstate(opts, input, next_input_idx, nlist, jump_idx);
            }
          },
          // splits and assertions are never added
          .jump, .split, .word_boundary, .text_start, .text_end, .not_word_boundary, .line_end, .line_start => unreachable,
          .word_boundary_alt_jump, .text_start_alt_jump, .text_end_alt_jump, .not_word_boundary_alt_jump, .line_end_alt_jump, .line_start_alt_jump => unreachable,
          // accept cannot go anywhere, any accepts are discarded due to more chars being left
          // we continue to see if there exists a longer match
          .accept => {
            @branchHint(.unlikely);
          },
        }
      }
    }
  };
}
