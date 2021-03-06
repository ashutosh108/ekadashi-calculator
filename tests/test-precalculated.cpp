#include "calc.h"
#include "html-table-parser.h"
#include "location.h"
#include "vrata.h"
#include "vrata_detail_printer.h"

#include "catch-formatters.h"
#include <cstdlib>
#include "date-fixed.h"
#include "fmt-format-fixed.h"
#include "filesystem-fixed.h"
#include <fstream>
#include <map>
#include <unordered_map>
#include <regex>
#include <sstream>
#include <variant>
#include <vector>

using namespace date;
using namespace std::chrono_literals;

std::string slurp_file(const fs::path & filename) {
    std::ifstream f;
    f.open(filename);
    if (!f) {
        throw std::system_error(errno, std::system_category(), fmt::format("can't open file '{}'", filename.string()));
    }

    std::ostringstream sstr;
    sstr << f.rdbuf();
    return sstr.str();
}

static fs::path source_dir_path{fs::path{__FILE__}.parent_path()};

/* decode strings like "30 апреля" as date::month_day.
 * Throw on error.
 */
date::month_day decode_month_day(const std::string& s) {
    static std::unordered_map<std::string, date::month> month_map{
        {"января", date::January},
        {"февраля", date::February},
        {"марта", date::March},
        {"апреля", date::April},
        {"мая", date::May},
        {"іюня", date::June},
        {"іюля", date::July},
        {"августа", date::August},
        {"сентября", date::September},
        {"октября", date::October},
        {"ноября", date::November},
        {"декабря", date::December}
    };
    std::string month_str;
    int day{};
    {
        std::istringstream stream{s};
        stream >> day >> month_str;
    }
    auto iter = month_map.find(month_str);
    if (iter == month_map.end()) {
        throw std::domain_error(fmt::format("can't parse month_day string '{}' ({} {})", s, day, month_str));
    }
    return iter->second / day;
}

using col_to_date = std::map<std::size_t, date::year_month_day>;

// make YYYY-MM-DD from MM-DD, provided that reference_ymd must be shortly before right result.
date::year_month_day append_proper_year_to_md(date::year_month_day reference_ymd, date::month_day md) {
    auto reference_year = reference_ymd.year();
    auto ymd = reference_year / md;
    // result must be >= reference, so adjust if we get otherwise
    if (ymd < reference_ymd) {
        return (++reference_year) / md;
    }
    return ymd;
}

/* return map "column number => date" for table header */
col_to_date get_date_headers(html::Table & t, date::year_month_day base_ymd) {
    std::size_t col_count = t.get_row_length(0);
    CAPTURE(col_count);

    std::map<std::size_t, date::year_month_day> map;

    auto first_cell_text = t.get(0, 0);
    for (std::size_t col=1; col < col_count; ++col) {
        auto cell_text = t.get(0, col);

        // skip all cells identical to the first one (॥ श्रीः ॥).
        if (cell_text == first_cell_text) continue;
        auto ymd = append_proper_year_to_md(base_ymd, decode_month_day(cell_text));
        map[col] = ymd;
    }
    return map;
}

template <class Container, class T>
bool contains(Container & c, T & val) {
    return std::find(c.begin(), c.end(), val) != c.end();
}

/* In: text from cell in pañcāṅgam, possibly including "such-and-such ekAdashI" in Russian language. E.g. "Варӯтӿинӣ экāдащӣ"
 * Detect whether known ekAdashI is described in this cell.
 * fail check on unknown ekAdashI name.
 * Return empty string if ekAdashI is not mentioned at all.
 * Return ekAdashI name otherwise;
 */
std::string get_ekadashi_name(const std::string & text) {
    std::regex regex{"([^\\s,.]+)[ -](э|Э)кāдащӣ"};
    std::smatch match;
    if (!std::regex_search(text, match, regex)) {
        return "";
    }
    std::string ekadashi_name = match[1].str();
    CAPTURE(text, ekadashi_name);
    // require ekadashi_name to be known
    REQUIRE(vp::ekadashi_name_rus_is_valid(ekadashi_name));
    return ekadashi_name;
}

TEST_CASE("get_ekadashi_name works") {
    REQUIRE(get_ekadashi_name("Варӯтӿинӣ экāдащӣ") == "Варӯтӿинӣ");
    REQUIRE(get_ekadashi_name(", Варӯтӿинӣ экāдащӣ, ") == "Варӯтӿинӣ");
}

struct Paranam {
    enum class Precision {
        Minutes,
        Seconds
    };

    std::optional<date::zoned_seconds> start;
    std::optional<date::zoned_seconds> end;
    Precision precision;
    Paranam(std::optional<date::zoned_seconds> start_ = std::nullopt, std::optional<date::zoned_seconds> end_ = std::nullopt, Precision precision_ = Precision::Minutes)
        : start(start_), end(end_), precision(precision_)
    {}
};

bool time_equals(const std::optional<date::zoned_seconds> & t1, const std::optional<date::zoned_seconds> & t2) {
    if (t1.has_value() != t2.has_value()) return false;
    if (!t1 && !t2) return true;
    return t1->get_sys_time() == t2->get_sys_time();
}

bool operator==(const Paranam & one, const Paranam & two) {
    return time_equals(one.start, two.start) &&
            time_equals(one.end, two.end) &&
            one.precision == two.precision;
}

// asymmetric comparison: true if whatever information that precalc paranam has matches our info.
bool precalc_paranam_time_matches_ours(const Paranam & precalc, const vp::Paran & nowcalc) {
    if (precalc.start) {
        if (!nowcalc.paran_start) {
            return false;
        }
        auto now_start_rounded = (precalc.precision == Paranam::Precision::Minutes ?
                                      nowcalc.paran_start->round_to_minute_up() :
                                      nowcalc.paran_start->round_to_second_up());
        auto precalc_start = precalc.start->get_sys_time();
        if (precalc_start != now_start_rounded) {
            return false;
        }
    }
    if (precalc.end) {
        if (!nowcalc.paran_end) {
            return false;
        }
        auto now_end_rounded = (precalc.precision == Paranam::Precision::Minutes ?
                                      nowcalc.paran_end->round_to_minute_down() :
                                      nowcalc.paran_end->round_to_second_down());
        auto precalc_end = precalc.end->get_sys_time();
        if (precalc_end != now_end_rounded) {
            return false;
        }
    }
    return true;
}

struct Precalculated_Vrata {
    date::local_days date;
    vp::Vrata_Type type;
    vp::Location location;
    bool skip = false;
    bool already_fixed = false;
    Paranam paranam;
    Precalculated_Vrata(vp::Location location_,
                        date::year_month_day date_,
                        vp::Vrata_Type type_ = vp::Vrata_Type::Ekadashi,
                        Paranam paranam_= Paranam{})
        : date(date_),
          type(type_),
          location(location_),
          paranam(paranam_)
    {}

    std::string make_zoned(std::optional<vp::JulDays_UT> jd) const {
        if (!jd.has_value()) return "(unspecified)";
        return fmt::to_string(jd->as_zoned_time(location.time_zone()));
    }

    bool operator==(const vp::Vrata_Detail_Printer & nowcalc) const {
        using namespace fmt::literals;
        UNSCOPED_INFO(fmt::format(
            "comparing: {date}<=>{nowcalc_date};\n"
            "{location_name}<=>{nowcalc_location_name};\n"
            "{type}<=>{nowcalc_type};\n"
            "S{paranam_start}<=>{nowcalc_paranam_start};\n"
            "E{paranam_end}<=>{nowcalc_paranam_end}",
            "date"_a = date, "nowcalc_date"_a = nowcalc.vrata.date,
            "location_name"_a = location.name,
            "nowcalc_location_name"_a = nowcalc.vrata.location.name,
            "type"_a = type,
            "nowcalc_type"_a = nowcalc.vrata.type,
            "paranam_start"_a = paranam.start,
            "nowcalc_paranam_start"_a = make_zoned(nowcalc.vrata.paran.paran_start),
            "paranam_end"_a = paranam.end,
            "nowcalc_paranam_end"_a = make_zoned(nowcalc.vrata.paran.paran_end)));
        if (date != nowcalc.vrata.date || location != nowcalc.vrata.location) {
            UNSCOPED_INFO("dates and locations must match, but they don't");
            return false;
        }
        if (type != nowcalc.vrata.type) {
            UNSCOPED_INFO("vrata types must match, but they don't");
            return false;
        }
        if (!precalc_paranam_time_matches_ours(paranam, nowcalc.vrata.paran)) {
            UNSCOPED_INFO("vrata times do not match");
            return false;
        }
        if (nowcalc.vrata.paran.type == vp::Paran::Type::Standard) {
            if (paranam.start) {
                UNSCOPED_INFO(
                    fmt::format("in case of standard paranam, start time must not be set, but it's={}", paranam.start));
            }
            if (paranam.end) {
                UNSCOPED_INFO(
                    fmt::format("in case of standard paranam, end time must not be set, but it's={}", paranam.end));
            }
            return !paranam.start && !paranam.end;
        }
        // it must be ">HH:MM" form, check with 1-min precision
        if (nowcalc.vrata.paran.type == vp::Paran::Type::From_Quarter_Dvadashi) {
            UNSCOPED_INFO("paran_type: From_Quarter_Dvadashi");
            // ">HH:MM" must have start and must NOT have end time
            if (!paranam.start) {
                UNSCOPED_INFO("pAraNam start must be set, but it is not");
                return false;
            }
            if (paranam.end) {
                UNSCOPED_INFO("pAraNam end must NOT be set, but it is set");
                return false;
            }

            if (!nowcalc.vrata.paran.paran_start) {
                UNSCOPED_INFO("now-calculated pAraNam must have start time");
                return false;
            }

            return true;
        }
        if (nowcalc.vrata.paran.type == vp::Paran::Type::Puccha_Dvadashi) {
            UNSCOPED_INFO(fmt::format("paran type: {}", nowcalc.vrata.paran.type));
            // "<HH:MM" might have start time and MUST have end time
            if (!paranam.end) {
                UNSCOPED_INFO("pAraNam end must be set");
                return false;
            }

            if (!nowcalc.vrata.paran.paran_end) {
                UNSCOPED_INFO("now-calculated pAraNam must have end time");
                return false;
            }

            return true;
        }
        throw std::runtime_error("unknown situation in comparison");
    }
};

