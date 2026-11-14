// Tests for engine/runtime/axmol_compat: Rolo's replacement for the yasio
// headers Axmol's core includes (ADR 0012). Axmol relies on the exact behaviour
// checked here: buffers that hand their memory to free(), the byte layout of its
// settings file, and the order of equal-priority scheduler entries.

#include "axmol.h"
#include "base/ZipUtils.h"
#include "yasio/byte_buffer.hpp"
#include "yasio/ibstream.hpp"
#include "yasio/obstream.hpp"
#include "yasio/ref_ptr.hpp"
#include "yasio/string_view.hpp"
#include "yasio/sz.hpp"
#include "yasio/utils.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace {
std::vector<unsigned char> bytesOf(const yasio::obstream& out)
{
    const auto* first = reinterpret_cast<const unsigned char*>(out.data());
    return {first, first + out.length()};
}

struct Point {
    float x;
    float y;
};
} // namespace

TEST_CASE("pod_vector grows, inserts and erases like a vector", "[runtime][compat]")
{
    yasio::pod_vector<int> values;
    for (int i = 0; i < 100; ++i)
        values.push_back(i);
    REQUIRE(values.size() == 100);
    CHECK(values.front() == 0);
    CHECK(values.back() == 99);

    values.erase(values.begin() + 10, values.begin() + 90);
    REQUIRE(values.size() == 20);
    CHECK(values[9] == 9);
    CHECK(values[10] == 90);

    values.insert(values.begin(), 3, -1);
    CHECK(values.size() == 23);
    CHECK(values[0] == -1);
    CHECK(values[3] == 0);

    // Inserting a range taken from the same vector must copy it first.
    values.insert(values.begin() + 1, values.begin() + 3, values.begin() + 5);
    CHECK(values[1] == 0);
    CHECK(values[2] == 1);
    CHECK(values[3] == -1);

    CHECK(values.index_of(90) >= 0);
    CHECK(values.index_of(12345) == -1);

    yasio::pod_vector<int> copy = values;
    yasio::pod_vector<int> moved = std::move(values);
    CHECK(copy.size() == moved.size());
    CHECK(values.empty()); // NOLINT(bugprone-use-after-move): moved-from state is defined
}

TEST_CASE("pod_vector{count} means count elements, not a one-element list", "[runtime][compat]")
{
    // Axmol's DrawNode writes `pod_vector<Vec2> v{n}` and indexes up to n.
    yasio::pod_vector<Point> points{static_cast<size_t>(7)};
    CHECK(points.size() == 7);

    auto& added = points.emplace_back(1.0f, 2.0f); // an aggregate, built with braces
    CHECK(added.x == 1.0f);
    CHECK(points.size() == 8);
}

TEST_CASE("pod_vector resize keeps old elements; reset and fill set new ones", "[runtime][compat]")
{
    yasio::byte_buffer buffer(4, 0xAB);
    buffer.resize(8);
    CHECK(buffer[3] == 0xAB);
    buffer.resize(10, 0x01);
    CHECK(buffer[9] == 0x01);
    buffer.reset(6);
    CHECK(buffer.size() == 6);
    for (auto byte : buffer)
        CHECK(byte == 0);

    buffer.resize_and_overwrite(16, [](unsigned char* data, size_t capacity) {
        CHECK(capacity == 16);
        data[0] = 'L';
        return size_t{1};
    });
    CHECK(buffer.size() == 1);
    CHECK(buffer[0] == 'L');
}

TEST_CASE("A released byte buffer belongs to the caller, who frees it", "[runtime][compat]")
{
    // Axmol's Data and Image take these blocks and later call free() on them;
    // AddressSanitizer reports a mismatch if the block came from new[].
    yasio::byte_buffer buffer(32, 7);
    size_t length = 0;
    unsigned char* block = buffer.detach_abi(length);
    CHECK(length == 32);
    CHECK(buffer.empty());
    CHECK(block[31] == 7);

    yasio::byte_buffer adopted;
    adopted.attach_abi(block, length); // ...or a buffer adopts it again
    CHECK(adopted.size() == 32);
    std::free(adopted.release_pointer());
}

TEST_CASE("insert_sorted keeps equal elements in insertion order", "[runtime][compat]")
{
    // Axmol's scheduler: updates of the same priority run in the order added.
    struct Entry {
        int priority;
        int id;
    };
    yasio::pod_vector<Entry> list;
    const auto byPriority = [](const Entry& lhs, const Entry& rhs) { return lhs.priority < rhs.priority; };
    yasio::insert_sorted(list, Entry{5, 1}, byPriority);
    yasio::insert_sorted(list, Entry{0, 2}, byPriority);
    yasio::insert_sorted(list, Entry{5, 3}, byPriority);
    yasio::insert_sorted(list, Entry{0, 4}, byPriority);
    REQUIRE(list.size() == 4);
    CHECK(list[0].id == 2);
    CHECK(list[1].id == 4);
    CHECK(list[2].id == 1);
    CHECK(list[3].id == 3);

    std::vector<int> numbers{1, 2, 1, 3, 1};
    CHECK(yasio::erase(numbers, 1) == 3);
    CHECK(numbers == std::vector<int>{2, 3});
    CHECK(yasio::erase_if(numbers, [](int n) { return n > 2; }) == 1);
    CHECK(numbers == std::vector<int>{2});
}

