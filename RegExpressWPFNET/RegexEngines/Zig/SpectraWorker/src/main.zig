const std = @import("std");
const Io = std.Io;
const json = std.json;

//const SpectraWorker = @import("SpectraWorker");
const spectra = @import("spectra");

const OPTIONS_TYPE = struct {
    dialect: ?[]u8 = null,
    unicode: bool = true,
    case_insensitive: bool = false,
    multi_line: bool = false,
    dot_matches_new_line: bool = false,
    swap_greed: bool = false,
    ignore_whitespace: bool = false,
    unicode_sets: bool = false,
    utf8: bool = true,
};

const INPUT_TYPE = struct {
    pattern: []u8,
    text: []u8,
    options: OPTIONS_TYPE = .{},
};

const OUTPUT_TYPE = struct {
    names: []const ?[]const u8,
    matches: ?[][]i32,
};

pub fn main1(init: std.process.Init) !void {
    const allocator = init.arena.allocator();
    const stdin = Io.File.stdin();
    const stdout = Io.File.stdout();
    const stderr = Io.File.stderr();

    var stdin_buffer: [512]u8 = undefined;
    var stdin_reader_wrapper = stdin.readerStreaming(init.io, &stdin_buffer);
    const reader: *std.Io.Reader = &stdin_reader_wrapper.interface;

    var input_al: std.ArrayList(u8) = .empty;

    try reader.appendRemaining(allocator, &input_al, std.Io.Limit.unlimited);

    const input_string = input_al.items;

    //std.debug.print("input_string: '{s}'\n", .{input_string});

    const input_parsed_object = try json.parseFromSlice(INPUT_TYPE, allocator, input_string, .{});
    //defer input_parsed_object.deinit();

    const input_object = input_parsed_object.value;

    const pattern = input_object.pattern;
    const text = input_object.text;
    const options = input_object.options;

    var compile_options: spectra.Regex.Options = .{};

    if (options.dialect) |dialect| {
        if (std.mem.eql(u8, dialect, "rust")) {
            compile_options.syntax.dialect = spectra.Syntax.Dialect.rust;
        } else if (std.mem.eql(u8, dialect, "ecmascript")) {
            compile_options.syntax.dialect = spectra.Syntax.Dialect.ecmascript;
        } else {
            std.debug.panic("Invalid dialect: '{s}'", .{dialect});
            @panic("Invalid dialect");
        }
    }

    compile_options.syntax.unicode = options.unicode;
    compile_options.syntax.case_insensitive = options.case_insensitive;
    compile_options.syntax.multi_line = options.multi_line;
    compile_options.syntax.dot_matches_new_line = options.dot_matches_new_line;
    compile_options.syntax.swap_greed = options.swap_greed;
    compile_options.syntax.ignore_whitespace = options.ignore_whitespace;
    compile_options.syntax.unicode_sets = options.unicode_sets;
    compile_options.syntax.utf8 = options.utf8;

    var diagnostic: spectra.Regex.Diagnostic = .{};
    compile_options.diagnostic = &diagnostic;

    var regex = spectra.Regex.compile(allocator, .init(pattern), compile_options) catch |err|
        {
            const error_text = try std.fmt.allocPrint(allocator, "{s}: {s} at {any}\n", //
                .{ @errorName(err), switch (diagnostic.reason) {
                    .none => "unknown error",
                    else => @tagName(diagnostic.reason),
                }, diagnostic.span.start });
            try stderr.writeStreamingAll(init.io, error_text);
            std.process.exit(1);
        };

    var cache = try regex.cache(allocator);
    var iterator = regex.findAll(&cache, text);
    const groups: []?spectra.Regex.Span = try allocator.alloc(?spectra.Regex.Span, regex.captureCount());

    var matches_arr: std.ArrayList([]i32) = .empty;

    while (try iterator.nextCaptures(groups)) {
        var groups_arr: std.ArrayList(i32) = .empty;

        for (groups) |group| {
            if (group) |g| {
                try groups_arr.append(allocator, @intCast(g.start));
                try groups_arr.append(allocator, @intCast(g.end));
            } else {
                try groups_arr.append(allocator, @intCast(-1));
                try groups_arr.append(allocator, @intCast(-1));
            }
        }

        try matches_arr.append(allocator, groups_arr.items);
    }

    const output_object: OUTPUT_TYPE =
        .{
            .names = regex.nfa.names,
            .matches = matches_arr.items,
        };

    const json_options: std.json.Stringify.Options = .{ .whitespace = .minified, .escape_unicode = true };
    const output_json = try std.fmt.allocPrint(allocator, "{f}\n", .{std.json.fmt(output_object, json_options)});

    try stdout.writeStreamingAll(init.io, output_json);
}

var init_arg: ?std.process.Init = null;

pub fn main(init: std.process.Init) !void {
    init_arg = init;
    @setRuntimeSafety(true);

    const allocator = init.arena.allocator();
    const stderr = Io.File.stderr();

    main1(init) catch |err| {
        const error_text = try std.fmt.allocPrint(allocator, "{s}\n", .{@errorName(err)});
        try stderr.writeStreamingAll(init.io, error_text);
        std.process.exit(1);
    };

    std.process.exit(0);
}

pub const panic = std.debug.FullPanic(myPanic);

fn myPanic(msg: []const u8, first_trace_addr: ?usize) noreturn {
    _ = first_trace_addr;

    const init: std.process.Init = init_arg.?; //' orelse std.process.exit(1); //...............

    const allocator = init.arena.allocator();
    //const stdin = Io.File.stdin();
    //const stdout = Io.File.stdout();
    const stderr = Io.File.stderr();

    const error_text = std.fmt.allocPrint(allocator, "{s}\n", .{msg}) catch "Catastrophic failure";
    stderr.writeStreamingAll(init.io, error_text) catch {};

    std.process.exit(1);
}