bool operator==(const Precalculated_Vrata & one, const Precalculated_Vrata & other) {
    return one.date == other.date && one.location == other.location
            && one.type == other.type
            && one.paranam == other.paranam;
}

template<>
struct fmt::formatter<Precalculated_Vrata> : fmt::formatter<std::string_view> {
    template<typename FormatContext>
    auto format(const Precalculated_Vrata & v, FormatContext & ctx) {
        return fmt::format_to(
            ctx.out(), "{}@{} on {}, pAraNam: {}..{}",
            v.type, v.location.name, v.date, v.paranam.start, v.paranam.end);
    }
};

std::chrono::seconds h_m_s_from_string(const std::string & s) {
    std::istringstream stream{s};
    std::chrono::seconds h_m_s;
    stream >> date::parse("%H:%M", h_m_s);
    if (!stream.good()) {
        throw std::runtime_error{fmt::format("can't parse '{}' as HH:MM", s)};
    }

    // if there are more characters to read, they must be ":SS"
    if (stream.rdbuf()->in_avail() != 0) {
        std::chrono::seconds sec;
        stream >> date::parse(":%S", sec);
        if (!stream.good() || stream.rdbuf()->in_avail() != 0) {
            throw std::runtime_error{fmt::format("can't parse '{}' as HH:MM:SS", s)};
        }
        h_m_s += sec;
    }
    if (h_m_s >= 24h) {
        throw std::runtime_error(fmt::format("HH:MM[:SS] is 24 hours or more: '{}'", s));
    }
    return h_m_s;
}

TEST_CASE("h_m_from_string works for basic cases") {
    using namespace std::string_literals;
    using namespace std::chrono;
    REQUIRE(hours{0} + minutes{0} == h_m_s_from_string("0:00"s));
    REQUIRE(hours{0} + minutes{0} == h_m_s_from_string("00:00"s));
    REQUIRE(hours{23} + minutes{45} == h_m_s_from_string("23:45"s));
    REQUIRE(hours{23} + minutes{59} == h_m_s_from_string("23:59"s));
    REQUIRE(hours{23} + minutes{45} == h_m_s_from_string("23:45:00"s));
    REQUIRE(23h + 45min + 15s == h_m_s_from_string("23:45:15"s));
    REQUIRE(23h + 45min + 59s == h_m_s_from_string("23:45:59"s));
    REQUIRE(hours{23} + minutes{59} == h_m_s_from_string("23:59"s));
    // 24hours or more should throw:
    REQUIRE_THROWS(h_m_s_from_string("24:00"));
    REQUIRE_THROWS(h_m_s_from_string("36:15"));
}

Paranam parse_precalc_paranam(const std::string & s,
                              date::year_month_day date,
                              const date::time_zone *time_zone)
{
    std::smatch match;
    // "*" alone or "*;" followed by other descriptions.
    // We also treat empty cell as a standard pAraNam, e.g. Murmansk in https://tatvavadi.ru/pa,.nchaa,ngam/posts/2019-03-29/
    if (s.empty() || std::regex_search(s, match, std::regex{R"~(^\*($|[;,]?\s+))~"})) {
        return {{}, {}};
    } else if (std::regex_search(s, match, std::regex{R"regex(^(?:!\s+)?(\d?\d:\d\d(:\d\d)?)\s*(?:-|—)\s*(\d?\d:\d\d(:\d\d)?)(?:$|\s))regex"})) {
        if (match.size() >= 2) {
            auto start_h_m_s{h_m_s_from_string(match[1].str())};
            auto end_h_m_s{h_m_s_from_string(match[3].str())};
            auto start_sec_empty = match[2].length() == 0;
            auto end_sec_empty = match[4].length() == 0;
            if (start_sec_empty != end_sec_empty) {
                throw std::runtime_error(fmt::format(":SS (seconds) of start and end must be either both set or both empty: {}", s));
            }
            auto start_zoned = date::make_zoned(time_zone, date::local_days(date) + start_h_m_s);
            auto end_zoned = date::make_zoned(time_zone, date::local_days(date) + end_h_m_s);
            return {start_zoned, end_zoned, (start_sec_empty ? Paranam::Precision::Minutes : Paranam::Precision::Seconds)};
        }
    } else if (std::regex_search(s, match, std::regex{R"~(&gt;\s*(\d?\d:\d\d))~"})) {
        if (match.size() >= 1) {
            auto start_h_m_s{h_m_s_from_string(match[1].str())};
            auto start_zoned = date::make_zoned(time_zone, date::local_days(date) + start_h_m_s);
            vp::JulDays_UT start_time{date::local_days(date)+start_h_m_s, time_zone};
            return {start_zoned, std::nullopt};
        }
    } else if (std::regex_search(s, match, std::regex{R"~(&lt;\s*(\d?\d:\d\d))~"})) {
        if (match.size() >= 1) {
            auto end_h_m_s{h_m_s_from_string(match[1].str())};
            auto end_zoned = date::make_zoned(time_zone, date::local_days(date) + end_h_m_s);
            return {std::nullopt, end_zoned};
        }
    }
    throw std::runtime_error(fmt::format("can't parse paran time '{}'", s));
}

bool is_atirikta(const std::string & prev_cell_text, const std::string & cell_text, /*out*/ vp::Vrata_Type & type) {
    if (cell_text.find("Атириктā экāдащӣ") != std::string::npos) {
        type = vp::Vrata_Type::With_Atirikta_Ekadashi;
        return true;
    } else if (cell_text.find("Атириктā двāдащӣ") != std::string::npos) {
        type = vp::Vrata_Type::With_Atirikta_Dvadashi;
        return true;
    }
    // e.g. https://tatvavadi.ru/pa,.nchaa,ngam/posts/2019-03-29/ ko phan gan:
    // "pApamocanI ekAdashI" in both cells.
    if (prev_cell_text == cell_text) {
        type = vp::Vrata_Type::With_Atirikta_Ekadashi;
        return true;
    }
    return false;
}

Precalculated_Vrata get_precalc_ekadashi(const vp::Location & location, [[maybe_unused]] html::Table::Row & row_data, [[maybe_unused]] std::size_t col, [[maybe_unused]] const std::string & ekadashi_name, date::year_month_day date) {
    // TODO: shravanA dvAdashI
    // TODO: joined ekAdashI/atiriktA cells
    vp::Vrata_Type type {vp::Vrata_Type::Ekadashi};
    Paranam paranam;
    if (is_atirikta(row_data[col], row_data[col+1], type)) {
        date::year_month_day day3{date::sys_days(date) + date::days{2}};
        paranam = parse_precalc_paranam(row_data[col+2], day3, location.time_zone());
    } else {
        date::year_month_day day2{date::sys_days(date) + date::days{1}};
        paranam = parse_precalc_paranam(row_data[col+1], day2, location.time_zone());
    }
    return Precalculated_Vrata{location, date, type, paranam};
}

std::string join(const html::Table::Row & v, char joiner=';') {
    std::string joined;
    bool first_string = true;
    for (const auto & pair : v) {
        if (!first_string) {
            joined += joiner;
        }
        first_string = false;
        joined += pair.second;
    }
    return joined;
}

TEST_CASE("join() works") {
    REQUIRE("a;b" == join(html::Table::Row{{1, "a"}, {2, "b"}}));
}

/* Try extracting vrata data from indicated cell.
 */
std::optional<Precalculated_Vrata> try_extract_vrata_from_cell(const vp::Location & location, html::Table::Row & row_data, std::size_t col, col_to_date & date_map) {
    std::string ekadashi_name = get_ekadashi_name(row_data[col]);
    if (ekadashi_name.empty()) return std::nullopt;
    return get_precalc_ekadashi(location, row_data, col, ekadashi_name, date_map[col]);
}

