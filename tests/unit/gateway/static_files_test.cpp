#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "static_files.hpp"

using namespace skyrelay::gateway;

TEST(StaticFiles, RangeTable) {   // Review Focus 5 — size 1000
  EXPECT_EQ(parse_range("", 1000).kind, RangeKind::Full);
  auto a = parse_range("bytes=0-499", 1000);  EXPECT_EQ(a.kind, RangeKind::Partial); EXPECT_EQ(a.range.last, 499u);
  auto b = parse_range("bytes=500-", 1000);   EXPECT_EQ(b.range.first, 500u); EXPECT_EQ(b.range.last, 999u);
  auto c = parse_range("bytes=-100", 1000);   EXPECT_EQ(c.range.first, 900u);
  auto d = parse_range("bytes=990-2000", 1000); EXPECT_EQ(d.range.last, 999u);
  EXPECT_EQ(parse_range("bytes=1000-", 1000).kind, RangeKind::Unsatisfiable);
  EXPECT_EQ(parse_range("bytes=5-1", 1000).kind, RangeKind::Invalid);
  EXPECT_EQ(parse_range("items=0-1", 1000).kind, RangeKind::Invalid);

  EXPECT_EQ(a.range.first, 0u);
  EXPECT_EQ(b.kind, RangeKind::Partial);
  EXPECT_EQ(c.kind, RangeKind::Partial);
  EXPECT_EQ(c.range.last, 999u);
  EXPECT_EQ(d.kind, RangeKind::Partial);
  EXPECT_EQ(d.range.first, 990u);
}

TEST(StaticFiles, RangeEdgeCases) {
  // A suffix longer than the file is the whole file.
  auto s = parse_range("bytes=-5000", 1000);
  EXPECT_EQ(s.kind, RangeKind::Partial);
  EXPECT_EQ(s.range.first, 0u);
  EXPECT_EQ(s.range.last, 999u);
  // The last byte only, and a single byte.
  auto l = parse_range("bytes=999-999", 1000);
  EXPECT_EQ(l.kind, RangeKind::Partial);
  EXPECT_EQ(l.range.first, 999u);
  EXPECT_EQ(l.range.last, 999u);
  // An empty suffix overlaps nothing.
  EXPECT_EQ(parse_range("bytes=-0", 1000).kind, RangeKind::Unsatisfiable);
  // Several ranges: answered with the whole file.
  EXPECT_EQ(parse_range("bytes=0-1,5-9", 1000).kind, RangeKind::Full);
  // The unit is case-insensitive and whitespace around the spec is tolerated.
  auto u = parse_range("Bytes= 10-19 ", 1000);
  EXPECT_EQ(u.kind, RangeKind::Partial);
  EXPECT_EQ(u.range.first, 10u);
  EXPECT_EQ(u.range.last, 19u);
  // Malformed specs.
  EXPECT_EQ(parse_range("bytes=", 1000).kind, RangeKind::Invalid);
  EXPECT_EQ(parse_range("bytes=-", 1000).kind, RangeKind::Invalid);
  EXPECT_EQ(parse_range("bytes=a-b", 1000).kind, RangeKind::Invalid);
  EXPECT_EQ(parse_range("bytes=1-2-3", 1000).kind, RangeKind::Invalid);
  EXPECT_EQ(parse_range("bytes=+1-2", 1000).kind, RangeKind::Invalid);
  EXPECT_EQ(parse_range("bytes 0-1", 1000).kind, RangeKind::Invalid);
  // Numbers too large for 64 bits saturate instead of wrapping.
  auto big = parse_range("bytes=0-99999999999999999999999", 1000);
  EXPECT_EQ(big.kind, RangeKind::Partial);
  EXPECT_EQ(big.range.last, 999u);
  EXPECT_EQ(parse_range("bytes=99999999999999999999999-", 1000).kind,
            RangeKind::Unsatisfiable);
  // Nothing in an empty file can be satisfied.
  EXPECT_EQ(parse_range("bytes=0-", 0).kind, RangeKind::Unsatisfiable);
  EXPECT_EQ(parse_range("bytes=-1", 0).kind, RangeKind::Unsatisfiable);
  EXPECT_EQ(parse_range("", 0).kind, RangeKind::Full);
}

TEST(StaticFiles, DecodesTargets) {
  using Segments = std::vector<std::string>;
  EXPECT_EQ(decode_target("/"), Segments{});
  EXPECT_EQ(decode_target("/tiles/region.pmtiles"),
            (Segments{"tiles", "region.pmtiles"}));
  // Query dropped, empty and "." segments skipped.
  EXPECT_EQ(decode_target("/a//./b/?x=1#f"), (Segments{"a", "b"}));
  // Glyph requests carry escaped spaces in the font stack name.
  EXPECT_EQ(decode_target("/assets/fonts/Noto%20Sans%20Regular/0-255.pbf"),
            (Segments{"assets", "fonts", "Noto Sans Regular", "0-255.pbf"}));
  // A dot inside a name is fine; only a whole ".." segment is not.
  EXPECT_EQ(decode_target("/a..b/c.."), (Segments{"a..b", "c.."}));
}

TEST(StaticFiles, RejectsBadTargets) {
  EXPECT_FALSE(decode_target(""));
  EXPECT_FALSE(decode_target("tiles/x"));
  EXPECT_FALSE(decode_target("http://example.com/x"));
  EXPECT_FALSE(decode_target("/tiles/../secret"));
  EXPECT_FALSE(decode_target("/.."));
  EXPECT_FALSE(decode_target("/tiles/%2e%2e/secret"));
  EXPECT_FALSE(decode_target("/tiles/%2E%2E"));
  // An escaped slash decodes to a separator, so this is a ".." segment too.
  EXPECT_FALSE(decode_target("/tiles/..%2fsecret"));
  EXPECT_FALSE(decode_target("/tiles/a%00b"));
  EXPECT_FALSE(decode_target("/tiles/a%zz"));
  EXPECT_FALSE(decode_target("/tiles/a%2"));
  EXPECT_FALSE(decode_target("/tiles/a%"));
}

TEST(StaticFiles, MimeTypes) {
  EXPECT_EQ(mime_type("region.pmtiles"), "application/octet-stream");
  EXPECT_EQ(mime_type("0-255.pbf"), "application/x-protobuf");
  EXPECT_EQ(mime_type("dark.json"), "application/json");
  EXPECT_EQ(mime_type("dark.png"), "image/png");
  EXPECT_EQ(mime_type("dark@2x.PNG"), "image/png");
  EXPECT_EQ(mime_type("index-abc123.js"), "text/javascript; charset=utf-8");
  EXPECT_EQ(mime_type("index.css"), "text/css; charset=utf-8");
  EXPECT_EQ(mime_type("index.html"), "text/html; charset=utf-8");
  EXPECT_EQ(mime_type("logo.svg"), "image/svg+xml");
  EXPECT_EQ(mime_type("README"), "application/octet-stream");
  EXPECT_EQ(mime_type("archive.unknown"), "application/octet-stream");
}
