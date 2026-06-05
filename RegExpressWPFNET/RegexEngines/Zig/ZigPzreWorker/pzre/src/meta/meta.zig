//! Comptime meta functions

const std = @import("std");
const assert = std.debug.assert;

pub fn compileError(comptime fmt: []const u8, comptime args: anytype) noreturn {
  comptime @compileError(std.fmt.comptimePrint(fmt, args));
}

pub fn propertyAssert(comptime what_it_is: []const u8, comptime with: fn(comptime T:type) bool, comptime T: type) void {
  comptime {
    if (!with(T)) compileError("Not {s}: {any}", .{what_it_is, T});
  }
}

pub fn propertyAssertNeg(comptime what_it_is_not: []const u8, comptime with: fn(comptime T:type) bool, comptime T: type) void {
  comptime {
    if (with(T)) compileError("{s} passed: {any}", .{what_it_is_not, T});
  }
}

pub fn isSlice(comptime T: type) bool {
  comptime return switch (@typeInfo(T)) {
    .pointer => |pointer| switch (pointer.size) {
      .slice => true,
      else => false,
      },
    else => false
  };
}

pub fn isInteger(comptime T: type) bool {
  comptime return switch (@typeInfo(T)) {
    .int => true,
    .comptime_int => true,
    else => false,
  };
}

pub fn isOnePointer(comptime T: type) bool {
  comptime return switch (@typeInfo(T)) {
    .pointer => |ptr| {
      if (ptr.size == .one) return true;
      return false;
    },
    else => false
  };
}

pub fn isStruct(comptime T: type) bool {
  comptime return switch (@typeInfo(T)) {
    .@"struct" => true,
    else => false,
  };
}

pub fn isTuple(comptime T: type) bool {
  comptime return switch (@typeInfo(T)) {
    .@"struct" => |info| {
      return info.is_tuple;
    },
    else => false,
  };
}

pub fn isErrorUnion(comptime T: type) bool {
  comptime return switch (@typeInfo(T)) {
    .error_union => true,
    else => false,
  };
}

/// Any type that can be iterated in a zig 'for' loop
pub fn isForIterable(comptime T: type) bool {
  return switch (@typeInfo(T)) {
    .array, .vector => true,
    .pointer => |ptr| {
      const ti = @typeInfo(ptr.child);
      if (ptr.size == .slice) return true;
      return switch (ti) {
        .array => true,
        .@"struct" => |info| info.is_tuple,
        else => false,
      };
    },
    .@"struct" => |s| s.is_tuple,
    else => false,
  };
}

/// Any type that can be iterated in a zig 'for' loop
pub fn isForIterableTuple(comptime T: type) bool {
  return switch (@typeInfo(T)) {
    .@"struct" => |s| s.is_tuple,
    .pointer => |ptr| {
      const ti = @typeInfo(ptr.child);
      return switch (ti) {
        .@"struct" => |info| info.is_tuple,
        else => false,
      };
    },
    else => false,
  };
}

pub fn isErrorSet(comptime T: type) bool {
  comptime return switch (@typeInfo(T)) {
    .error_set => true,
    else => false,
  };
}

pub fn isOptional(comptime T: type) bool {
  comptime return switch (@typeInfo(T)) {
    .optional => true,
    else => false,
  };
}

pub fn FunctionArgument(comptime F: type, comptime idx: usize) type {
  const info = switch (@typeInfo(F)) {
    .@"fn" => |i| i,
    else => @compileError("F is not a function type"),
  };
  const params = info.params;
  if (params.len <= idx) @compileError("F arity idx out of bounds");
  return params[idx].type orelse @compileError("Argument has no type");
}

pub fn FunctionArgumentsTuple(comptime F: type) type {
  const info = switch (@typeInfo(F)) {
    .@"fn" => |i| i,
    else => @compileError("F is not a function type"),
  };
  
  const params = info.params;
  var types: [params.len]type = undefined;
  
  for (params, 0..) |p, i| {
    types[i] = p.type orelse @compileError("Argument has no type");
  }
  
  return @Tuple(&types);
}

/// Get child of type
/// Works for iterators
pub fn GetChild(comptime T: type) ?type {
  comptime {
    return switch (@typeInfo(T)) {
      .pointer, => |info| info.child,
      .array, => |info| info.child,
      .optional => |info| info.child,
      .vector => |info| info.child,
      else => null
    };
  }
}

pub fn concatTupleCoerced(comptime T: type, lhs: anytype, rhs: anytype) T {
  comptime propertyAssert("Tuple", isTuple, T);
  var result: T = undefined;
  
  const lhs_fields = @typeInfo(@TypeOf(lhs)).@"struct".fields;
  inline for (lhs_fields, 0..) |f, i| {
    const name = std.fmt.comptimePrint("{d}", .{i});
    @field(result, name) = @field(lhs, f.name);
  }
  
  const rhs_fields = @typeInfo(@TypeOf(rhs)).@"struct".fields;
  inline for (rhs_fields, 0..) |f, i| {
    const name = std.fmt.comptimePrint("{d}", .{lhs_fields.len + i});
    @field(result, name) = @field(rhs, f.name);
  }
  
  return result;
}

/// Returns the payload of an error_union or null when not an error union
pub fn UnwrapError(comptime T: type) ?type {
  return switch (@typeInfo(T)) {
    .error_union => |u| u.payload,
    else => null,
  };
}

/// Returns the error_set of an error_union or null when not an error union
pub fn UnwrapErrorE(comptime T: type) ?type {
  return switch (@typeInfo(T)) {
    .error_union => |u| u.error_set,
    else => null,
  };
}