vp::Location find_location_by_name_rus(const std::string & name) {
    static std::vector<std::pair<const char *, vp::Location>> rus_locations {
        { "Одесса", vp::odessa_coord },
        { "Vinnitsa", vp::vinnitsa_coord },
        { "Киев", vp::kiev_coord },
        { "San Francisco", vp::sanfrantsisko_coord },
        { "Tiraspol", vp::tiraspol_coord },
        { "Khmelnytskyi", vp::hmelnitskiy_coord },
        { "Кишинев", vp::kishinev_coord },
        { "Воронеж", vp::voronezh_coord },
        { "Харьков", vp::harkov_coord },
        { "Хабаровск", vp::habarovsk_coord },
        { "Lugansk", vp::lugansk_coord },
        { "Москва", vp::moskva_coord },
        { "Vrindavan", vp::vrindavan_coord },
        { "Los Angeles", vp::losanjeles_coord },
        { "Колката", vp::kalkuta_coord },
        { "Душанбе", vp::dushanbe_coord },
        { "Санкт-Петербург", vp::spb_coord },
        { "Freiburg im Breisgau", vp::freiburg_coord },
        { "Николаев", vp::nikolaev_coord },
        { "Ramenskoye, Moscow Oblast", vp::ramenskoe_m_obl_coord },
        { "Минск", vp::minsk_coord },
        { "Барнаул", vp::barnaul_coord },
        { "Нью-Дели", vp::newdelhi_coord },
        { "Dusseldorf", vp::dusseldorf_coord },
        { "Cologne", vp::koeln_kkd_coord },
        { "Sochi", vp::sochi_coord },
        { "Velikiy Novgorod", vp::novgorod_coord },
        { "Лондон", vp::london_coord },
        { "Manchester", vp::manchester_coord },
        { "Panaji", vp::panaji_coord },
        { "Mumbai", vp::bombey_coord },
        { "Pune", vp::pune_coord },
        { "Симферополь", vp::simferopol_coord },
        { "Манали", vp::manali_coord },
        { "Пятигорск", vp::pyatigorsk_coord },
        { "Киров", vp::kirov_coord },
        { "Washington, D.C.", vp::washington_coord },
        { "Гокарна", vp::gokarna_coord },
        { "Tel Aviv", vp::telaviv_coord },
        { "Томск", vp::tomsk_coord },
        { "Kiel", vp::kiel_coord },
        { "Омск", vp::omsk_coord },
        { "Tashkent", vp::tashkent_coord },
        { "Удупи", vp::udupi_coord },
        { "Варшава", vp::varshava_coord },
        { "Донецк", vp::donetsk_coord },
        { "Тбилиси", vp::tbilisi_coord },
        { "Sukhum", vp::suhum_coord },
        { "Кременчуг", vp::kremenchug_coord },
        { "Puno", vp::puno_coord },
        { "Владивосток", vp::vladivostok_coord },
        { "Pernem", vp::pernem_coord },
        { "Краснодар", vp::krasnodar_coord },
        { "Meadow Lake", vp::meadowlake_coord },
        { "Торонто", vp::toronto_coord },
        { "Фредериктон", vp::fredericton_coord },
        { "Пермь", vp::perm_coord },
        { "Уфа", vp::ufa_coord },
        { "Смоленск", vp::smolensk_coord },
        { "Кривой Рог", vp::krivoyrog_coord },
        { "Петропавловск-Камчатскій", vp::petropavlovskkamchatskiy_coord },
        { "Ко Пха Нган Ко Самуи", vp::kophangan_coord },
        { "Денпасар", vp::denpasar_coord },
        { "Mundelein", vp::mundelein_coord },
        { "Бишкек", vp::bishkek_coord },
        { "Вѣна", vp::vena_coord },
        { "Старый Оскол", vp::staryyoskol_coord },
        { "Edmonton", vp::edmonton_coord },
        { "Новосибирск", vp::novosibirsk_coord },
        { "Ереван", vp::erevan_coord },
        { "Ставрополь", vp::stavropol_coord },
        { "Pokhara", vp::pokhara_coord },
        { "Мурманск", vp::murmansk_coord },
        { "Мирный", vp::mirnyy_coord },
        { "Рига", vp::riga_coord },
        { "Сургут", vp::surgut_coord },
        { "Рязань", vp::ryazan_coord },
        { "Athens", vp::afiny_coord },
        { "Chita", vp::chita_coord },
        { "Полтава", vp::poltava_coord },
        { "Казань", vp::kazan_coord },
        { "Актау", vp::aktau_coord },
        { "Таллин", vp::tallin_coord },
        { "Юрмала", vp::jurmala_coord },
        { "Семикаракорск", vp::semikarakorsk_coord },
        { "Colombo", vp::kolombo_coord },
        { "Ульяновск", vp::ulyanovsk_coord },
        { "Tagbilaran", vp::tagbilaran_coord },
        { "Гомель", vp::gomel_coord },
        { "Екатеринбург", vp::ekaterinburg_coord },
        { "Вильнюс", vp::vilnyus_coord },
        { "Костомукша", vp::kostomuksha_coord },
        { "Алма-Ата", vp::almaata_coord },
        { "Коломыя", vp::kolomyya_coord },
        { "Самара", vp::samara_coord },
        { "Челябинск", vp::chelyabinsk_coord },
        { "Текели", vp::tekeli_coord },
        { "Волгоград", vp::volgograd_coord },
        { "Тамбов", vp::tambov_coord },
        { "Марсель", vp::marsel_coord },
        { "Барселона", vp::barcelona_coord },
        { "Мадрид", vp::madrid_coord },
        { "Майами", vp::miami_coord }
    };
    auto iter = std::find_if(rus_locations.begin(), rus_locations.end(), [&name](const auto & pair) {
        return pair.first == name;
    });
    if (iter == rus_locations.end()) {
        throw std::runtime_error(fmt::format("location {}' is not known in test data, aborting", name));
    }
    return iter->second;
}

/* Extracts single vrata from a row */
Precalculated_Vrata extract_vrata_from_row(html::Table::Row & row, col_to_date & date_map) {
    vp::Location location = find_location_by_name_rus(row[2]);

    // manual loop because occasionally we need to increment the iterator manually to skip some cells
    for (auto iter = date_map.begin(); iter != date_map.end(); ++iter) {
        auto & col = iter->first;

        std::optional<Precalculated_Vrata> vrata = try_extract_vrata_from_cell(location, row, col, date_map);
        if (vrata)
            return *vrata;
    }
    CAPTURE(row, date_map);
    throw std::runtime_error("can't extract vrata from row");
}

date::year_month_day get_ymd_from_slug(const char * slug) {
    std::istringstream s{slug};
    date::year_month_day ymd{};
    s >> date::parse("%Y-%m-%d", ymd);
    if (!s.good()) {
        throw std::runtime_error(std::string{"can't get YYYY-MM-DD from slug: '"} + slug + "'");
    }
    return ymd;
}

std::vector<Precalculated_Vrata> extract_vratas_from_precalculated_table(std::string && s, date::year_month_day reference_ymd) {
    auto p = html::TableParser(std::move(s));
    auto t = p.next_table();
    if (!t) throw std::runtime_error("can't parse table");
    std::vector<Precalculated_Vrata> vratas;
    INFO("extract_vratas_from_precalculated_table()");

    auto date_headers = get_date_headers(*t, reference_ymd);
    CAPTURE(date_headers);
    std::size_t row_count = t->row_count();
    // from row 1 because row 0 is date headers only
    for (size_t row=1; row < row_count; ++row) {
        CAPTURE(row);
        auto & row_data = t->get_row(row);
        CAPTURE(join(row_data));
        REQUIRE(row_data.size() > 3);

        // skip header rows, they have colspan=3 in the beginning
        if (row_data[0] == row_data[1] && row_data[0] == row_data[2]) {
            continue;
        }

        vratas.push_back(extract_vrata_from_row(row_data, date_headers));
    }
    return vratas;
}

TEST_CASE("*do* allow empty pAraNam type cell (treat is like standard pAraNam)") {
    auto vratas = extract_vratas_from_precalculated_table(
                "<table><td><td><td><td>1 января<td>2 января"
                "<tr><td><td><td>Удупи<td>Варӯтӿинӣ экāдащӣ<td>", 2019_y/January/1);
    REQUIRE(vratas.size() == 1);
    Precalculated_Vrata expected{vp::udupi_coord, 2019_y/January/1};
    REQUIRE(expected == vratas[0]);
}

TEST_CASE("precalc parsing: 1. standard ekAdashI with standard pAraNam") {
    auto vratas = extract_vratas_from_precalculated_table(
                "<table><td><td><td><td>1 января<td>2 января"
                "<tr><td><td><td>Удупи<td>Варӯтӿинӣ экāдащӣ<td>*", 2019_y/January/1);
    REQUIRE(vratas.size() == 1);
    Precalculated_Vrata expected{vp::udupi_coord, 2019_y/January/1};
    REQUIRE(expected == vratas[0]);
}

TEST_CASE("precalc parsing: 2. standard ekAdashI with 'start-end' pAraNam") {
    auto vratas = extract_vratas_from_precalculated_table(
                "<table><td><td><td><td>1 января<td>2 января"
                "<tr><td><td><td>Удупи<td>Варӯтӿинӣ экāдащӣ<td>6:07 - 6:08", 2019_y/January/1);
    REQUIRE(vratas.size() == 1);
    // 5:30 is indian timezone shift from UTC
    date::sys_seconds paran_start{date::sys_days(2019_y/January/2) + std::chrono::hours{6} + std::chrono::minutes{7} - std::chrono::minutes{5*60+30}};
    date::sys_seconds paran_end{date::sys_days(2019_y/January/2) + std::chrono::hours{6} + std::chrono::minutes{8} - std::chrono::minutes{5*60+30}};
    Precalculated_Vrata expected{vp::udupi_coord, 2019_y/January/1, vp::Vrata_Type::Ekadashi, Paranam{paran_start, paran_end}};
    REQUIRE(expected == vratas[0]);
}