TEST_CASE("obstream writes the byte layout of Axmol's settings file", "[runtime][compat]")
{
    // Big-endian numbers and 7-bit encoded lengths: the same bytes the original
    // library wrote, so settings saved before keep loading.
    yasio::obstream out;
    out.write<int>(1);
    out.write_v("ab");
    out.write_ix(300);
    out.write_ix(-1);
    CHECK(bytesOf(out) == std::vector<unsigned char>{0x00, 0x00, 0x00, 0x01,         // write<int>(1)
                                                     0x02, 'a', 'b',                 // write_v("ab")
                                                     0xAC, 0x02,                     // 300
                                                     0xFF, 0xFF, 0xFF, 0xFF, 0x0F}); // -1

    unsigned char header[4] = {};
    yasio::obstream::swrite(header, std::int32_t{0x01020304});
    CHECK(header[0] == 0x01);
    CHECK(header[3] == 0x04);
    CHECK(yasio::ibstream::sread<std::int32_t>(header) == 0x01020304);
}

TEST_CASE("ibstream_view reads back what obstream wrote", "[runtime][compat]")
{
    yasio::obstream out;
    out.write<int>(-42);
    out.write<std::uint16_t>(0xBEEF);
    out.write_v("hello");
    out.write_v(std::string(200, 'x')); // a length that needs two bytes
    out.write_ix(-7);

    yasio::ibstream_view in(out.data(), out.length());
    CHECK(in.read<int>() == -42);
    CHECK(in.read<std::uint16_t>() == 0xBEEF);
    CHECK(in.read_v() == "hello");
    CHECK(in.read_v().size() == 200);
    CHECK(in.read_ix<int>() == -7);
    CHECK(in.eof());
    CHECK(in.seek(0, SEEK_CUR) == static_cast<std::ptrdiff_t>(out.length()));

    // Reading past the end is an error, not a read of whatever memory follows.
    CHECK_THROWS(in.read<int>());
    in.seek(0, SEEK_SET);
    CHECK(in.read<int>() == -42);
}

TEST_CASE("fast_ibstream_view reads numbers in this computer's byte order", "[runtime][compat]")
{
    const std::uint32_t values[2] = {0x11223344u, 7u};
    yasio::fast_ibstream_view in(values, sizeof(values));
    CHECK(in.read<std::uint32_t>() == 0x11223344u);
    CHECK(in.tell() == 4);
    in.advance(-4);
    in.advance(4);
    CHECK(in.read<std::uint32_t>() == 7u);
    CHECK(in.eof());
}

TEST_CASE("String helpers compare prefixes and suffixes", "[runtime][compat]")
{
    const std::string path = "assets/Hero.PNG";
    CHECK(cxx20::starts_with(path, "assets/"));
    CHECK_FALSE(cxx20::starts_with(path, "Assets/"));
    CHECK(cxx20::ic::starts_with(path, "ASSETS/"));
    CHECK(cxx20::ends_with(path, ".PNG"));
    CHECK(cxx20::ic::ends_with(path, ".png"));
    CHECK(cxx20::ic::iequals("File:///", "file:///"));
    CHECK_FALSE(cxx20::ic::iequals("file", "files"));
    {
        using namespace cxx17;
        CHECK("abc"_sv == std::string_view("abc"));
        CHECK(L"NO"_sv.length() == 2);
    }
    CHECK(YASIO_SZ_ALIGN(17, 16) == 32);
    CHECK(YASIO_SZ_ALIGN(32, 16) == 32);
}

TEST_CASE("ref_ptr adopts on construction, retains on copy, releases at the end", "[runtime][compat]")
{
    struct Counted {
        int references = 1;
        bool* destroyed = nullptr;
        void retain() { ++references; }
        void release()
        {
            if (--references == 0) {
                *destroyed = true;
                delete this;
            }
        }
    };
    bool destroyed = false;
    {
        yasio::ref_ptr<Counted> first(new Counted{1, &destroyed});
        CHECK(first->references == 1);
        {
            yasio::ref_ptr<Counted> second = first;
            CHECK(first->references == 2);
        }
        CHECK(first->references == 1);
        CHECK_FALSE(destroyed);
    }
    CHECK(destroyed);
}

TEST_CASE("Clock helpers count in their stated units", "[runtime][compat]")
{
    const auto milliseconds = yasio::clock<yasio::system_clock_t>();
    const auto seconds = yasio::time_now();
    CHECK(milliseconds / 1000 >= seconds - 2);
    CHECK(milliseconds / 1000 <= seconds + 2);
    // Read one after the other: inside one expression, C++ does not say
    // which side runs first (MSVC reads the right one first).
    const auto micro = yasio::highp_clock();
    const auto nano = yasio::xhighp_clock();
    CHECK(micro <= nano / 1000 + 1);
    CHECK(nano / 1000 - micro < 1'000'000);   // the same clock, in the same second
}

TEST_CASE("Axmol's own code works with the replacement buffers", "[runtime][compat]")
{
    // base64 and gzip in Axmol's core return and fill these buffers.
    const yasio::byte_buffer decoded = ax::utils::base64Decode("Um9sbw==");
    CHECK(std::string(decoded.begin(), decoded.end()) == "Rolo");

    const std::string text(1000, 'L');
    const auto packed = ax::ZipUtils::compressGZ(text.data(), text.size());
    CHECK(packed.size() < text.size());
    const auto unpacked = ax::ZipUtils::decompressGZ(packed.data(), packed.size());
    CHECK(std::string(unpacked.begin(), unpacked.end()) == text);
}

TEST_CASE("Without OpenSSL, Axmol's digest helpers give an empty text", "[runtime][compat]")
{
    // Rolo builds Axmol without OpenSSL (ADR 0024); its stand-in headers
    // know no digest. Nothing in Rolo asks for one: this pins down what
    // would happen, an empty answer and no crash.
    CHECK(ax::utils::getStringMD5Hash("Rolo").empty());
    CHECK(ax::utils::computeDigest("Rolo", "sha1").empty());
    CHECK(ax::utils::computeDigest("Rolo", "no-such-digest").empty());
}
