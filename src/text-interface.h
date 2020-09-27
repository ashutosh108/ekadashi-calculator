#ifndef VP_SRC_TEXT_INTERFACE_H
#define VP_SRC_TEXT_INTERFACE_H

#include <chrono>
#include "filesystem-fixed.h"
#include <optional>
#include <tl/expected.hpp>
#include <unordered_map>

#include "location.h"
#include "swe.h"
#include "tz-fixed.h"
#include "vrata.h"

namespace vp::text_ui {

/* Change dir to the directory with eph and tzdata data files (usually it's .exe dir) */
void change_to_data_dir(const char* argv0);

date::year_month_day parse_ymd(const std::string_view s);
tl::expected<vp::Vrata, vp::CalcError> calc_one(date::year_month_day base_date, Location location);
tl::expected<vp::Vrata, vp::CalcError> calc_and_report_one(date::year_month_day base_date, Location coord, fmt::memory_buffer & buf);

// Find next ekAdashI vrata for the named location, report details to the output buffer.
tl::expected<vp::Vrata, vp::CalcError> find_calc_and_report_one(date::year_month_day base_date, const char * location_name, fmt::memory_buffer & buf);
void report_details(const vp::MaybeVrata & vrata, fmt::memory_buffer & buf);

void print_detail_one(date::year_month_day base_date, Location coord, fmt::memory_buffer & buf);
void print_detail_one(date::year_month_day base_date, const char * location_name, fmt::memory_buffer & buf);
void calc_and_report_all(date::year_month_day d);
vp::VratasForDate calc_all(date::year_month_day);
vp::VratasForDate calc_one(date::year_month_day base_date, std::string location_name);
std::string version();
std::string program_name_and_version();

class LocationDb {
public:

    auto begin() { return locations().cbegin(); }
    auto end() { return locations().cend(); }
    static std::optional<Location> find_coord(const char *location_name);

private:
    static const std::vector<Location> & locations();
};

namespace detail {

fs::path determine_exe_dir(const char* argv0);
fs::path determine_working_dir(const char* argv0);

struct MyHash {
    std::size_t operator()(const date::year_month_day & ymd) const {
        auto hy = std::hash<int>{}(ymd.year().operator int());
        auto hm = std::hash<unsigned int>{}(ymd.month().operator unsigned int());
        auto hd = std::hash<unsigned int>{}(ymd.day().operator unsigned int());
        return hy ^ (hm << 1) ^ (hd << 2);
    }
};

extern std::unordered_map<date::year_month_day, vp::VratasForDate, MyHash> cache;

} // namespace detail

} // namespace vp::text_ui

#endif // VP_SRC_TEXT_INTERFACE_H