TEST_CASE("precalc parsing: 3. standard ekAdashI with '> start' pAraNam") {
    auto vratas = extract_vratas_from_precalculated_table(
                "<table><td><td><td><td>1 января<td>2 января"
                "<tr><td><td><td>Удупи<td>Варӯтӿинӣ экāдащӣ<td>&gt;6:07", 2019_y/January/1);
    REQUIRE(vratas.size() == 1);
    // 5:30 is indian timezone shift from UTC
    date::sys_seconds paran_start{date::sys_days(2019_y/January/2) + std::chrono::hours{6} + std::chrono::minutes{7} - std::chrono::minutes{5*60+30}};
    Precalculated_Vrata expected{vp::udupi_coord, 2019_y/January/1, vp::Vrata_Type::Ekadashi, Paranam{paran_start, {}}};
    REQUIRE(expected == vratas[0]);
}

TEST_CASE("precalc parsing: 4. standard ekAdashI with '< end' pAraNam") {
    auto vratas = extract_vratas_from_precalculated_table(
                "<table><td><td><td><td>1 января<td>2 января"
                "<tr><td><td><td>Удупи<td>Варӯтӿинӣ экāдащӣ<td>&lt;6:08", 2019_y/January/1);
    REQUIRE(vratas.size() == 1);
    // 5:30 is indian timezone shift from UTC
    date::sys_seconds paran_end{date::sys_days(2019_y/January/2) + std::chrono::hours{6} + std::chrono::minutes{8} - std::chrono::minutes{5*60+30}};
    Precalculated_Vrata expected{vp::udupi_coord, 2019_y/January/1, vp::Vrata_Type::Ekadashi, Paranam{{}, paran_end}};
    REQUIRE(expected == vratas[0]);
}

void check_precalculated_vrata(const Precalculated_Vrata & vrata) {
    CAPTURE(vrata);
    // Start searching one day before precalculated vrata date because our rules for uposhyatvam (ativRddhAdi)
    // differ with old calculations (arddha-ghaTika before aruNodaya).
    // So sometimes by our calculations it is clean ekAdashI (so the fast is "today", on the first ekAdashI sunrise)
    // whereas old rules (incorrectly) state it was dashamI viddhA (so the fast is one day later).
    const date::local_days start_date = vrata.date - date::days{1};
    // calculate sunrise/sunset by TOP EDGE of sun disc crossing the horizon because
    // the old precalc tables are based on data from "Panchaga" program which used
    // "by edge" setting. Our usual default is "by disc center".
    auto our_vrata = vp::Calc{vp::Swe{vrata.location, vp::CalcFlags::SunriseByDiscEdge}}.find_next_vrata(start_date);
    REQUIRE(our_vrata.has_value());
    auto our_vrata_detail = vp::Vrata_Detail_Printer{*our_vrata};
    REQUIRE(vrata == our_vrata_detail);
}

void check_precalculated_vratas(const std::vector<Precalculated_Vrata> & vratas) {
    for (const auto & v : vratas) {
        if (v.skip) continue;
        check_precalculated_vrata(v);
    }
}

struct FixSkip{};
struct FixShiftStartTime {
    std::chrono::seconds s;
};
struct FixShiftEndTime {
    std::chrono::seconds s;
};
struct FixStart{
    std::optional<std::chrono::minutes> expected;
    std::chrono::minutes new_time;
};
struct FixStartSeconds{
    std::optional<std::chrono::seconds> expected;
    std::chrono::seconds new_time;
};
struct FixRemoveParanStartTime{
    std::chrono::seconds expected;
};
struct FixEnd{
    std::optional<std::chrono::minutes> expected;
    std::chrono::minutes new_time;
};
struct FixEndSeconds{
    std::optional<std::chrono::seconds> expected;
    std::chrono::seconds new_time;
};
struct FixRemoveParanEndTime{
    std::chrono::seconds expected;
};
struct FixVrataDate{
    date::local_days expected;
    date::local_days new_date;
    FixVrataDate(date::year_month_day expected_, date::year_month_day new_date_) : expected(expected_), new_date(new_date_) {}
};
struct FixVrataType{
    vp::Vrata_Type expected;
    vp::Vrata_Type new_type;
};

using FixVariant = std::variant<
    FixSkip,
    FixShiftStartTime,
    FixShiftEndTime,
    FixStart,
    FixStartSeconds,
    FixRemoveParanStartTime,
    FixEnd,
    FixEndSeconds,
    FixRemoveParanEndTime,
    FixVrataDate,
    FixVrataType
>;

using Fixes = std::map<vp::Location, std::vector<FixVariant>>;

/* Make time nullopt provided that it's old value matches the expected */
void remove_time(std::optional<date::zoned_seconds> & time, std::optional<date::zoned_seconds> expected) {
    if (time != expected) {
        throw std::runtime_error(fmt::format("can't remove {} in {}: HH:MM:SS do not match", expected, time));
    }
    time = std::nullopt;
}

void replace_time(std::optional<date::zoned_seconds> & time, std::optional<date::zoned_seconds> expected, date::zoned_seconds new_time) {
    if (time != expected) {
        throw std::runtime_error(fmt::format("can't replace {}=>{} in {}: HH:MM:SS do not match",
                                             expected, new_time, time));
    }
    time = new_time;
}

void shift_time_if_exists(std::optional<date::zoned_seconds> & time, const std::chrono::seconds & shift_by) {
    if (!time) return;
    auto time_zone = time->get_time_zone();
    *time = date::make_zoned(time_zone, time->get_local_time() + shift_by);
}

struct VrataFixer {
    Precalculated_Vrata &vrata;
    VrataFixer(Precalculated_Vrata & vrata_) : vrata(vrata_){}

    void operator()(const FixSkip &) {
        vrata.skip = true;
    }
    void operator()(const FixShiftEndTime & fix) {
        shift_time_if_exists(vrata.paranam.end, fix.s);
    }
    void operator()(const FixShiftStartTime & fix) {
        shift_time_if_exists(vrata.paranam.start, fix.s);
    }
    date::zoned_seconds local_paran_hms_to_zone(const date::time_zone * time_zone, date::local_days date, std::chrono::seconds hms) {
        auto local = date + hms;
        return date::make_zoned(time_zone, local);
    }
    std::optional<date::zoned_seconds> replace_hms(std::optional<date::zoned_seconds> zoned, std::optional<std::chrono::seconds> hms) {
        if (zoned.has_value() != hms.has_value()) {
            std::optional<date::hh_mm_ss<std::chrono::seconds>> hms_for_printing{hms};
            throw std::runtime_error(fmt::format("can't replace hms part of '{}' with '{}': one of them doesn't exist", zoned, hms_for_printing));
        }
        if (!zoned) return std::nullopt;
        auto time_zone = zoned->get_time_zone();
        auto local_days = date::floor<date::days>(zoned->get_local_time());
        return date::make_zoned(time_zone, local_days + *hms);
    }
    void operator()(const FixStart & fix) {
        auto old_time = replace_hms(vrata.paranam.start, fix.expected);
        auto new_date = vrata.date + date::days(vp::is_atirikta(vrata.type) ? 2 : 1);
        auto new_time = local_paran_hms_to_zone(vrata.location.time_zone(), new_date, fix.new_time);
        replace_time(vrata.paranam.start, old_time, new_time);
    }
    void operator()(const FixStartSeconds & fix) {
        auto old_time = replace_hms(vrata.paranam.start, fix.expected);
        auto new_date = vrata.date + date::days(vp::is_atirikta(vrata.type) ? 2 : 1);
        auto new_time = local_paran_hms_to_zone(vrata.location.time_zone(), new_date, fix.new_time);
        replace_time(vrata.paranam.start, old_time, new_time);
        vrata.paranam.precision = Paranam::Precision::Seconds;
    }
    void operator()(const FixEnd & fix) {
        auto old_time = replace_hms(vrata.paranam.end, fix.expected);
        auto new_date = vrata.date + date::days(vp::is_atirikta(vrata.type) ? 2 : 1);
        auto new_time = local_paran_hms_to_zone(vrata.location.time_zone(), new_date, fix.new_time);
        replace_time(vrata.paranam.end, old_time, new_time);
    }
    void operator()(const FixEndSeconds & fix) {
        auto old_time = replace_hms(vrata.paranam.end, fix.expected);
        auto new_date = vrata.date + date::days(vp::is_atirikta(vrata.type) ? 2 : 1);
        auto new_time = local_paran_hms_to_zone(vrata.location.time_zone(), new_date, fix.new_time);
        replace_time(vrata.paranam.end, old_time, new_time);
        vrata.paranam.precision = Paranam::Precision::Seconds;
    }
    void operator()(const FixRemoveParanStartTime & fix) {
        auto old_time = replace_hms(vrata.paranam.start, fix.expected);
        remove_time(vrata.paranam.start, old_time);
    }
    void operator()(const FixRemoveParanEndTime & fix) {
        auto old_time = replace_hms(vrata.paranam.end, fix.expected);
        remove_time(vrata.paranam.end, old_time);
    }
    void operator()(const FixVrataDate & fix) {
        if (vrata.date != fix.expected) {
            throw std::runtime_error(fmt::format("{}: can't replace '{}' by '{}': dates don't match",
                                                 vrata.date, fix.expected, fix.new_date));
        }
        vrata.date = fix.new_date;
    }
    void operator()(const FixVrataType & fix) {
        if (vrata.type != fix.expected) {
            throw std::runtime_error(fmt::format("{}: can't replace {} by {}': types don't match",
                                                 vrata.type, fix.expected, fix.new_type));
        }
        vrata.type = fix.new_type;
    }
};

