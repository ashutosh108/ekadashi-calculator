#include "text-interface.h"

#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>

#include "calc.h"
#include "vrata_detail.h"

using namespace vp;

namespace vp::text_ui {

date::year_month_day parse_ymd(const char *s) {
    std::tm tm{};
    std::istringstream stream{s};
    stream >> std::get_time(&tm, "%Y-%m-%d");
    return date::year_month_day{date::year{tm.tm_year+1900}, date::month{static_cast<unsigned int>(tm.tm_mon+1)}, date::day{static_cast<unsigned int>(tm.tm_mday)}};
}

const std::vector<LocationDb::NamedCoord> &LocationDb::locations() {
    static std::vector<NamedCoord> locations_ {
        { "udupi", udupi_coord },
        { "gokarna", gokarna_coord },
        { "newdelhi", newdelhi_coord },
        { "manali", manali_coord },
        { "kalkuta", kalkuta_coord },
        { "aktau", aktau_coord },
        { "perm", perm_coord },
        { "ufa", ufa_coord },
        { "ekaterinburg", ekaterinburg_coord },
        { "surgut", surgut_coord },
        { "chelyabinsk", chelyabinsk_coord },
        { "bishkek", bishkek_coord },
        { "almaata", almaata_coord },
        { "tekeli", tekeli_coord },
        { "omsk", omsk_coord },
        { "novosibirsk", novosibirsk_coord },
        { "barnaul", barnaul_coord },
        { "tomsk", tomsk_coord },
        { "kophangan", kophangan_coord },
        { "denpasar", denpasar_coord },
        { "mirnyy", mirnyy_coord },
        { "habarovsk", habarovsk_coord },
        { "vladivostok", vladivostok_coord },
        { "petropavlovskkamchatskiy", petropavlovskkamchatskiy_coord },
        { "erevan", erevan_coord },
        { "tbilisi", tbilisi_coord },
        { "samara", samara_coord },
        { "volgograd", volgograd_coord },
        { "ulyanovsk", ulyanovsk_coord },
        { "pyatigorsk", pyatigorsk_coord },
        { "stavropol", stavropol_coord },
        { "semikarakorsk", semikarakorsk_coord },
        { "krasnodar", krasnodar_coord },
        { "simferopol", simferopol_coord },
        { "donetsk", donetsk_coord },
        { "staryyoskol", staryyoskol_coord },
        { "voronezh", voronezh_coord },
        { "tambov", tambov_coord },
        { "kazan", kazan_coord },
        { "kirov", kirov_coord },
        { "ryazan", ryazan_coord },
        { "moskva", moskva_coord },
        { "spb", spb_coord },
        { "murmansk", murmansk_coord },
        { "kostomuksha", kostomuksha_coord },
        { "smolensk", smolensk_coord },
        { "gomel", gomel_coord },
        { "minsk", minsk_coord },
        { "harkov", harkov_coord },
        { "poltava", poltava_coord },
        { "kremenchug", kremenchug_coord },
        { "krivoyrog", krivoyrog_coord },
        { "kiev", kiev_coord },
        { "nikolaev", nikolaev_coord },
        { "odessa", odessa_coord },
        { "kolomyya", kolomyya_coord },
        { "kishinev", kishinev_coord },
        { "riga", riga_coord },
        { "yurmala", yurmala_coord },
        { "tallin", tallin_coord },
        { "vilnyus", vilnyus_coord },
        { "varshava", varshava_coord },
        { "vena", vena_coord },
        { "marsel", marsel_coord },
        { "madrid", madrid_coord },
        { "london", london_coord },
        { "frederikton", frederikton_coord },
        { "toronto", toronto_coord },
        { "miami", miami_coord },
        { "meadowlake", meadowlake_coord },
    };
    return locations_;
}

std::optional<Location> LocationDb::find_coord(const char *location_name) {
    auto found = std::find_if(
                std::begin(locations()),
                std::end(locations()),
                [=](auto named_coord){
        return strcmp(named_coord.name, location_name) == 0;
    }
    );
    if (found == std::end(locations())) return std::nullopt;
    return found->coord;
}

void calc_one(date::year_month_day base_date, const char *location_name, Location coord, std::ostream &o) {
    auto vrata = Calc{coord}.find_next_vrata(base_date);
    if (!vrata.has_value()) {
        o << location_name << ": calculation error, can't find next Ekadashi. Sorry.\n";
    } else {
        Vrata_Detail vd{*vrata, coord};
        o << location_name << '\n' << vd << "\n\n";
    }
}

void calc_one(date::year_month_day base_date, const char * location_name, std::ostream &o) {
    std::optional<Location> coord = LocationDb::find_coord(location_name);
    if (!coord) {
        o << "Location not found: '" << location_name << "'\n";
        return;
    }
    calc_one(base_date, location_name, *coord, o);
}

void print_detail_one(date::year_month_day base_date, const char *location_name, Location coord, std::ostream &o) {
    o << location_name << ' ' << base_date << '\n';
    o << "<to be implemented>\n";
    Calc calc{coord};
    auto sunrise = calc.swe.get_sunrise(JulDays_UT{base_date});
    if (sunrise) {
        auto arunodaya_info = calc.get_arunodaya(*sunrise);
        if (arunodaya_info) {
            auto [arunodaya, arunodaya_half_ghatika_before] = *arunodaya_info;
                    o << "arunodaya-1/2ghatika: " << arunodaya_half_ghatika_before
                    << ' ' << calc.swe.get_tithi(arunodaya_half_ghatika_before) << '\n';
                    o << "arunodaya: " << arunodaya
                    << ' ' << calc.swe.get_tithi(arunodaya) << '\n';
        }
                    o << "sunrise: " << *sunrise << ' ' << calc.swe.get_tithi(*sunrise) << '\n';
        }
        }

                    void print_detail_one(date::year_month_day base_date, const char * location_name, std::ostream &o) {
                std::optional<Location> coord = LocationDb::find_coord(location_name);
                if (!coord) {
                    o << "Location not found: '" << location_name << "'\n";
                    return;
                }
                print_detail_one(base_date, location_name, *coord, o);
            }

            void calc_all(date::year_month_day d, std::ostream &o) {
                for (auto &l : LocationDb::locations()) {
                    calc_one(d, l.name, l.coord, o);
                }
            }

            namespace detail {

            std::filesystem::path determine_exe_dir(const char* argv0) {
                namespace fs = std::filesystem;
                return fs::absolute(fs::path{argv0}).parent_path();
            }

            std::filesystem::path determine_working_dir(const char* argv0) {
                namespace fs = std::filesystem;
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

}

/* Change dir to the directory with eph and tzdata data files (usually it's .exe dir or the one above) */
void change_to_data_dir(const char* argv0)
{
    auto working_dir = detail::determine_working_dir(argv0);
    std::filesystem::current_path(working_dir);
}

} // namespace vp::text_ui