/// Tries to unwrap an optional 
pub fn UnwrapOptional(comptime T: type) ?type {
  return switch (@typeInfo(T)) {
    .optional => GetChild(T).?,
    else => null,
  };
}

pub fn isEnum(comptime T: type) bool {
  comptime return switch (@typeInfo(T)) {
    .@"enum" => true,
    else => false
  };
}

/// Asserts that the enum structure can act as an alias collective for array indices
/// Meaning that the enum tag values follow the pattern 0, 1, 2, ... , fields(T).len - 1
pub fn assertIndexEnum(comptime T: type) void {
  comptime {
    propertyAssert("Enum", isEnum, T);
    
    for (std.meta.fields(T), 0..) |field, i| {
      if (field.value != i) {
        @compileError("Enum tag values must perfectly map to sequential array indices starting from zero");
      }
    }
  }
}

pub fn assertFieldsAreSubsetOf(comptime T: type, comptime fields: []const u8) void {
  comptime {
    for (std.meta.fields(T)) |field| {
      var found = false;
      for (fields) |efield| {
        if (std.mem.eql(u8, field.name, efield.nam)) {
          found = true;
          break;
        }
      }
      assert(found);
    }
  }
}

/// Recursively generates all valid permutations of a union/struct.
/// 
/// `sweep_values` must be a tuple of arrays/slices containing the exact 
/// elements to sweep over for non-discrete types (e.g., `.{ &[_]context.Mode{...} }`).
pub fn unionDiscreetSweep(comptime U: type, comptime sweep_values: anytype) []const U {
  return getChoices(U, sweep_values);
}

fn extractSweepValues(comptime T: type, comptime sweep_values: anytype) ?[]const T {
  const TupleType = @TypeOf(sweep_values);
  if (comptime !isTuple(TupleType)) @compileError("sweep_values must be a tuple");

  inline for (std.meta.fields(TupleType), 0..) |field, i| {
    if (field.type == T) {
      const idx = comptime std.fmt.comptimePrint("{d}", .{i});
      return &.{@field(sweep_values, idx)};
    }
  }
  return null;
}

fn getChoices(comptime T: type, comptime sweep_values: anytype) []const T {
  // Check if the type bounds were explicitly injected via the sweep tuple
  if (extractSweepValues(T, sweep_values)) |vals| {
    return vals;
  }

  // Derive permutations topologically
  switch (@typeInfo(T)) {
    .@"enum" => |enum_info| {
      const fields = enum_info.fields;
      var res: [fields.len]T = undefined;
      inline for (fields, 0..) |f, i| {
        res[i] = @enumFromInt(f.value);
      }
      const final = res;
      return &final;
    },
    .bool => {
      return &[_]bool{ false, true };
    },
    .void => {
      return &[_]void{{}};
    },
    .optional => |opt_info| {
      const child_choices = getChoices(opt_info.child, sweep_values);
      var res: [child_choices.len + 1]T = undefined;
      res[0] = null;
      for (child_choices, 0..) |c, i| res[i + 1] = c;
      const final = res;
      return &final;
    },
    .array => |arr_info| {
      const child_choices = getChoices(arr_info.child, sweep_values);
      comptime var total = 1;
      for (0..arr_info.len) |_| total *= child_choices.len;

      var res: [total]T = undefined;
      for (&res, 0..) |*item, idx| {
        var temp_idx = idx;
        var inst: T = undefined;
        for (0..arr_info.len) |i| {
          const choice_idx = temp_idx % child_choices.len;
          temp_idx /= child_choices.len;
          inst[i] = child_choices[choice_idx];
        }
        item.* = inst;
      }
      const final = res;
      return &final;
    },
    .@"struct" => |struct_info| {
      if (struct_info.is_tuple) {
        @compileError("Tuples are not supported in discreet sweeps");
      }
      const fields = struct_info.fields;
      comptime var total = 1;
      inline for (fields) |f| {
        total *= getChoices(f.type, sweep_values).len;
      }

      var res: [total]T = undefined;
      for (&res, 0..) |*item, idx| {
        var temp_idx = idx;
        var inst: T = undefined;
        inline for (fields) |f| {
          const f_choices = getChoices(f.type, sweep_values);
          const choice_idx = temp_idx % f_choices.len;
          temp_idx /= f_choices.len;
          @field(inst, f.name) = f_choices[choice_idx];
        }
        item.* = inst;
      }
      const final = res;
      return &final;
    },
    .@"union" => |union_info| {
      comptime var total = 0;
      inline for (union_info.fields) |f| {
        total += getChoices(f.type, sweep_values).len;
      }

      var res: [total]T = undefined;
      var offset = 0;
      inline for (union_info.fields) |f| {
        const f_choices = getChoices(f.type, sweep_values);
        for (f_choices) |c| {
          res[offset] = @unionInit(T, f.name, c);
          offset += 1;
        }
      }
      const final = res;
      return &final;
    },
    else => {
      @compileError("Type '" ++ @typeName(T) ++ "' is non-discrete and no sweep values were provided in the tuple.");
    },
  }
}

pub fn hasDeclAll(comptime T: type, comptime field_name: []const u8) bool {
  comptime {
    if (isTuple(T)) {
      for (std.meta.fields(T)) |field| {
        if (!@hasDecl(field.type, field_name)) return false;
      }
      return true;
    } else if (isSlice(T)) {
      const Child = GetChild(T).?;
      return @hasDecl(Child, field_name);
    } else if (isOnePointer(T)) {
      return hasDeclAll(GetChild(T).?, field_name);
    } else {
      return if (T == @TypeOf(.{})) true else @hasDecl(T, field_name);
    }
  }
}
