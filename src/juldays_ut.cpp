#include "juldays_ut.h"

#include <cmath>
#include <chrono>
#include "swephexp.h"
#include "tz-fixed.h"

namespace vp {

JulDays_UT::JulDays_UT(date::year_month_day d, double_hours hours)
{
    juldays_ut_ = double_days{swe_julday(static_cast<int>(d.year()),
                                 static_cast<int>(static_cast<unsigned>(d.month())),
                                 static_cast<int>(static_cast<unsigned>(d.day())),
                                 hours.count(),
                                         SE_GREG_CAL)};
}

JulDays_UT::JulDays_UT(date::local_time<double_days> t, const date::time_zone * tz)
{
    auto z = date::make_zoned(tz, t, date::choose::earliest);
    date::sys_time<double_days> sys = z.get_sys_time();
    auto sys_days = date::floor<date::days>(sys);
    date::year_month_day ymd = sys_days;
    double_hours hours{sys - sys_days};
    juldays_ut_ = double_days{
            swe_julday(static_cast<int>(ymd.year()),
                       static_cast<int>(static_cast<unsigned>(ymd.month())),
                       static_cast<int>(static_cast<unsigned>(ymd.day())),
                       hours.count(),
                       SE_GREG_CAL)};
}

JulDays_UT::JulDays_UT(date::sys_time<double_days> t)
{
    auto t_days = date::floor<date::days>(t);
    date::year_month_day ymd{t_days};
    double_hours hours{t-t_days};
    juldays_ut_ = double_days{
            swe_julday(static_cast<int>(ymd.year()),
                       static_cast<int>(static_cast<unsigned>(ymd.month())),
                       static_cast<int>(static_cast<unsigned>(ymd.day())),
                       hours.count(),
                   SE_GREG_CAL)};
}

JulDays_UT::JulDays_UT(date::local_days date) : JulDays_UT(date::year_month_day{date})
{
}

bool JulDays_UT::operator==(const JulDays_UT &to) const
{
    const double epsilon = 1e-6;
    return std::fabs(juldays_ut_.count() - to.juldays_ut_.count()) <= epsilon;
}

date::year_month_day JulDays_UT::year_month_day() const
{
    int year{}, month{}, day{};
    double hours{};
    swe_revjul(juldays_ut_.count(), SE_GREG_CAL, &year, &month, &day, &hours);
    return date::year{year}/month/day;
}

double_hours JulDays_UT::hours() const
{
    int year{}, month{}, day{};
    double hours{};
    swe_revjul(juldays_ut_.count(), SE_GREG_CAL, &year, &month, &day, &hours);
    return double_hours{hours};
}

date::sys_time<double_hours> JulDays_UT::as_sys_time() const
{
    int year{}, month{}, day{};
    double hours{};
    swe_revjul(juldays_ut_.count(), SE_GREG_CAL, &year, &month, &day, &hours);
    return date::sys_days{date::year{year}/month/day} + double_hours{hours};
}

JulDays_UT operator +(const JulDays_UT &t, double_days delta)
{
    return JulDays_UT{t.raw_julian_days_ut() + delta};
}

JulDays_UT operator -(const JulDays_UT &t, double_days delta)
{
    return t + (-delta);
}

JulDays_UT JulDays_UT::operator +=(double_days delta)
{
    *this = *this+delta;
    return *this;
}

JulDays_UT JulDays_UT::operator -=(double_days delta)
{
    *this = *this-delta;
    return *this;
}

bool JulDays_UT::operator <(const JulDays_UT &other) const
{
    return raw_julian_days_ut() < other.raw_julian_days_ut();
}

bool JulDays_UT::operator <=(const JulDays_UT &other) const
{
    return raw_julian_days_ut() <= other.raw_julian_days_ut();
}

bool JulDays_UT::operator >(const JulDays_UT &other) const
{
    return raw_julian_days_ut() > other.raw_julian_days_ut();
}

bool JulDays_UT::operator >=(const JulDays_UT &other) const
{
    return raw_julian_days_ut() >= other.raw_julian_days_ut();
}

bool JulDays_UT::operator !=(const JulDays_UT &other) const
{
    return raw_julian_days_ut() != other.raw_julian_days_ut();
}

double_days operator -(const JulDays_UT &t1, const JulDays_UT &t2)
{
    return t1.raw_julian_days_ut() - t2.raw_julian_days_ut();
}

date::sys_seconds JulDays_UT::round_to_minute_up() const
{
    return date::ceil<std::chrono::minutes>(as_sys_time());
}

date::sys_seconds JulDays_UT::round_to_second_up() const
{
    return date::ceil<std::chrono::seconds>(as_sys_time());
}

date::sys_seconds JulDays_UT::round_to_minute_down() const
{
    return date::floor<std::chrono::minutes>(as_sys_time());
}

date::sys_seconds JulDays_UT::round_to_second_down() const
{
    return date::floor<std::chrono::seconds>(as_sys_time());
}

date::sys_seconds JulDays_UT::round_to_minute() const
{
    return date::round<std::chrono::minutes>(as_sys_time());
}

date::sys_seconds JulDays_UT::round_to_second() const
{
    return date::round<std::chrono::seconds>(as_sys_time());
}

date::zoned_time<double_days> JulDays_UT::as_zoned_time(const date::time_zone * time_zone) const
{
    return date::make_zoned(time_zone, as_sys_time());
}

} // namespace vp
