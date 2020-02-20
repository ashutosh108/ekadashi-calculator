#include "calc.h"
#include "html-table-parser.h"
#include "location.h"
#include "vrata.h"
#include "vrata_detail.h"

#include "catch.hpp"
#include "date-fixed.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>

std::string slurp_file(const std::filesystem::path & filename) {
    std::ifstream f;
    f.open(filename);
    if (!f) {
        throw std::system_error(errno, std::system_category(), "can't open file '" + filename.string() + "'");
    }

    std::stringstream sstr;
    sstr << f.rdbuf();
    return sstr.str();
}

static std::filesystem::path source_dir_path{std::filesystem::path{__FILE__}.parent_path()};

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
        {"июня", date::June},
        {"июля", date::July},
        {"августа", date::August},
        {"сентября", date::September},
        {"октября", date::October},
        {"ноября", date::November},
        {"декабря", date::December}
    };
    std::string month_str;
    int day;
    {
        std::stringstream stream{s};
        stream >> day >> month_str;
    }
    auto iter = month_map.find(month_str);
    if (iter == month_map.end()) {
        throw std::domain_error("can't parse month_day string '" + s + "' (" + std::to_string(day) + " " + month_str + ")");
    }
    return iter->second / day;
}

using col_to_date = std::map<std::size_t, date::year_month_day>;

