#include <cstdio>
#include "source_timeline.h"
#include "media_order.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main() {
    // --- SourceTimeline ---
    SourceTimeline t;
    t.add_file("a.mp4", 1000);
    t.add_file("b.mp4", 500);
    t.add_file("c.mp4", 2000);
    CHECK(t.file_count() == 3 && t.total_ticks() == 3500);
    CHECK(t.file(0).start_ticks == 0 && t.file(1).start_ticks == 1000 && t.file(2).start_ticks == 1500);
    CHECK(t.file_index_for(-50) == 0);
    CHECK(t.file_index_for(0) == 0);
    CHECK(t.file_index_for(999) == 0);
    CHECK(t.file_index_for(1000) == 1);   // boundary belongs to the later file
    CHECK(t.file_index_for(1499) == 1);
    CHECK(t.file_index_for(1500) == 2);
    CHECK(t.file_index_for(3499) == 2);
    CHECK(t.file_index_for(99999) == 2);  // past the end -> last file
    CHECK(t.to_local(2, 1700) == 200 && t.to_global(2, 200) == 1700);
    SourceTimeline one; one.add_file("x", 10);
    CHECK(one.file_index_for(5) == 0 && one.file_index_for(500) == 0);
    t.clear(); CHECK(t.file_count() == 0 && t.total_ticks() == 0);

    // --- GoPro names ---
    int ch, rec;
    CHECK(media_order::parse_gopro_name("D:\\v\\GX020123.MP4", &ch, &rec) && ch == 2 && rec == 123);
    CHECK(media_order::parse_gopro_name("/x/gh010007.mp4", &ch, &rec) && ch == 1 && rec == 7);
    CHECK(!media_order::parse_gopro_name("GX0201.MP4", &ch, &rec));
    CHECK(!media_order::parse_gopro_name("GP010123.MP4", &ch, &rec));   // older GoPro prefix isn't handled
    CHECK(!media_order::parse_gopro_name("DJI_20261103101500_0001_D.MP4", &ch, &rec));

    // GoPro: recording first, then chapter
    std::vector<std::string> g;
    g.push_back("GX010124.MP4"); g.push_back("GX020123.MP4"); g.push_back("GX010123.MP4"); g.push_back("GX030123.MP4");
    media_order::order_files(&g);
    CHECK(g[0] == "GX010123.MP4" && g[1] == "GX020123.MP4" && g[2] == "GX030123.MP4" && g[3] == "GX010124.MP4");

    // DJI-style: by timestamp then sequence; natural digits
    std::vector<std::string> d;
    d.push_back("C:\\f\\DJI_20261103101500_0002_D.MP4");
    d.push_back("C:\\f\\DJI_20261103093000_0001_D.MP4");
    d.push_back("C:\\f\\DJI_20261103101500_0001_D.MP4");
    media_order::order_files(&d);
    CHECK(d[0].find("093000_0001") != std::string::npos && d[1].find("101500_0001") != std::string::npos && d[2].find("101500_0002") != std::string::npos);

    std::vector<std::string> n;
    n.push_back("clip10.mp4"); n.push_back("clip2.mp4"); n.push_back("Clip1.mp4"); n.push_back("clip02.mp4");
    media_order::order_files(&n);
    CHECK(n[0] == "Clip1.mp4");
    CHECK(n[3] == "clip10.mp4");
    CHECK(media_order::natural_compare("a9", "a10") < 0);
    CHECK(media_order::natural_compare("A", "a") == 0);
    CHECK(media_order::natural_compare("a", "ab") < 0);
    CHECK(media_order::natural_compare("x007", "x7") == 0);

    // mixed kinds: everything not GoPro first, and a consistent order either way in
    std::vector<std::string> m1, m2;
    m1.push_back("GX010001.MP4"); m1.push_back("movie.mp4"); m1.push_back("GX020001.MP4");
    m2.push_back("GX020001.MP4"); m2.push_back("GX010001.MP4"); m2.push_back("movie.mp4");
    media_order::order_files(&m1); media_order::order_files(&m2);
    CHECK(m1 == m2 && m1[0] == "movie.mp4" && m1[1] == "GX010001.MP4");

    // already in order, empty, single
    std::vector<std::string> e; media_order::order_files(&e); CHECK(e.empty());
    std::vector<std::string> s1; s1.push_back("only.mp4"); media_order::order_files(&s1); CHECK(s1.size() == 1);

    printf(fails ? "FAILED\n" : "all ok\n");
    return fails;
}