static vp::Location all_coord{vp::Latitude{0.0}, vp::Longitude{0.0}, "all"};

void fix_vratas(std::vector<Precalculated_Vrata> & vratas, const Fixes & fixes) {
    // first apply individual fixes
    for (auto & vrata : vratas) {
        auto fixer = VrataFixer(vrata);
        auto fix_iter = fixes.find(vrata.location);
        if (fix_iter != fixes.end()) {
            for (auto & fix : fix_iter->second) {
                std::visit(fixer, fix);
                vrata.already_fixed = true;
            }
        }
    }
    // if we have fixes to be applied to all coordinates, apply them,
    // but only to vratas NOT affected by individual fixes.
    if (auto fix_iter=fixes.find(all_coord); fix_iter != fixes.end()) {
        for (auto & vrata : vratas) {
            auto fixer = VrataFixer(vrata);
            if (vrata.already_fixed) continue;
            for (auto & fix : fix_iter->second) {
                std::visit(fixer, fix);
            }
        }
    }
}

void test_one_precalculated_table_slug(const char * slug, const Fixes & fixes={}) {
    CAPTURE(slug);

    std::string filename{std::string{"data/precalculated-"} + slug + ".html"};
    auto s = slurp_file(source_dir_path / filename);
    REQUIRE(!s.empty());

    auto slug_ymd = get_ymd_from_slug(slug);
    //sanity check
    REQUIRE(slug_ymd >= 2000_y/January/1);
    REQUIRE(slug_ymd < 2030_y/January/1);

    CAPTURE(slug_ymd);
    std::vector<Precalculated_Vrata> vratas = extract_vratas_from_precalculated_table(std::move(s), slug_ymd);
    fix_vratas(vratas, fixes);
    CAPTURE(vratas.size());
    check_precalculated_vratas(vratas);
}

