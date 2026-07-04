const t = @import("test.zig");

const testSearchAndReplace = t.testSearchAndReplace;
const testSearchAndReplaceMultiline = t.testSearchAndReplaceMultiline;
const pzre = @import("../root.zig");
const Match = pzre.regex.Match;

const Config = pzre.compile.Config;

test "pzre search and replace" {
  try testSearchAndReplace(
    "\\s+",
    "  sensor_data   ---  active  ",
    " ",
    0,
    29,
    .{ .span = .init(0, 2), .new = " sensor_data   ---  active  " },
    .{ .span = .init(0, 29), .count = 4, .new = " sensor_data --- active " }
  );

  try testSearchAndReplace(
    "-+",
    " sensor_data --- active ",
    "=",
    0,
    24,
    .{ .span = .init(13, 16), .new = " sensor_data = active " },
    .{ .span = .init(13, 16), .count = 1, .new = " sensor_data = active " }
  );

  try testSearchAndReplace(
    "^\\s+",
    " sensor_data = active ",
    "",
    0,
    22,
    .{ .span = .init(0, 1), .new = "sensor_data = active " },
    .{ .span = .init(0, 1), .count = 1, .new = "sensor_data = active " }
  );

  try testSearchAndReplace(
    "\\s+$",
    "sensor_data = active ",
    "",
    0,
    21,
    .{ .span = .init(20, 21), .new = "sensor_data = active" },
    .{ .span = .init(20, 21), .count = 1, .new = "sensor_data = active" }
  );

  try testSearchAndReplace(
    "[aeiou]",
    "sensor_data = active",
    "X",
    0,
    20,
    .{ .span = .init(1, 2), .new = "sXnsor_data = active" },
    .{ .span = .init(1, 20), .count = 7, .new = "sXnsXr_dXtX = XctXvX" }
  );

  try testSearchAndReplace(
    "^.*=\\s*",
    "sXnsXr_dXtX = XctXvX",
    "",
    0,
    20,
    .{ .span = .init(0, 14), .new = "XctXvX" },
    .{ .span = .init(0, 14), .count = 1, .new = "XctXvX" }
  );
}

test "pzre search and replace insertions" {
  try testSearchAndReplace(
    "^",
    "payload",
    "header_",
    0,
    7,
    .{ .span = .init(0, 0), .new = "header_payload" },
    .{ .span = .init(0, 0), .count = 1, .new = "header_payload" }
  );

  try testSearchAndReplace(
    "$",
    "header_payload",
    "_tail",
    0,
    14,
    .{ .span = .init(14, 14), .new = "header_payload_tail" },
    .{ .span = .init(14, 14), .count = 1, .new = "header_payload_tail" }
  );

  try testSearchAndReplace(
    "\\b",
    "user data",
    "|",
    0,
    9,
    .{ .span = .init(0, 0), .new = "|user data" },
    .{ .span = .init(0, 9), .count = 4, .new = "|user| |data|" }
  );

  try testSearchAndReplace(
    "\\B",
    "hello",
    "-",
    0,
    5,
    .{ .span = .init(1, 1), .new = "h-ello" },
    .{ .span = .init(1, 4), .count = 4, .new = "h-e-l-l-o" }
  );
}

test "pzre search and replace complicated real world scenario" {
  // AI generated real world scenarios

  // Redact IPv4 addresses from a connection trace
  try testSearchAndReplace(
    "[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}",
    "Login from 192.168.1.50 and 10.0.0.1 failed",
    "[IP]",
    0,
    43,
    .{ .span = .init(11, 23), .new = "Login from [IP] and 10.0.0.1 failed" },
    .{ .span = .init(11, 36), .count = 2, .new = "Login from [IP] and [IP] failed" }
  );

  // Strip XML/HTML tags to extract the raw plaintext payload
  try testSearchAndReplace(
    "<[^>]+>",
    "<span>User <b>John</b> joined</span>",
    "",
    0,
    36,
    .{ .span = .init(0, 6), .new = "User <b>John</b> joined</span>" },
    .{ .span = .init(0, 36), .count = 4, .new = "User John joined" }
  );

  // Nullify UUIDs in a transaction record
  try testSearchAndReplace(
    "[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}",
    "req 123e4567-e89b-12d3-a456-426614174000 proc",
    "[UUID]",
    0,
    45,
    .{ .span = .init(4, 40), .new = "req [UUID] proc" },
    .{ .span = .init(4, 40), .count = 1, .new = "req [UUID] proc" }
  );

  // Standardize arbitrary whitespace and delimiter clusters into a clean CSV format
  try testSearchAndReplace(
    "\\s*\\|\\s*",
    "data  |  value1 |value2|  value3",
    ",",
    0,
    32,
    .{ .span = .init(4, 9), .new = "data,value1 |value2|  value3" },
    .{ .span = .init(4, 26), .count = 3, .new = "data,value1,value2,value3" }
  );

  // Mask a strict timestamp prefix in a system log
  try testSearchAndReplace(
    "^\\[[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\\]",
    "[2026-03-20 12:33:58] INFO root: started",
    "[TIME]",
    0,
    40,
    .{ .span = .init(0, 21), .new = "[TIME] INFO root: started" },
    .{ .span = .init(0, 21), .count = 1, .new = "[TIME] INFO root: started" }
  );
}

test "pzre search and replace multiline" {
  // AI generated real world scenarios

  // Simulating a standard configuration file read directly into a buffer
  const file_buffer =
    \\# init
    \\
    \\a=1  
    \\
    \\# exit
  ;

  // 1. Deletion of entire lines
  try testSearchAndReplaceMultiline(
    "^#.*\\n?",
    file_buffer,
    "",
    0,
    21,
    // Fix: Removed one \n from the start of both expected strings
    .{ .span = .init(0, 7), .new = "\na=1  \n\n# exit" },
    .{ .span = .init(0, 21), .count = 2, .new = "\na=1  \n\n" }
  );

  // 2. Cleaning up whitespace
  try testSearchAndReplaceMultiline(
    " +$",
    file_buffer,
    "",
    0,
    21,
    .{ .span = .init(11, 13), .new = "# init\n\na=1\n\n# exit" },
    .{ .span = .init(11, 13), .count = 1, .new = "# init\n\na=1\n\n# exit" }
  );

  // 3. Prepending text on specific lines
  try testSearchAndReplaceMultiline(
    "^",
    file_buffer,
    "> ",
    0,
    21,
    .{ .span = .init(0, 0), .new = "> # init\n\na=1  \n\n# exit" },
    .{ .span = .init(0, 15), .count = 5, .new = "> # init\n> \n> a=1  \n> \n> # exit" },
  );

  // 4. Appending text on specific lines
  try testSearchAndReplaceMultiline(
    "$",
    file_buffer,
    ";",
    0,
    21,
    .{ .span = .init(6, 6), .new = "# init;\n\na=1  \n\n# exit" },
    .{ .span = .init(6, 21), .count = 5, .new = "# init;\n;\na=1  ;\n;\n# exit;" }
  );

  // 5. Deleting an entire file
  try testSearchAndReplaceMultiline(
    "(.|\\n)+",
    file_buffer,
    "",
    0,
    21,
    .{ .span = .init(0, 21), .new = "" },
    .{ .span = .init(0, 21), .count = 1, .new = "" }
  );
}
