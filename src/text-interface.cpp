#include "text-interface.h"

#include "calc.h"
#include "vrata_detail_printer.h"

#include <charconv>
#include <cstring>

using namespace vp;

namespace vp::text_ui {

namespace {
constexpr int len(const char * s) { return std::char_traits<char>::length(s); }
static_assert (len("123") == 3);
}

date::year_month_day parse_ymd(const std::string_view s) {
    // YYYY-MM-DD
    // 01234567890
    if (s.length() != len("YYYY-MM-DD")) return {};
    const char * const str = s.data();
    int year{};
    if (auto [p, e] = std::from_chars(str+0, str+len("YYYY"), year); e != std::errc{} || p != str+4)
        return {};
    unsigned int month{};
    if (auto [p, e] = std::from_chars(str+len("YYYY-"), str+len("YYYY-MM"), month); e != std::errc{} || p != str+7)
        return {};
    unsigned int day{};
    if (auto [p, e] = std::from_chars(str+len("YYYY-MM-"), str+len("YYYY-MM-DD"), day); e != std::errc{} || p != str+10)
        return {};
    return date::year{year}/date::month{month}/date::day{day};
}

const std::vector<Location> &LocationDb::locations() {
    static std::vector<Location> locations_ {
        { udupi_coord },
        { gokarna_coord },
        { newdelhi_coord },
        { manali_coord },
        { kalkuta_coord },
        { dushanbe_coord },
        { aktau_coord },
        { aktobe_coord },
        { perm_coord },
        { ufa_coord },
        { ekaterinburg_coord },
        { surgut_coord },
        { chelyabinsk_coord },
        { bishkek_coord },
        { almaata_coord },
        { tekeli_coord },
        { ustkamenogorsk_coord },
        { omsk_coord },
        { novosibirsk_coord },
        { barnaul_coord },
        { tomsk_coord },
        { kophangan_coord },
        { denpasar_coord },
        { mirnyy_coord },
        { habarovsk_coord },
        { vladivostok_coord },
        { petropavlovskkamchatskiy_coord },
        { erevan_coord },
        { tbilisi_coord },
        { samara_coord },
        { volgograd_coord },
        { ulyanovsk_coord },
        { pyatigorsk_coord },
        { stavropol_coord },
        { semikarakorsk_coord },
        { krasnodar_coord },
        { simferopol_coord },
        { donetsk_coord },
        { staryyoskol_coord },
        { voronezh_coord },
        { tambov_coord },
        { kazan_coord },
        { kirov_coord },
        { ryazan_coord },
        { moskva_coord },
        { spb_coord },
        { murmansk_coord },
        { kostomuksha_coord },
        { smolensk_coord },
        { gomel_coord },
        { minsk_coord },
        { harkov_coord },
        { poltava_coord },
        { kremenchug_coord },
        { krivoyrog_coord },
        { kiev_coord },
        { nikolaev_coord },
        { odessa_coord },
        { kolomyya_coord },
        { kishinev_coord },
        { nicosia_coord },
        { riga_coord },
        { jurmala_coord },
        { tallin_coord },
        { vilnyus_coord },
        { varshava_coord },
        { vena_coord },
        { marsel_coord },
        { barcelona_coord },
        { madrid_coord },
        { london_coord },
        { fredericton_coord },
        { toronto_coord },
        { miami_coord },
        { cancun_coord },
        { meadowlake_coord },
    };
    return locations_;
}

std::optional<Location> LocationDb::find_coord(const char *location_name) {
    auto found = std::find_if(
        std::begin(locations()),
        std::end(locations()),
        [=](auto named_coord){
            return named_coord.name == location_name;
        }
    );
    if (found == std::end(locations())) return std::nullopt;
    return *found;
}

// try decreasing latitude until we get all necessary sunrises/sunsets
tl::expected<vp::Vrata, vp::CalcError> decrease_latitude_and_find_vrata(date::year_month_day base_date, Location location) {
    auto l = location;
    l.latitude_adjusted = true;
    while (1) {
        l.latitude.latitude -= 1.0;
        auto vrata = Calc{l}.find_next_vrata(base_date);
        // Return if have actually found vrata.
        // Also return if we ran down to low enough latitudes so that it doesn't
        // make sense to decrease it further; just report whatever error we got in that case.
        if (vrata || l.latitude.latitude <= 60.0) return vrata;
    }
}

vp::VratasForDate calc_one(date::year_month_day base_date, std::string location_name, CalcFlags flags)
{
    vp::VratasForDate vratas;
    auto location = LocationDb::find_coord(location_name.c_str());
    if (!location) {
        vratas.push_back(tl::make_unexpected(CantFindLocation{std::move(location_name)}));
        return vratas;
    }
    vratas.push_back(calc_one(base_date, *location, flags));
    return vratas;
}

tl::expected<vp::Vrata, vp::CalcError> calc_one(date::year_month_day base_date, Location location, CalcFlags flags) {
    // Use immediately-called lambda to ensure Calc is destroyed before more
    // will be created in decrease_latitude_and_find_vrata()
    auto vrata = [&](){
        return Calc{Swe{location, flags}}.find_next_vrata(base_date);
    }();
    if (vrata) return vrata;

    auto e = vrata.error();
    // if we are in the northern areas and the error is that we can't find sunrise or sunset, then try decreasing latitude until it's OK.
    if ((std::holds_alternative<CantFindSunriseAfter>(e) || std::holds_alternative<CantFindSunsetAfter>(e)) && location.latitude.latitude > 60.0) {
        return decrease_latitude_and_find_vrata(base_date, location);
    }
    // Otherwise return whatever error we've got.
    return vrata;
}


void report_details(const vp::MaybeVrata & vrata, fmt::memory_buffer & buf) {
    if (!vrata.has_value()) {
        fmt::format_to(buf,
                       "# {}*\n"
                       "Can't find next Ekadashi, sorry.\n"
                       "* Error: {}\n",
                       vrata->location_name(),
                       vrata.error());
    } else {
        Vrata_Detail_Printer vd{*vrata};
        fmt::format_to(buf, "{}\n\n", vd);
    }

}

// Find next ekAdashI vrata for the named location, report details to the output buffer.
tl::expected<vp::Vrata, vp::CalcError> calc_and_report_one(date::year_month_day base_date, Location location, fmt::memory_buffer & buf) {
    auto vrata = calc_one(base_date, location);
    report_details(vrata, buf);
    return vrata;
}

tl::expected<vp::Vrata, vp::CalcError> find_calc_and_report_one(date::year_month_day base_date, const char * location_name, fmt::memory_buffer & buf) {
    std::optional<Location> coord = LocationDb::find_coord(location_name);
    if (!coord) {
        fmt::format_to(buf, "Location not found: '{}'\n", location_name);
        return tl::make_unexpected(CantFindLocation{location_name});
    }
    return calc_and_report_one(base_date, *coord, buf);
}

void print_detail_header(date::year_month_day base_date, const Location & coord, fmt::memory_buffer & buf)
{
    fmt::format_to(buf,
               "{} {}\n"
               "<to be implemented>\n",
               coord.name, base_date);
}

struct NamedTimePoint {
    std::string name;
    JulDays_UT time_point;
    enum class Print_Tithi : char { No, Yes };
    Print_Tithi print_tithi = Print_Tithi::Yes;
};

void detail_add_tithi_events(vp::JulDays_UT from, vp::JulDays_UT to, const vp::Calc & calc, std::vector<NamedTimePoint> & events) {
    const auto min_tithi = vp::Tithi{std::floor(calc.swe.get_tithi(from).tithi)};
    const auto max_tithi = vp::Tithi{std::ceil(calc.swe.get_tithi(to).tithi)};
    auto start = from - std::chrono::hours{36};
    for (vp::Tithi tithi = min_tithi; tithi <= max_tithi; tithi += 1.0) {
        auto tithi_start = calc.find_tithi_start(start, tithi);
        events.push_back(NamedTimePoint{fmt::format("{:d} starts", tithi), tithi_start, NamedTimePoint::Print_Tithi::No});
        if (tithi.is_dvadashi()) {
            auto dvadashi_quarter_end = calc.find_tithi_start(start, tithi+0.25);
            events.push_back(NamedTimePoint{fmt::format("First quarter of {:d} ends", tithi), dvadashi_quarter_end});
        }
    }

}

std::vector<NamedTimePoint> get_detail_events(date::year_month_day base_date, const vp::Calc & calc) {
    std::vector<NamedTimePoint> events;
    const auto local_astronomical_midnight = calc.calc_astronomical_midnight(base_date);
    const auto sunrise = calc.swe.find_sunrise(local_astronomical_midnight);
    if (sunrise) {
        const auto arunodaya = calc.arunodaya_for_sunrise(*sunrise);
        if (arunodaya) {
            events.push_back(NamedTimePoint{"arunodaya", *arunodaya});
        }
        events.push_back(NamedTimePoint{"sunrise", *sunrise});

        const auto sunset = calc.swe.find_sunset(*sunrise);
        if (sunset) {
            events.push_back(NamedTimePoint{"sunset", *sunset});
            events.push_back(NamedTimePoint{"1/5 of daytime", calc.proportional_time(*sunrise, *sunset, 0.2)});
            events.push_back(NamedTimePoint{"middle of the day", calc.proportional_time(*sunrise, *sunset, 0.5)});
            const auto sunrise2 = calc.swe.find_sunrise(*sunset);
            if (sunrise2) {
                const auto middle_of_night = calc.proportional_time(*sunset, *sunrise2, 0.5);
                events.push_back(NamedTimePoint{"middle of the night", middle_of_night});
                events.push_back(NamedTimePoint{"next sunrise", *sunrise2});
                const auto earliest_timepoint = arunodaya ? * arunodaya : *sunrise;
                const auto latest_timepoint = *sunrise2;
                detail_add_tithi_events(earliest_timepoint, latest_timepoint, calc, events);
            }
        }
    }
    return events;
}

/* print details (-d mode) for a single date and single location */
void print_detail_one(date::year_month_day base_date, Location coord, fmt::memory_buffer & buf, vp::CalcFlags flags) {
    print_detail_header(base_date, coord, buf);

    Calc calc{Swe{coord, flags}};
    std::vector<NamedTimePoint> events = get_detail_events(base_date, calc);
    std::stable_sort(events.begin(), events.end(), [](const NamedTimePoint & left, const NamedTimePoint & right) {
        return left.time_point < right.time_point;
    });
    for (const auto & e : events) {
        if (e.print_tithi == NamedTimePoint::Print_Tithi::Yes) {
            const auto tithi = calc.swe.get_tithi(e.time_point);
            fmt::format_to(buf, "{} {:s}: {}\n", vp::JulDays_Zoned{coord.time_zone(), e.time_point}, tithi, e.name);
        } else {
            fmt::format_to(buf, "{} {}\n", vp::JulDays_Zoned{coord.time_zone(), e.time_point}, e.name);
        }
    }
}

void print_detail_one(date::year_month_day base_date, const char * location_name, fmt::memory_buffer & buf, vp::CalcFlags flags) {
    const std::optional<Location> coord = LocationDb::find_coord(location_name);
    if (!coord) {
        fmt::format_to(buf, "Location not found: '{}'\n", location_name);
        return;
    }
    print_detail_one(base_date, *coord, buf, flags);
}

void calc_and_report_all(date::year_month_day d) {
    for (auto &l : LocationDb()) {
        fmt::memory_buffer buf;
        calc_and_report_one(d, l, buf);
        fmt::print("{}", std::string_view{buf.data(), buf.size()});
    }
}

namespace detail {
    fs::path determine_exe_dir(const char* argv0) {
        return fs::absolute(fs::path{argv0}).parent_path();
    }