TEST_CASE("precalculated ekAdashIs part 1", "[.][precalc]") {
    test_one_precalculated_table_slug(
                "2017-11-12", {
                    {vp::riga_coord, {FixEnd{std::nullopt, 9h + 40min}}},
                    {vp::jurmala_coord, {FixEnd{std::nullopt, 9h + 40min}}}
                });
    test_one_precalculated_table_slug(
                "2017-11-27", {
                    {vp::murmansk_coord, {FixSkip{}}}, // TODO: no sunrise cases
                    {vp::mirnyy_coord, {FixShiftStartTime{+1min}}}, // 10:31, not 10:30
                    {vp::london_coord, {FixShiftStartTime{-1min}}}, // 09:23, not 09:24
                });
//    test_one_precalculated_table_slug("2017-12-11"); // TODO: joined ekAdashI/atiriktA cells
    test_one_precalculated_table_slug(
                "2017-12-26", {
                    {vp::murmansk_coord, {FixSkip{}}}, // TODO: no sunrise cases
                });
    test_one_precalculated_table_slug(
                "2018-01-10", {
                            {vp::petropavlovskkamchatskiy_coord, {FixShiftStartTime{+3min}}},
                            {vp::murmansk_coord, {FixSkip{}}}, // TODO: no sunrise cases
                });
    test_one_precalculated_table_slug(
        "2018-01-23",
        {
            {all_coord,
             {FixShiftEndTime{+1min}}},
            // london (ativRddhAdi is hrasva):
            // 2018-01-27 05:32:59.451759 GMT arddha-ghaTika before aruNodaya1
            // 2018-01-27 05:43:09.707190 GMT 55gh_50vigh (samyam)
            // 2018-01-27 05:44:57.550097 GMT ekAdashI start (21h 12m 59.496s=53.041gh long)
            // 2018-01-27 05:45:42.271037 GMT 55gh_55vigh (hrasva)
            // 2018-01-27 05:48:14.834885 GMT aruNodaya1
            {vp::london_coord,
             {FixVrataDate{2018_y/January/28, 2018_y/January/27},
              FixStart{std::nullopt, 8h + 11min}}},
        });
//    test_one_precalculated_table_slug("2018-02-08"); // TODO: joined ekAdashI/atiriktA cells
    test_one_precalculated_table_slug("2018-02-24");
//    test_one_precalculated_table_slug("2018-03-10"); // TODO: shravaNA dvAdashI
//    test_one_precalculated_table_slug("2018-03-17"); // TODO: non-ekAdashI tables (chAndra-yugAdi etc)
    test_one_precalculated_table_slug("2018-03-23");
//    test_one_precalculated_table_slug("2018-04-09"); // TODO: joined ekAdashI/atiriktA cells
    test_one_precalculated_table_slug(
                "2018-04-24", {
                    {vp::gomel_coord,
                     {FixStartSeconds{std::nullopt, 5h+37min+31s},
                      FixEndSeconds{std::nullopt, 5h+37min+42s}}},
                    {vp::kremenchug_coord,
                     // case manually verified by Ashutosha on 2020-02-21 and confirmed by Narasimha:
                     // the reason for difference is the underlying data difference. Old Panchangam gives sunrise < dvadashi_end
                     // (thus brief interval of pAraNam in Puccha-Dvādaśī), and new data are sunrise > dvadashi, so standard pAraNam.
                     {FixShiftStartTime{+6s},
                      FixShiftEndTime{+42s}}},
                    {vp::fredericton_coord,
                      {FixShiftStartTime{+1min}}},
                });
    test_one_precalculated_table_slug(
                "2018-05-09", {
                    {vp::perm_coord, // dvAdashI quarter and sunrise are very close
                     {FixStart{std::nullopt, 5h+4min}}},
                    {vp::manali_coord,
                     {FixStart{std::nullopt, 5h + 34min}}},
                    {vp::kalkuta_coord,
                     {FixStart{std::nullopt, 5h + 34min}}},
                    {vp::ekaterinburg_coord,
                     {FixStart{std::nullopt, 5h + 04min}}},
                    {vp::petropavlovskkamchatskiy_coord,
                     {FixStartSeconds{5h + 36min + 0s, 5h+35min+51s},
                      FixEndSeconds{5h + 36min + 30s, 5h+36min+39s}}},
                    {all_coord,
                     {FixShiftStartTime{-1min}}},
                });
//    test_one_precalculated_table_slug("2018-05-14_adhimaasa"); // TODO: non-ekadashi tables (asdhimAsa start here)
    test_one_precalculated_table_slug(
                "2018-05-23", {
                    {vp::petropavlovskkamchatskiy_coord,
                     {FixStart{std::nullopt, 6h + 16min}}},
                    {vp::murmansk_coord, // TODO: "no sunset" case for sun disc edge
                     {FixSkip{}}},
                });
    test_one_precalculated_table_slug(
        "2018-06-07",
        {
            {vp::tbilisi_coord,
             {FixRemoveParanEndTime{8h + 33min}}},
            {vp::stavropol_coord,
             {FixEnd{std::nullopt, 7h + 34min}}},
            {vp::staryyoskol_coord,
             {FixRemoveParanEndTime{7h + 33min}}}, // 1/5 is a bit before enf of dvAdashI
            {vp::murmansk_coord,
             {FixSkip{}}}, // TODO: "no sunset" cases
            {vp::tallin_coord,
             {FixEnd{std::nullopt, 7h + 34min}}},
            {all_coord,
             {FixShiftEndTime{+1min}}},
            // fredericton (ativRddhAdi is hrasva):
            // 2018-06-09 04:23:48.586967 ADT arddha-ghaTika before aruNodaya1
            // 2018-06-09 04:29:06.180215 ADT ekAdashI start (22h 55m 32.384s=57.314gh long)
            // 2018-06-09 04:29:27.985529 ADT 55gh_50vigh (samyam)
            // 2018-06-09 04:30:52.835159 ADT 55gh_55vigh (hrasva)
            // 2018-06-09 04:32:17.684870 ADT aruNodaya1
            // ...
            // 2018-06-10 08:56:59.588587 ADT dvAdashI's first quarter ends
            {vp::fredericton_coord,
             {FixVrataDate{2018_y/June/10, 2018_y/June/9},
              FixStart{std::nullopt, 8h + 57min}}},
        });
    test_one_precalculated_table_slug(
                "2018-06-21", {
                    {all_coord,
                     {FixShiftStartTime{+1min}}},
                    {vp::perm_coord,
                     {FixShiftStartTime{+0min}}},
                    {vp::ekaterinburg_coord,
                     {FixShiftStartTime{+0min}}},
                    // kalkutta:
                    // 2018-06-23 03:10:10.678616 IST 55gh (vRddha)
                    // 2018-06-23 03:19:57.244254 IST ekAdashI start (24h 32m 44.059s=61.364gh long)
                    {vp::kalkuta_coord,
                     {FixVrataDate{2018_y/June/23, 2018_y/June/24},
                      FixRemoveParanStartTime{10h + 8min}}},
                    // almaata:
                    // 2018-06-23 03:49:12.810457 +06 55gh (vRddha)
                    // 2018-06-23 03:49:57.244254 +06 ekAdashI start (24h 32m 44.059s=61.364gh long)
                    {vp::almaata_coord,
                     {FixVrataDate{2018_y/June/23, 2018_y/June/24},
                      FixVrataType{vp::Vrata_Type::With_Atirikta_Dvadashi, vp::Vrata_Type::Ekadashi}}},
                    {vp::murmansk_coord,
                     {FixSkip{}}}, // TODO: "no sunset" cases
                });
    test_one_precalculated_table_slug(
        "2018-07-06",
        {
            {vp::mirnyy_coord,
             {FixStart{5h + 18min, 6h + 17min}}},
            // habarovsk (hrasva):
            // 2018-07-09 03:57:44.690776 +10 arddha-ghaTika before aruNodaya1
            // 2018-07-09 04:00:55.573386 +10 ekAdashI start (21h 56m 23.236s=54.849gh long)
            // 2018-07-09 04:03:13.949702 +10 55gh_50vigh (samyam)
            // 2018-07-09 04:04:36.264364 +10 55gh_55vigh (hrasva)
            // 2018-07-09 04:05:58.578783 +10 aruNodaya1
            // 2018-07-09 05:11:49.682639 +10 sunrise1
            // 2018-07-09 20:57:22.943251 +10 sunset1
            // 2018-07-10 01:57:18.809270 +10 dvAdashI start (21h 18m 28.769s=53.270gh long)
            // 2018-07-10 05:12:41.978473 +10 sunrise2
            // 2018-07-10 07:16:56.001490 +10 pAraNam start
            // 2018-07-10 07:16:56.001490 +10 dvAdashI's first quarter ends
            {vp::habarovsk_coord,
             {FixShiftStartTime{-1min}}},
            {vp::vladivostok_coord,
             {FixShiftStartTime{-1min}}},
            {vp::murmansk_coord,
             {FixSkip{}}}, // TODO: "no sunset" cases
        });
    test_one_precalculated_table_slug(
                "2018-07-20", {
                    {vp::toronto_coord,
                     {FixEnd{8h + 25min, 8h + 55min}}},
                    {vp::meadowlake_coord,
                     {FixVrataType{vp::Vrata_Type::Ekadashi, vp::Vrata_Type::With_Atirikta_Dvadashi},
                      FixRemoveParanStartTime{10h + 25min},
                      FixEnd{std::nullopt, 6h + 55min}}},
                });
    test_one_precalculated_table_slug(
        "2018-08-05",
        {
            // vena (ativRddhAdi is hrasva):
            // 2018-08-07 04:17:14.677403 CEST arddha-ghaTika before aruNodaya1
            // 2018-08-07 04:22:46.309078 CEST ekAdashI start (21h 22m 53.175s=53.454gh long)
            // 2018-08-07 04:23:29.139463 CEST 55gh_50vigh (samyam)
            // 2018-08-07 04:25:02.754888 CEST 55gh_55vigh (hrasva)
            // 2018-08-07 04:26:36.369991 CEST aruNodaya1
            // ...
            // 2018-08-08 06:59:24.819275 CEST dvAdashI's first quarter ends
            {vp::vena_coord,
             {FixVrataDate{2018_y/August/8, 2018_y/August/7},
              FixStart{std::nullopt, 7h + 0min}}},
        });
    test_one_precalculated_table_slug(
        "2018-08-19",
        {
            // ufa (ativRddhatva is samyam)
            // 2018-08-21 04:40:23.937500 +05 arddha-ghaTika before aruNodaya1
            // 2018-08-21 04:46:43.803232 +05 ekAdashI start (26h 23m 59.310s=66.000gh long)
            // 2018-08-21 04:46:45.921185 +05 55gh_50vigh (samyam)
            // 2018-08-21 04:48:21.416986 +05 55gh_55vigh (hrasva)
            // 2018-08-21 04:49:56.912263 +05 aruNodaya1
            {vp::ufa_coord,
             {FixVrataDate{2018_y/August/22, 2018_y/August/21},
              FixVrataType{vp::Vrata_Type::Ekadashi, vp::Vrata_Type::With_Atirikta_Ekadashi}}},
            // pAraNam after 1/4 dvAdashI. Difference is probably due to manual calculations and rounding.
            {vp::london_coord,
             {FixShiftStartTime{-2min}}},
        });
    test_one_precalculated_table_slug(
                "2018-08-31", {
                    {vp::kishinev_coord,
                     {FixEnd{std::nullopt, 6h + 42min}}}, //obvious typoin percalc: was 6:34 (sunrise) instead of 6:42 (dvAdashI end)
                    {vp::riga_coord,
                     {FixStartSeconds{6h + 42min, 6h + 41min + 57s}, //rounding
                      FixEndSeconds{6h + 42min + 28s, 6h + 42min + 37s}}}, // old panchanga gives 6:42:38, so :28 is a typo, but actual time is 6:42:37.527, so :37
                    {vp::vilnyus_coord,
                     {FixStartSeconds{6h + 40min + 30s, 6h + 40min + 25s}, //rounding
                      FixEndSeconds{6h + 42min + 28s, 6h + 42min + 37s}}}, //actually 6:42:37, but we round down before checks
                });
//    test_one_precalculated_table_slug("2018-09-12"); // TODO: shravaNA dvAdashI
//    test_one_precalculated_table_slug("2018-09-22"); // TODO: non-ekadashi tables (ananta-caturdashi here)
    test_one_precalculated_table_slug("2018-10-03");
    test_one_precalculated_table_slug("2018-10-18");
    test_one_precalculated_table_slug(
        "2018-11-01",
        {
            // bishkek (ativRddhAdi is samyam):
            // 2018-11-03 05:36:36.737726 +06 arddha-ghaTika before aruNodaya1
            // 2018-11-03 05:40:24.041462 +06 ekAdashI start (22h 03m 42.798s=55.155gh long)
            // 2018-11-03 05:45:50.939508 +06 55gh_50vigh (samyam)
            // 2018-11-03 05:48:09.489913 +06 55gh_55vigh (hrasva)
            // 2018-11-03 05:50:28.040278 +06 aruNodaya1
            // So after introducing ativRddhAdi, this fix for Bishkek is no longer needed.
            // {vp::bishkek_coord,
            //  {FixVrataDate{2018_y/November/3, 2018_y/November/4}, // sandigdha moved it one day forward
            //   FixRemoveParanStartTime{9h+17min}}},
            {vp::almaata_coord,
             {FixVrataDate{2018_y/November/3, 2018_y/November/4}, // sandigdha moved it one day forward
              FixRemoveParanStartTime{9h+17min}}},
            {vp::tekeli_coord,
             {FixVrataDate{2018_y/November/3, 2018_y/November/4}, // sandigdha moved it one day forward
              FixRemoveParanStartTime{9h+17min}}},
        });
    test_one_precalculated_table_slug(
                "2018-11-17", {
                    {all_coord,
                     {FixShiftStartTime{+1min}}},
                });
    test_one_precalculated_table_slug(
                "2018-12-01", {
                    {all_coord,
                     {FixShiftEndTime{+1min}}},
                    {vp::murmansk_coord, {FixSkip{}}}, // TODO: no sunrise cases
                    {vp::kostomuksha_coord,
                     {FixRemoveParanEndTime{9h+49min}}}, // sunrise is after dvadashi end, so it's standard "1/5" pAraNam there
                    {vp::tallin_coord,
                     {FixRemoveParanEndTime{8h+49min}}}, // sunrise is after dvadashi end, so it's standard "1/5" pAraNam there
                });
    test_one_precalculated_table_slug(
        "2018-12-12",
        {
            {all_coord,
             {FixShiftEndTime{+1min},
              FixShiftStartTime{+1min}}},
            // perm (ativRddhAdi is hrasva):
            // 2018-12-18 07:23:56.310267 +05 arddha-ghaTika before aruNodaya1
            // 2018-12-18 07:27:11.265034 +05 ekAdashI start (23h 38m 14.057s=59.093gh long)
            // 2018-12-18 07:35:45.836252 +05 55gh_50vigh (samyam)
            // 2018-12-18 07:38:43.217788 +05 55gh_55vigh (hrasva)
            // 2018-12-18 07:41:40.599365 +05 aruNodaya1
            // ...
            // 2018-12-19 12:48:05.889584 +05 dvAdashI's first quarter ends
            {vp::perm_coord,
             {FixVrataDate{2018_y/December/19, 2018_y/December/18},
              FixStart{std::nullopt, 12h + 49min}}},
            // samara (ativRddhAdi is hrasva):
            // 2018-12-18 06:25:03.088219 +04 arddha-ghaTika before aruNodaya1
            // 2018-12-18 06:27:11.265034 +04 ekAdashI start (23h 38m 14.057s=59.093gh long)
            // 2018-12-18 06:36:08.186845 +04 55gh_50vigh (samyam)
            // 2018-12-18 06:38:54.461541 +04 55gh_55vigh (hrasva)
            // 2018-12-18 06:41:40.736278 +04 aruNodaya1
            // 2018-12-18 08:54:41.920749 +04 sunrise1
            // 2018-12-18 16:17:18.280910 +04 sunset1
            // 2018-12-19 06:05:25.321645 +04 dvAdashI start (22h 50m 42.272s=57.113gh long)
            // 2018-12-19 08:55:23.078683 +04 sunrise2
            // 2018-12-19 10:23:49.915076 +04 1/5 of day2
            // 2018-12-19 11:48:05.889584 +04 pAraNam start
            // 2018-12-19 11:48:05.889584 +04 dvAdashI's first quarter ends
            // After atidRddhAdi calculations, this fix is no longer necessary.
            // {vp::samara_coord,
            //  {FixVrataDate{2018_y/December/18, 2018_y/December/19}, // sandigdha moved it one day forward
            //   FixRemoveParanStartTime{11h+48min}}},
            // pyatigorsk (ativRddhAdi is hrasva)
            // 2018-12-18 05:24:07.243823 MSK arddha-ghaTika before aruNodaya1
            // 2018-12-18 05:27:11.265034 MSK ekAdashI start (23h 38m 14.057s=59.093gh long)
            // 2018-12-18 05:34:16.775580 MSK 55gh_50vigh (samyam)
            // 2018-12-18 05:36:49.158540 MSK 55gh_55vigh (hrasva)
            // 2018-12-18 05:39:21.541539 MSK aruNodaya1
            // 2018-12-18 07:41:15.923109 MSK sunrise1
            // 2018-12-18 16:27:18.344188 MSK sunset1
            // 2018-12-19 05:05:25.321645 MSK dvAdashI start (22h 50m 42.272s=57.113gh long)
            // 2018-12-19 07:41:53.046022 MSK sunrise2
            // 2018-12-19 09:27:02.659595 MSK 1/5 of day2
            // 2018-12-19 10:48:05.889584 MSK pAraNam start
            // 2018-12-19 10:48:05.889584 MSK dvAdashI's first quarter ends
            // After atidRddhAdi calculations, this fix is no longer necessary.
            // {vp::pyatigorsk_coord,
            //  {FixVrataDate{2018_y/December/18, 2018_y/December/19}, // sandigdha moved it one day forward
            //   FixRemoveParanStartTime{10h+48min}}},
            {vp::murmansk_coord, {FixSkip{}}}, // TODO: "no sunrise" cases
        });
    test_one_precalculated_table_slug(
                "2018-12-29", {
                    {vp::murmansk_coord, {FixSkip{}}}, // TODO: "no sunrise" cases
                });
//    test_one_precalculated_table_slug("2019-01-09"); // TODO: non-ekAdashI tables (dhanur-vyatipAta-yoga)
    test_one_precalculated_table_slug(
                "2019-01-13", {
                    {vp::denpasar_coord,
                     {FixStart{std::nullopt, 6h+32min}}},
                });
    test_one_precalculated_table_slug(
                "2019-01-29", {
                    // Fredericton *old* Panchangam data:
                    // 2019-01-29 17:23:43 sunset // 10:31:11 night len */7.5=1:24:09
                    // 2019-01-30 06:03:32 ekAdashI start
                    // 2019-01-30 06:30:45 aruNodaya (sunrise-1/7.5 of night length)
                    // 2019-01-30 07:54:54 sunrise1
                    // 2019-01-31 07:32:05 dvAdashI start
                    // 2019-01-31 07:53:44 sunrise2
                    // 2019-02-01 07:52:32 sunrise3
                    // 2019-02-01 09:49:45 dvAdashI end
                    // so even by old anchangam data this is supposed to be clean ekAdashI on 2019-01-31, not atiriktA on 30th.
                    {vp::fredericton_coord,
                     {FixVrataDate{2019_y/January/30, 2019_y/January/31},
                      FixVrataType{vp::Vrata_Type::With_Atirikta_Dvadashi, vp::Vrata_Type::Ekadashi}}},
                    // miami:
                    // 2019-01-30 04:57:37.552881 EST 55gh (vRddha)
                    // 2019-01-30 05:03:32.497164 EST ekAdashI start (25h 28m 32.335s=63.689gh long)
                    {vp::miami_coord,
                        {FixVrataDate{2019_y/January/30, 2019_y/January/31},
                         FixVrataType{vp::Vrata_Type::With_Atirikta_Dvadashi, vp::Vrata_Type::Ekadashi}}},

                });
    test_one_precalculated_table_slug("2019-02-12");
    test_one_precalculated_table_slug(
                "2019-02-28", {
                    {vp::spb_coord,         // sandigdha: vrata next day
                     {FixVrataDate{2019_y/March/1, 2019_y/March/2},
                      FixRemoveParanStartTime{15h+15min}}},
                    {vp::murmansk_coord,    // sandigdha: vrata next day
                     {FixVrataDate{2019_y/March/1, 2019_y/March/2},
                      FixRemoveParanStartTime{15h+15min}}},
                    {vp::kostomuksha_coord, // sandigdha: vrata next day
                     {FixVrataDate{2019_y/March/1, 2019_y/March/2},
                      FixRemoveParanStartTime{15h+15min}}},
                    {vp::minsk_coord,       // sandigdha: vrata next day
                     {FixVrataDate{2019_y/March/1, 2019_y/March/2},
                      FixRemoveParanStartTime{15h+15min}}},
                    {vp::kolomyya_coord,    // sandigdha: vrata next day
                     {FixVrataDate{2019_y/March/1, 2019_y/March/2},
                      FixRemoveParanStartTime{14h+15min}}},
                    // Riga:
                    // 2019-03-01 05:06:25.244340 EET 55gh (vRddha)
                    // 2019-03-01 05:09:25.159960 EET ekAdashI start (26h 25m 13.905s=66.051gh long)
                    // 2019-03-01 07:21:55 sunrise1
                    // 2019-03-02 07:19:19 sunrise2
                    // 2019-03-02 07:34:39 dvAdashI start
                    {vp::riga_coord,
                     {FixVrataDate{2019_y/March/1, 2019_y/March/2},
                      FixRemoveParanStartTime{14h+15min}}},
                    // Jurmala:
                    // 2019-03-01 05:07:43.524945 EET 55gh (vRddha)
                    // 2019-03-01 05:09:25.159960 EET ekAdashI start (26h 25m 13.905s=66.051gh long)
                    // 2019-03-01 07:23:15 sunrise1
                    // 2019-03-02 07:20:38 sunrise2
                    // 2019-03-02 07:34:39 dvAdashI start
                    {vp::jurmala_coord,
                     {FixVrataDate{2019_y/March/1, 2019_y/March/2},
                      FixRemoveParanStartTime{14h+15min}}},
                    // Tallin:
                    // 2019-03-01 05:07:00.663862 EET 55gh (vRddha)
                    // 2019-03-01 05:09:25.159960 EET ekAdashI start (26h 25m 13.905s=66.051gh long)
                    // 2019-03-01 07:24:09 sunrise1
                    // 2019-03-02 07:21:17 sunrise2
                    // 2019-03-02 07:34:39 dvAdashI start
                    {vp::tallin_coord,
                     {FixVrataDate{2019_y/March/1, 2019_y/March/2},
                      FixRemoveParanStartTime{14h+15min}}},
                    // Vilnius:
                    // 2019-03-01 04:59:10.559069 EET 55gh (vRddha)
                    // 2019-03-01 05:09:25.159960 EET ekAdashI start (26h 25m 13.905s=66.051gh long)
                    // 2019-03-01 07:13:23 sunrise1
                    // 2019-03-02 07:10:59 sunrise2
                    // 2019-03-02 07:34:39 dvAdashI start
                    {vp::vilnyus_coord,
                     {FixVrataDate{2019_y/March/1, 2019_y/March/2},
                      FixRemoveParanStartTime{14h+15min}}},
                    // Warsaw: data from old Panchanga
                    // 2019-03-01 04:09:25 ekAdashI start
                    // 2019-03-01 06:26:39 sunrise1
                    // 2019-03-02 06:24:26 sunrise2
                    // 2019-03-02 06:34:39 dvAdashI start
                    // So it looks like by old data it also should have been atiriktA ekAdashI.
                    {vp::varshava_coord,
                     {FixVrataType{vp::Vrata_Type::Ekadashi, vp::Vrata_Type::With_Atirikta_Ekadashi},
                      FixRemoveParanStartTime{13h+15min}}},
                });
    test_one_precalculated_table_slug(
                "2019-03-15", {
                    {all_coord,
                     {FixShiftEndTime{+60min}}}, // switch to summer time happened earlier than old table's author expected.
                });
    test_one_precalculated_table_slug(
                "2019-03-29", {
                    {vp::almaata_coord,  // 1/5 of day comes before dvAdashI end, so it's standard pAraNam.
                     {FixRemoveParanEndTime{9h+8min}}},
                    {vp::tekeli_coord,  // 1/5 of day comes before dvAdashI end, so it's standard pAraNam.
                     {FixRemoveParanEndTime{9h+8min}}},
                    // ko pha ngan .. petropavlovsk: simple cell rowspan error in precalc table.
                    {vp::kophangan_coord,
                     {FixVrataDate{2019_y/March/31, 2019_y/April/1},
                      FixVrataType{vp::Vrata_Type::With_Atirikta_Ekadashi, vp::Vrata_Type::Ekadashi}}},
                    {vp::denpasar_coord,
                     {FixVrataDate{2019_y/March/31, 2019_y/April/1},
                      FixVrataType{vp::Vrata_Type::With_Atirikta_Ekadashi, vp::Vrata_Type::Ekadashi}}},
                    {vp::mirnyy_coord,
                     {FixVrataDate{2019_y/March/31, 2019_y/April/1},
                      FixVrataType{vp::Vrata_Type::With_Atirikta_Ekadashi, vp::Vrata_Type::Ekadashi}}},
                    {vp::habarovsk_coord,
                     {FixVrataDate{2019_y/March/31, 2019_y/April/1},
                      FixVrataType{vp::Vrata_Type::With_Atirikta_Ekadashi, vp::Vrata_Type::Ekadashi}}},
                    {vp::vladivostok_coord,
                     {FixVrataDate{2019_y/March/31, 2019_y/April/1},
                      FixVrataType{vp::Vrata_Type::With_Atirikta_Ekadashi, vp::Vrata_Type::Ekadashi}}},
                    {vp::petropavlovskkamchatskiy_coord,
                     {FixVrataDate{2019_y/March/31, 2019_y/April/1},
                      FixVrataType{vp::Vrata_Type::With_Atirikta_Ekadashi, vp::Vrata_Type::Ekadashi}}},
                    {vp::krasnodar_coord,
                     {FixStartSeconds{6h + 7min, 6h + 7min + 8s},
                      FixEndSeconds{6h + 8min, 6h + 8min + 48s}}},
                    {all_coord,
                     {FixShiftStartTime{-2min}}}, // e.g. 10:15 => 10:13 in Simferopol. Must be typo or manual calculations error of 1/4 of dvAdashI
                    {vp::staryyoskol_coord,
                     {FixStartSeconds{6h + 7min, 6h + 6min + 48s},
                      FixEndSeconds{6h + 8min, 6h + 8min + 48s}}},
                    {vp::murmansk_coord,    // empty cell in precalc table, but pAraNam interval is quite short (sunrise..end-of-dvAdashI)
                     {FixEnd{std::nullopt, 6h+8min}}},
                });
    test_one_precalculated_table_slug(
        "2019-04-11",
        {
            {vp::petropavlovskkamchatskiy_coord,
             {FixEnd{7h + 55min, 7h + 56min}}},     // discrepancy reason is not clear
            // gomel (ativRddhAdi is hrasva):
            // 2019-04-15 04:31:13.806077 +03 arddha-ghaTika before aruNodaya1
            // 2019-04-15 04:38:03.931231 +03 55gh_50vigh (samyam)
            // 2019-04-15 04:38:23.925893 +03 ekAdashI start (21h 14m 51.571s=53.119gh long)
            // 2019-04-15 04:39:46.462560 +03 55gh_55vigh (hrasva)
            // 2019-04-15 04:41:28.994050 +03 aruNodaya1
            // 2019-04-15 06:03:30.497631 +03 sunrise1
            // 2019-04-15 19:50:02.871448 +03 sunset1
            // 2019-04-16 01:53:15.496876 +03 dvAdashI start (21h 02m 51.995s=52.619gh long)
            // 2019-04-16 06:01:18.089337 +03 sunrise2
            // 2019-04-16 07:08:58.495517 +03 pAraNam start
            // 2019-04-16 07:08:58.495517 +03 dvAdashI's first quarter ends
            // So after ativRddhAdi, this fix is no longer necessary.
            // {vp::gomel_coord, // sandigdha moved vrata one day ahead
            //  {FixVrataDate{2019_y/April/15, 2019_y/April/16},
            //   FixRemoveParanStartTime{7h+13min}}},
            {all_coord,
             {FixShiftStartTime{-4min}}}, // e.g. 07:13 => 07:09 for Minsk (quarter of dvAdashI)
            // Kremenchug old Panchangam data:
            // 2019-04-15 04:36:08 aruNodaya
            // 2019-04-15 04:38:24 ekAdashI start
            // 2019-04-15 05:59:45 sunrise0
            // 2019-04-16 01:53:15 dvAdashI start
            // so even from old data this should have been ekAdashI on 16th, not 15th.
            {vp::kremenchug_coord,
             {FixVrataDate{2019_y/April/15, 2019_y/April/16},
              FixRemoveParanStartTime{7h+13min}}},
            {vp::krivoyrog_coord, //sandigdha moved vrata one day ahead
             {FixVrataDate{2019_y/April/15, 2019_y/April/16},
              FixRemoveParanStartTime{7h+13min}}},
            // kiev (ativRddhAdi is hrasva):
            // 2019-04-15 04:35:15.669541 EEST arddha-ghaTika before aruNodaya1
            // 2019-04-15 04:38:23.925893 EEST ekAdashI start (21h 14m 51.571s=53.119gh long)
            // 2019-04-15 04:42:10.630436 EEST 55gh_50vigh (samyam)
            // 2019-04-15 04:43:54.370691 EEST 55gh_55vigh (hrasva)
            // 2019-04-15 04:45:38.111025 EEST aruNodaya1
            // 2019-04-15 06:08:37.643058 EEST sunrise1
            // 2019-04-15 19:47:46.215379 EEST sunset1
            // 2019-04-16 01:53:15.496876 EEST dvAdashI start (21h 02m 51.995s=52.619gh long)
            // 2019-04-16 06:06:33.781368 EEST sunrise2
            // 2019-04-16 07:08:58.495517 EEST pAraNam start
            // 2019-04-16 07:08:58.495517 EEST dvAdashI's first quarter ends
            // No fix necessary with ativRddhAdi.
            // {vp::kiev_coord, //sandigdha moved vrata one day ahead
            //  {FixVrataDate{2019_y/April/15, 2019_y/April/16},
            //   FixRemoveParanStartTime{7h+13min}}},
            // nikolaev (ativRddhAdi is hrasva):
            // 2019-04-15 04:33:41.159492 EEST arddha-ghaTika before aruNodaya1
            // 2019-04-15 04:38:23.925893 EEST ekAdashI start (21h 14m 51.571s=53.119gh long)
            // 2019-04-15 04:40:43.580054 EEST 55gh_50vigh (samyam)
            // 2019-04-15 04:42:29.185194 EEST 55gh_55vigh (hrasva)
            // 2019-04-15 04:44:14.790495 EEST aruNodaya1
            // 2019-04-15 06:08:43.838398 EEST sunrise1
            // 2019-04-15 19:36:27.924510 EEST sunset1
            // 2019-04-16 01:53:15.496876 EEST dvAdashI start (21h 02m 51.995s=52.619gh long)
            // 2019-04-16 06:06:53.027452 EEST sunrise2
            // 2019-04-16 07:08:58.495517 EEST pAraNam start
            // 2019-04-16 07:08:58.495517 EEST dvAdashI's first quarter ends
            // No fix necessary with ativRddhAdi.
            // {vp::nikolaev_coord, //sandigdha moved vrata one day ahead
            //  {FixVrataDate{2019_y/April/15, 2019_y/April/16},
            //   FixRemoveParanStartTime{7h+13min}}},
            {vp::marsel_coord, // precalc's paran start 06:13 is wrong: it's dvAdashI's 1/4, but it's before sunrise. Actual pAraNam is standard, from sunrise.
             {FixRemoveParanStartTime{6h+13min}}},
        });
    test_one_precalculated_table_slug(
                "2019-04-27", {
                    // 2019-05-01 05:14:29.517097 +04 sunrise2
                    // 2019-05-01 05:14:45.915399 +04 pAraNam start
                    {vp::ulyanovsk_coord,
                     {FixStart{std::nullopt, 5h + 15min}}},
                });
    test_one_precalculated_table_slug(
                "2019-05-13", {
                    // Delhi: 1/5 of daytime (8:14:48) is close to dvadashi start (8:15:xx), so actually we shouldn't specify the paran end time here
                    {vp::newdelhi_coord,
                     {FixRemoveParanEndTime{8h+15min}}}, // dvAdashI end and 1/5 are close
                    {vp::manali_coord,
                     {FixRemoveParanEndTime{8h+15min}}}, // dvAdashI end and 1/5 are close
                    {vp::kalkuta_coord,
                     {FixRemoveParanEndTime{8h+15min}}},
                    {vp::surgut_coord,
                     {FixRemoveParanEndTime{7h+45min}}},
                    {vp::varshava_coord,
                     {FixStartSeconds{4h+44min+45s, 4h+44min+42s},
                      FixEndSeconds{4h+45min+20s, 4h+45min+30s}}},
                    {vp::fredericton_coord,
                     {FixShiftStartTime{+29min}}},
                    {vp::toronto_coord,
                     {FixShiftStartTime{+29min}}},
                });
}