/* return map "column number => date" for table header */
col_to_date get_date_headers(html::Table & t, date::year year) {
    std::size_t col_count = t.get_row_length(0);
    CAPTURE(col_count);

    std::map<std::size_t, date::year_month_day> map;

    auto first_cell_text = t.get(0, 0);
    for (std::size_t col=1; col < col_count; ++col) {
        auto cell_text = t.get(0, col);

        // skip all cells identical to the first one (॥ श्रीः ॥).
        if (cell_text == first_cell_text) continue;
        auto ymd = year / decode_month_day(cell_text);
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
    REQUIRE(contains(vp::ekadashi_names_rus(), ekadashi_name));
    return ekadashi_name;
}

TEST_CASE("get_ekadashi_name works") {
    REQUIRE(get_ekadashi_name("Варӯтӿинӣ экāдащӣ") == "Варӯтӿинӣ");
    REQUIRE(get_ekadashi_name(", Варӯтӿинӣ экāдащӣ, ") == "Варӯтӿинӣ");
}

struct Precalculated_Vrata {
    date::year_month_day date;
    vp::Vrata_Type type;
    vp::Location location;
    std::optional<vp::JulDays_UT> paranam_start;
    std::optional<vp::JulDays_UT> paranam_end;
    bool skip = false;
    Precalculated_Vrata(vp::Location location_,
                        date::year_month_day date_,
                        vp::Vrata_Type type_ = vp::Vrata_Type::Ekadashi,
                        std::optional<vp::JulDays_UT> paranam_start_ = std::nullopt,
                        std::optional<vp::JulDays_UT> paranam_end_ = std::nullopt)
        : date(date_),
          type(type_),
          location(location_),
          paranam_start(paranam_start_),
          paranam_end(paranam_end_) {}
    bool operator==(const vp::Vrata_Detail & other) const {
        UNSCOPED_INFO("comparing: " << date << "<=>" << other.vrata.date << "; "
                      << location.name << "<=>" << other.location.name << "; "
                      << type << "<=>" << other.vrata.type);
        if (date != other.vrata.date || location != other.location
                || type != other.vrata.type) return false;
        if (other.vrata.paran.type == vp::Paran::Type::Standard) {
            UNSCOPED_INFO("paranam_start=" << paranam_start << ", end=" << paranam_end);
            // in case of standard paranam, start and end time must not be set
            return !paranam_start && !paranam_end;
        }
        // it must be ">HH:MM" form, check with 1-min precision
        if (other.vrata.paran.type == vp::Paran::Type::From_Quarter_Dvadashi) {
            UNSCOPED_INFO("paran_type: From_Quarter_Dvadashi");
            // ">HH:MM" must have start and must NOT have end time
            if (!paranam_start) return false;
            if (paranam_end) return false;

            if (!other.vrata.paran.paran_start) return false; // now-calculated paran must have start time

            auto other_rounded = other.vrata.paran.paran_start->round_to_minute_up();
            return *paranam_start == other_rounded;
        }
        if (other.vrata.paran.type == vp::Paran::Type::Puccha_Dvadashi || other.vrata.paran.type == vp::Paran::Type::Until_Dvadashi_End) {
            UNSCOPED_INFO("paran type: " << other.vrata.paran.type);
            // "<HH:MM" might have start time and MUST have end time
            if (!paranam_end) return false;

            if (!other.vrata.paran.paran_end) return false; // now-calculated paran must have end time

            auto other_rounded = other.vrata.paran.paran_end->round_to_minute_down();
            return *paranam_end == other_rounded;
        }
        UNSCOPED_INFO("paranam comparison: start " << paranam_start << " <=> " << other.vrata.paran.paran_start->round_to_minute_up()
                      << "\nend " << paranam_end << " <=> " << other.vrata.paran.paran_end->round_to_minute_down());
        return paranam_start == other.vrata.paran.paran_start.value().round_to_minute_up() &&
                paranam_end == other.vrata.paran.paran_end.value().round_to_minute_down();
    }
    friend std::ostream & operator<<(std::ostream & s, const Precalculated_Vrata & v);
};

bool operator==(const Precalculated_Vrata & one, const Precalculated_Vrata & other) {
    return one.date == other.date && one.location == other.location
            && one.type == other.type
            && one.paranam_start == other.paranam_start
            && one.paranam_end == other.paranam_end;
}

std::ostream & operator<<(std::ostream & s, const Precalculated_Vrata & v) {
    s << v.type << "@" << v.location.name << " on " << v.date;
    s << ", pAraNam: " << v.paranam_start << ".." << v.paranam_end;
    return s;
}

std::chrono::minutes h_m_from_string(const std::string & s) {
    std::istringstream stream{s};
    std::chrono::minutes h_m;
    stream >> date::parse("%H:%M", h_m);
    if (!stream.good()) {
        throw std::runtime_error{"can't parse '" + s + "' as HH:MM"};
    }
    return h_m;
}

TEST_CASE("h_m_from_string works for basic cases") {
    using namespace std::string_literals;
    using namespace std::chrono;
    REQUIRE(hours{0} + minutes{0} == h_m_from_string("0:00"s));
    REQUIRE(hours{0} + minutes{0} == h_m_from_string("00:00"s));
    REQUIRE(hours{23} + minutes{45} == h_m_from_string("23:45"s));
    REQUIRE(hours{23} + minutes{59} == h_m_from_string("23:59"s));
}


std::pair<std::optional<vp::JulDays_UT>, std::optional<vp::JulDays_UT>> parse_precalc_paranam(std::string s, date::year_month_day date, const char * timezone_name) {
    using namespace date;
    if (s == "*") {
        return {{}, {}};
    }
    std::smatch match;
    if (std::regex_search(s, match, std::regex{R"~((\d?\d:\d\d) (?:-|—) (\d?\d:\d\d))~"})) {
        if (match.size() >= 2) {
            std::chrono::minutes start_h_m{h_m_from_string(match[1].str())};
            std::chrono::minutes end_h_m{h_m_from_string(match[2].str())};
            auto time_zone = date::locate_zone(timezone_name);
            vp::JulDays_UT start_time{date::local_days{date}+start_h_m, time_zone};
            vp::JulDays_UT end_time{date::local_days{date}+end_h_m, time_zone};
            return {start_time, end_time};
        }
    } else if (std::regex_search(s, match, std::regex{R"~(&gt;\s*(\d?\d:\d\d))~"})) {
        if (match.size() >= 1) {
            std::chrono::minutes start_h_m{h_m_from_string(match[1].str())};
            auto time_zone = date::locate_zone(timezone_name);
            vp::JulDays_UT start_time{date::local_days{date}+start_h_m, time_zone};
            return {start_time, std::nullopt};
        }
    } else if (std::regex_search(s, match, std::regex{R"~(&lt;\s*(\d?\d:\d\d))~"})) {
        if (match.size() >= 1) {
            std::chrono::minutes end_h_m{h_m_from_string(match[1].str())};
            auto time_zone = date::locate_zone(timezone_name);
            vp::JulDays_UT end_time{date::local_days{date}+end_h_m, time_zone};
            return {std::nullopt, end_time};
        }
    }
    throw std::runtime_error("can't parse paran time '" + s + "'");
}

bool is_atirikta(std::string cell_text, /*out*/ vp::Vrata_Type & type) {
    if (cell_text.find("Атириктā экāдащӣ") != std::string::npos) {
        type = vp::Vrata_Type::Atirikta_Ekadashi;
        return true;
    } else if (cell_text.find("Атириктā двāдащӣ") != std::string::npos) {
        type = vp::Vrata_Type::With_Atirikta_Dvadashi;
        return true;
    }
    return false;
}

Precalculated_Vrata get_precalc_ekadashi(const vp::Location & location, [[maybe_unused]] html::Table::Row & row_data, [[maybe_unused]] std::size_t col, [[maybe_unused]] const std::string & ekadashi_name, date::year_month_day date) {
    // TODO: extract vrata type and pAraNam time.
    vp::Vrata_Type type {vp::Vrata_Type::Ekadashi};
    std::optional<vp::JulDays_UT> paranam_start;
    std::optional<vp::JulDays_UT> paranam_end;
    if (is_atirikta(row_data[col+1], type)) {
        date::year_month_day day3{date::sys_days{date} + date::days{2}};
        std::tie(paranam_start, paranam_end) = parse_precalc_paranam(row_data[col+2], day3, location.timezone_name);
    } else {
        date::year_month_day day2{date::sys_days{date} + date::days{1}};
        std::tie(paranam_start, paranam_end) = parse_precalc_paranam(row_data[col+1], day2, location.timezone_name);
    }
    return Precalculated_Vrata{location, date, type, paranam_start, paranam_end};
}

std::string join(const html::Table::Row & v, char joiner=';') {
    std::string joined;
    bool first = true;
    for (const auto & [col, cell]: v) {
        if (!first) {
            joined += joiner;
        }
        first = false;
        joined += cell;
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
        { "Kiev", vp::kiel_coord },
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
        { "Фредериктон", vp::frederikton_coord },
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
        { "Юрмала", vp::yurmala_coord },
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
        { "Madrid", vp::madrid_coord },
        { "Miami", vp::miami_coord }
    };
    auto iter = std::find_if(rus_locations.begin(), rus_locations.end(), [&name](const auto & pair) {
        return pair.first == name;
    });
    if (iter == rus_locations.end()) {
        throw std::runtime_error("location '" + name + "' is not known in test data, aborting");
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

date::year get_year_from_slug(const char * slug) {
    std::istringstream s{slug};
    date::year year{};
    s >> date::parse("%Y", year);
    return year;
}

std::vector<Precalculated_Vrata> extract_vratas_from_precalculated_table(std::string && s, date::year year) {
    auto p = html::TableParser(std::move(s));
    auto t = p.next_table();
    if (!t) throw std::runtime_error("can't parse table");
    std::vector<Precalculated_Vrata> vratas;
    INFO("extract_vratas_from_precalculated_table()")

    auto date_headers = get_date_headers(*t, year);
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

        // TODO: check that timezone in the table matches our data (move to extract_vrata?)

        vratas.push_back(extract_vrata_from_row(row_data, date_headers));
    }
    return vratas;
}

// TODO: check that we extract all relevant info in cases of:
// 5-12. same four cases for atirikA ekAdashI and atiriktA dvAdashI
TEST_CASE("do not allow empty paran type cell") {
    REQUIRE_THROWS_AS(
                extract_vratas_from_precalculated_table(
                "<table><td><td><td><td>1 января<td>2 января"
                "<tr><td><td><td>Удупи<td>Варӯтӿинӣ экāдащӣ<td>", date::year{2019}),
                std::runtime_error);
}

TEST_CASE("precalc parsing: 1. standard ekAdashI with standard pAraNam") {
    using namespace date;
    auto vratas = extract_vratas_from_precalculated_table(
                "<table><td><td><td><td>1 января<td>2 января"
                "<tr><td><td><td>Удупи<td>Варӯтӿинӣ экāдащӣ<td>*", 2019_y);
    REQUIRE(vratas.size() == 1);
    Precalculated_Vrata expected{vp::udupi_coord, 2019_y/January/1};
    REQUIRE(expected == vratas[0]);
}

TEST_CASE("precalc parsing: 2. standard ekAdashI with 'start-end' pAraNam") {
    using namespace date;
    auto vratas = extract_vratas_from_precalculated_table(
                "<table><td><td><td><td>1 января<td>2 января"
                "<tr><td><td><td>Удупи<td>Варӯтӿинӣ экāдащӣ<td>6:07 - 6:08", 2019_y);
    REQUIRE(vratas.size() == 1);
    // 5:30 is indian timezone shift from UTC
    vp::JulDays_UT paran_start{2019_y/January/2, std::chrono::hours{6} + std::chrono::minutes{7} - std::chrono::minutes{5*60+30}};
    vp::JulDays_UT paran_end{2019_y/January/2, std::chrono::hours{6} + std::chrono::minutes{8} - std::chrono::minutes{5*60+30}};
    Precalculated_Vrata expected{vp::udupi_coord, 2019_y/January/1, vp::Vrata_Type::Ekadashi, paran_start, paran_end};
    REQUIRE(expected == vratas[0]);
}

TEST_CASE("precalc parsing: 3. standard ekAdashI with '> start' pAraNam") {
    using namespace date;
    auto vratas = extract_vratas_from_precalculated_table(
                "<table><td><td><td><td>1 января<td>2 января"
                "<tr><td><td><td>Удупи<td>Варӯтӿинӣ экāдащӣ<td>&gt;6:07", 2019_y);
    REQUIRE(vratas.size() == 1);
    // 5:30 is indian timezone shift from UTC
    vp::JulDays_UT paran_start{2019_y/January/2, std::chrono::hours{6} + std::chrono::minutes{7} - std::chrono::minutes{5*60+30}};
    std::optional<vp::JulDays_UT> paran_end{};
    Precalculated_Vrata expected{vp::udupi_coord, 2019_y/January/1, vp::Vrata_Type::Ekadashi, paran_start, paran_end};
    REQUIRE(expected == vratas[0]);
}

TEST_CASE("precalc parsing: 4. standard ekAdashI with '< end' pAraNam") {
    using namespace date;
    auto vratas = extract_vratas_from_precalculated_table(
                "<table><td><td><td><td>1 января<td>2 января"
                "<tr><td><td><td>Удупи<td>Варӯтӿинӣ экāдащӣ<td>&lt;6:08", 2019_y);
    REQUIRE(vratas.size() == 1);
    // 5:30 is indian timezone shift from UTC
    std::optional<vp::JulDays_UT> paran_start{};
    vp::JulDays_UT paran_end{2019_y/January/2, std::chrono::hours{6} + std::chrono::minutes{8} - std::chrono::minutes{5*60+30}};
    Precalculated_Vrata expected{vp::udupi_coord, 2019_y/January/1, vp::Vrata_Type::Ekadashi, paran_start, paran_end};
    REQUIRE(expected == vratas[0]);
}

void check_precalculated_vrata(const Precalculated_Vrata & vrata) {
    CAPTURE(vrata);
    auto our_vrata = vp::Calc{vrata.location}.find_next_vrata(vrata.date);
    REQUIRE(our_vrata.has_value());
    auto our_vrata_detail = vp::Vrata_Detail{*our_vrata, vrata.location};
    REQUIRE(vrata == our_vrata_detail);
}

void check_precalculated_vratas(const std::vector<Precalculated_Vrata> & vratas) {
    for (const auto & v : vratas) {
        if (v.skip) continue;
        check_precalculated_vrata(v);
    }
}

enum class Fix {
    ParanStartTime, ParanEndTime, Skip
};

struct FixItem {
    const vp::Location & loc;
    Fix type;
    std::string from;
    std::string to;
};

using Fixes = std::vector<FixItem>;

date::local_time<std::chrono::seconds> local_from_y_m_d_h_m(const std::string & y_m_d_h_m) {
    std::istringstream s{y_m_d_h_m};
    date::local_time<std::chrono::seconds> local_time;
    s >> date::parse("%Y-%m-%d %H:%M", local_time);
    if (!s.good()) {
        throw std::runtime_error("can't parse " + y_m_d_h_m + " as local time");
    }
    return local_time;
}

enum class Round { Up, Down };

typedef void (vp::JulDays_UT::*Rounder)(void);

void replace_time(std::optional<vp::JulDays_UT> & time, const std::string & from_str, const std::string & to_str, const char * timezone_name) {
    if (!time && from_str != "unspecified") {
        throw std::runtime_error("can't fix time from " + from_str + " to " + to_str + ": it's empty");
    }
    auto time_zone = date::locate_zone(timezone_name);
    if (from_str == "unspecified") {
        if (time) {
            std::stringstream s;
            s << "can't fix time from " << from_str << " to " << to_str << ": it's not unspecified but is " << *time << " instead";
            throw std::runtime_error(s.str());
        }
    } else {
        auto existing_zoned = time->as_zoned_time(time_zone);
        auto existing_h_m = date::format("%H:%M", existing_zoned);
        if (existing_h_m != from_str) {
            std::stringstream s;
            s << "can't replace " << from_str << "=>" << to_str <<  " in " << existing_zoned << ": HH:MM do not match";
            throw std::runtime_error(s.str());
        }
    }
    auto new_local_time = local_from_y_m_d_h_m(to_str);
    auto new_zoned_time = date::make_zoned(time_zone, new_local_time);
    auto new_sys_time{new_zoned_time.get_sys_time()};
    time = vp::JulDays_UT{new_sys_time};
}


void apply_vrata_fix(Precalculated_Vrata & vrata, const FixItem & fix) {
    switch (fix.type) {
    case Fix::ParanStartTime:
        replace_time(vrata.paranam_start, fix.from, fix.to, vrata.location.timezone_name);
        break;
    case Fix::ParanEndTime:
        replace_time(vrata.paranam_end, fix.from, fix.to, vrata.location.timezone_name);
        break;
    case Fix::Skip:
        vrata.skip = true;
        break;
    }
}

void fix_vratas(std::vector<Precalculated_Vrata> & vratas, Fixes && fixes) {
    for (auto & vrata : vratas) {
        auto fix_iter = std::find_if(fixes.cbegin(), fixes.cend(), [&vrata](const FixItem & fix) {
            return fix.loc == vrata.location;
        });
        if (fix_iter != fixes.cend()) {
            apply_vrata_fix(vrata, *fix_iter);
        }
    }
}

void test_one_precalculated_table_slug(const char * slug, Fixes fixes={}) {
    CAPTURE(slug);

    std::string filename{std::string{"data/precalculated-"} + slug + ".html"};
    auto s = slurp_file(source_dir_path / filename);
    REQUIRE(!s.empty());

    auto year = get_year_from_slug(slug);
    //sanity check
    REQUIRE(year >= date::year{2000});
    REQUIRE(year < date::year{2030});

    CAPTURE(year);
    std::vector<Precalculated_Vrata> vratas = extract_vratas_from_precalculated_table(std::move(s), year);
    fix_vratas(vratas, std::move(fixes));
    CAPTURE(vratas.size());
    check_precalculated_vratas(vratas);
}

TEST_CASE("precalculated ekAdashIs") {
    test_one_precalculated_table_slug(
        "2017-11-12",
        {
            {vp::murmansk_coord, Fix::ParanStartTime, "10:30", "2017-11-15 10:36"},
            {vp::riga_coord, Fix::ParanEndTime, "unspecified", "2017-11-15 09:40"},
            {vp::yurmala_coord, Fix::ParanEndTime, "unspecified", "2017-11-15 09:40"}
        });
//    test_one_precalculated_table_slug("2017-11-27");
//    test_one_precalculated_table_slug("2017-12-11");
//    test_one_precalculated_table_slug("2017-12-26");
    test_one_precalculated_table_slug(
        "2018-01-10",
        {
            {vp::petropavlovskkamchatskiy_coord, Fix::ParanStartTime, "10:28", "2018-01-13 10:31"},
            {vp::murmansk_coord, Fix::Skip, "", ""}, // TODO: fix calculations for no sunrise cases
        });
//    test_one_precalculated_table_slug("2018-01-23");
//    test_one_precalculated_table_slug("2018-01-30");
//    test_one_precalculated_table_slug("2018-02-08");
//    test_one_precalculated_table_slug("2018-02-24");
//    test_one_precalculated_table_slug("2018-03-10");
//    test_one_precalculated_table_slug("2018-03-17");
//    test_one_precalculated_table_slug("2018-03-23");
//    test_one_precalculated_table_slug("2018-04-09");
//    test_one_precalculated_table_slug("2018-04-24");
//    test_one_precalculated_table_slug("2018-05-09");
//    test_one_precalculated_table_slug("2018-05-14_adhimaasa");
//    test_one_precalculated_table_slug("2018-05-23");
//    test_one_precalculated_table_slug("2018-06-07");
//    test_one_precalculated_table_slug("2018-06-21");
//    test_one_precalculated_table_slug("2018-07-06");
//    test_one_precalculated_table_slug("2018-07-20");
//    test_one_precalculated_table_slug("2018-08-05");
//    test_one_precalculated_table_slug("2018-08-19");
//    test_one_precalculated_table_slug("2018-08-31");
//    test_one_precalculated_table_slug("2018-09-12");
//    test_one_precalculated_table_slug("2018-09-22");
//    test_one_precalculated_table_slug("2018-10-03");
//    test_one_precalculated_table_slug("2018-10-18");
//    test_one_precalculated_table_slug("2018-11-01");
//    test_one_precalculated_table_slug("2018-11-17");
//    test_one_precalculated_table_slug("2018-12-01");
//    test_one_precalculated_table_slug("2018-12-12");
//    test_one_precalculated_table_slug("2018-12-29");
//    test_one_precalculated_table_slug("2019-01-09");
//    test_one_precalculated_table_slug("2019-01-13");
//    test_one_precalculated_table_slug("2019-01-29");
//    test_one_precalculated_table_slug("2019-02-12");
//    test_one_precalculated_table_slug("2019-02-28");
//    test_one_precalculated_table_slug("2019-03-15");
//    test_one_precalculated_table_slug("2019-03-29");
//    test_one_precalculated_table_slug("2019-04-11");
    test_one_precalculated_table_slug("2019-04-27");
//    test_one_precalculated_table_slug("2019-05-13");
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