    fs::path determine_working_dir(const char* argv0) {
        auto exe_dir = detail::determine_exe_dir(argv0);

        constexpr int max_steps_up = 2;

        for (int step=0; step < max_steps_up; ++step) {
            // Most common case: "eph" and "tzdata" directories exist in the same directory as .exe
            if (fs::exists(exe_dir / "eph")) {
                return exe_dir;
            }

            // But when running exes from Debug/ or Release/ subdirectories, we need to make one step up.
            // But only make one step up: if the data dir still doesn't exist there either, too bad...
            exe_dir = exe_dir.parent_path();
        }
        // fallback: return whatever we got, even if we couldn't find proper working dir.
        return exe_dir;
    }
    std::unordered_map<CalcSettings, vp::VratasForDate, MyHash> cache;

    bool operator==(const CalcSettings & left, const CalcSettings & right)
    {
        return (left.date == right.date) && (left.flags == right.flags);
    }

} // namespace detail

/* Change dir to the directory with eph and tzdata data files (usually it's .exe dir or the one above) */
void change_to_data_dir(const char* argv0)
{
    auto working_dir = detail::determine_working_dir(argv0);
    fs::current_path(working_dir);
}

std::string version()
{
#ifdef VP_VERSION
    return std::string{VP_VERSION};
#else
    return std::string{"unknown"};
#endif
}

std::string program_name_and_version()
{
    return "Vaiṣṇavaṁ Pañcāṅgam " + version();
}

// Try calculating, return true if resulting date range is small enough (suggesting that it's the same ekAdashI for all locations),
// false otherwise (suggesting that we should repeat calculation with adjusted base_date
bool try_calc_all(date::year_month_day base_date, vp::VratasForDate & vratas, CalcFlags flags) {
    std::transform(
        LocationDb().begin(),
        LocationDb().end(),
        std::back_inserter(vratas),
        [base_date, flags](const vp::Location & location) {
            return calc_one(base_date, location, flags);
        });
    return vratas.all_from_same_ekadashi();
}

vp::VratasForDate calc_all(date::year_month_day base_date, CalcFlags flags)
{
    const auto key = detail::CalcSettings{base_date, flags};
    if (auto found = detail::cache.find(key); found != detail::cache.end()) {
        return found->second;
    }
    vp::VratasForDate vratas;

    if (!try_calc_all(base_date, vratas, flags)) {
        vratas.clear();
        date::year_month_day adjusted_base_date = date::sys_days{base_date} - date::days{1};
        try_calc_all(adjusted_base_date, vratas, flags);
    }
    detail::cache[key] = vratas;
    return vratas;
}

} // namespace vp::text_ui
