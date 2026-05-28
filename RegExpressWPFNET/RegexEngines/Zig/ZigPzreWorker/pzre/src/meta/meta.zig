//! Comptime meta functions

const std = @import("std");

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
    .pointer => |ptr| ptr.size == .slice or @typeInfo(ptr.child) == .array,
    .@"struct" => |s| s.is_tuple,
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