TEST_CASE("precalculacted ekAdashIs interim test (to be moved under [.][precalc] tags later)") {
}

TEST_CASE("precalculated ekAdashIs part 2", "[.][precalc]") {
//    test_one_precalculated_table_slug("2019-05-28");
//    test_one_precalculated_table_slug("2019-06-11");
//    test_one_precalculated_table_slug("2019-06-26");
//    test_one_precalculated_table_slug("2019-07-10");
//    test_one_precalculated_table_slug("2019-07-27");
//    test_one_precalculated_table_slug("2019-08-04");
//    test_one_precalculated_table_slug("2019-08-08");
//    test_one_precalculated_table_slug("2019-08-20");
//    test_one_precalculated_table_slug("2019-09-01");
//    test_one_precalculated_table_slug("2019-09-07");
//    test_one_precalculated_table_slug("2019-09-23");
//    test_one_precalculated_table_slug("2019-10-04");
//    test_one_precalculated_table_slug("2019-10-22");
//    test_one_precalculated_table_slug("2019-11-05");
//    test_one_precalculated_table_slug("2019-11-20");
//    test_one_precalculated_table_slug("2019-12-01");
//    test_one_precalculated_table_slug("2019-12-16");
//    test_one_precalculated_table_slug("2019-12-31");
//    test_one_precalculated_table_slug("2020-01-03");
//    test_one_precalculated_table_slug("2020-01-12");
//    test_one_precalculated_table_slug("2020-01-18");
//    test_one_precalculated_table_slug("2020-01-30");
//    test_one_precalculated_table_slug("2020-02-18");
}
