#ifndef JulDays_UT_H_INCLUDED
#define JulDays_UT_H_INCLUDED

#include "date-fixed.h"
#include "tz-fixed.h"

#include <optional>
#include <ostream>

namespace vp {

using double_hours = std::chrono::duration<double, std::ratio<3600>>;
using double_days = std::chrono::duration<double, std::ratio<86400>>;

/* Wrapper class for jd (julian days) as it is understood in the swiss ephemeris library.
 * Very compact class: only one double in size, no virtual functions.
 * Provides value semantics, though maybe not all operations implemented for the lack of necessity.
 * NOTE: Julian days is *not* related to Julian calendar. It's named after monk Julius
 * and it is number of days since noon UT Jan 1, 4713BC.
 * See https://en.wikipedia.org/wiki/Julian_day
*/
class JulDays_UT
{
public:
    explicit JulDays_UT(double_days juldays_ut);
    explicit JulDays_UT(date::year_month_day d, double_hours hours=double_hours{});
    explicit JulDays_UT(date::local_time<double_days> t, const date::time_zone * tz);
    explicit JulDays_UT(date::sys_time<double_days> t);

    double_days raw_julian_days_ut() const { return juldays_ut_; }
    bool operator==(JulDays_UT const &to) const;
    date::year_month_day year_month_day() const;
    double_hours hours() const;
    date::sys_time<double_hours> as_sys_time() const;
    JulDays_UT operator +=(double_days);
    JulDays_UT operator -=(double_days);
    bool operator <(JulDays_UT const &other) const;
    bool operator <=(JulDays_UT const &other) const;
    bool operator >(JulDays_UT const &other) const;
    bool operator >=(JulDays_UT const &other) const;
    bool operator !=(JulDays_UT const &other) const;
    date::sys_seconds round_to_minute_up() const;
    date::sys_seconds round_to_second_up() const;
    date::sys_seconds round_to_minute_down() const;
    date::sys_seconds round_to_second_down() const;
    date::zoned_time<double_days> as_zoned_time(const date::time_zone * time_zone) const;
private:
    double_days juldays_ut_;
};

class JulDays_Zoned {
public:
    JulDays_UT t_;
    const char *timezone_name_;
    JulDays_Zoned(const char *timezone_name, JulDays_UT t) :
        t_(t), timezone_name_(timezone_name) {}
};

std::ostream &operator<<(std::ostream &os, JulDays_UT const &t);
std::ostream &operator<<(std::ostream &os, const std::optional<JulDays_UT> & t);
std::ostream &operator<<(std::ostream &os, JulDays_Zoned const &t);
JulDays_UT operator +(const JulDays_UT &, double_days);
JulDays_UT operator -(const JulDays_UT &, double_days);

// returns number of days between two Swe_Time-s (like as_julian_days())
double_days operator -(const JulDays_UT &, const JulDays_UT &);

} // namespace vp

#endif // JulDays_UT_H_INCLUDED
