#include <cassert>
#include <optional>

#include "calc.h"
#include "swe.h"

std::optional<Swe_Time> Calc::find_next_ekadashi_sunrise(Swe_Time after, Coord coord)
{
    Swe s;
    int max_days_left = 16;
    std::optional<Swe_Time> sunrise = after;
    while ((sunrise = s.get_sunrise(*sunrise, coord)).has_value() && max_days_left--) {
        if (s.get_tithi(*sunrise).is_ekadashi()) {
            return sunrise;
        }
        sunrise = Swe_Time{sunrise->as_julian_days()+0.1};
    }
    return {};
}

std::optional<Vrata> Calc::find_next_vrata(Date after, Coord coord)
{
    Swe s;
    auto sunrise = find_next_ekadashi_sunrise(Swe_Time{after}, coord);
    if (sunrise) {
        auto arunodaya = get_arunodaya(*sunrise, coord);
        if (!arunodaya.has_value()) { return{}; }
        auto tithi_arunodaya = s.get_tithi(*arunodaya);
        if ((tithi_arunodaya.is_dashami())) {
            // purva-viddha Ekadashi, get next sunrise
            sunrise = s.get_sunrise(Swe_Time{sunrise->as_julian_days()+0.1}, coord);
        }
        Date d{sunrise->as_date()};
        return Vrata{d};
    }
    return {};
}

std::optional<Swe_Time> Calc::get_arunodaya(Swe_Time sunrise, Coord coord)
{
    Swe_Time back_24hrs{sunrise.as_julian_days()-1};
    auto prev_sunset = Swe{}.get_sunset(back_24hrs, coord);
    if (!prev_sunset.has_value()) { return{}; }
    double night_len_in_days = sunrise.as_julian_days() - prev_sunset->as_julian_days();
    const double muhurtas_per_night = (12*60) / 48.0;
    return Swe_Time{sunrise.as_julian_days() - night_len_in_days * 2 / muhurtas_per_night};
}

bool operator==(const Vrata &v1, const Vrata &v2)
{
    return v1.type == v2.type && v1.date == v2.date;
}

std::ostream &operator<<(std::ostream &o, const Vrata &v)
{
    return o << "Vrata{" << v.date << "}";
}
