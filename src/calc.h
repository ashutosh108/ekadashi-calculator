#ifndef CALC_H
#define CALC_H

#include "location.h"
#include "date-fixed.h"
#include "swe.h"
#include "juldays_ut.h"
#include "tithi.h"
#include "vrata.h"

#include <optional>

namespace vp {

class Calc
{
public:
    Calc(Swe swe);
    // main interface: get info for nearest future Vrata after given date
    std::optional<Vrata> find_next_vrata(date::year_month_day after) const;

    // Helper functions. They are public for easier testing,
    // but should be considered private otherwise.

    // find next sunrise which happens to be within Ekadashi tithi
    std::optional<JulDays_UT> find_ekadashi_sunrise(JulDays_UT after) const;

    // get arunodaya(= sunrise - night_length/8) for given sunrise
    std::optional<JulDays_UT> arunodaya_for_sunrise(JulDays_UT sunrise) const;

    std::optional<JulDays_UT> find_tithi_start(JulDays_UT, Tithi) const;
    JulDays_UT find_tithi_start_v(JulDays_UT, Tithi) const;

    static JulDays_UT proportional_time(JulDays_UT const t1, JulDays_UT const t2, double const proportion);
    JulDays_UT calc_astronomical_midnight(date::year_month_day date) const;

    std::optional<Ativrddhatvam> calc_ativrddhatvam_for_sunrise(JulDays_UT sunrise) const;

    vp::Swe swe;

private:
    std::optional<JulDays_UT> sunset_before_sunrise(JulDays_UT const sunrise) const;
    date::year_month_day get_vrata_date(const JulDays_UT sunrise) const;
    Paran get_paran(const JulDays_UT last_fasting_sunrise) const;
    Paran atirikta_paran(const JulDays_UT first_fasting_sunrise) const;

    std::optional<JulDays_UT> next_sunrise(JulDays_UT sunrise) const;
    JulDays_UT next_sunrise_v(JulDays_UT sunrise) const;
    bool got_atirikta_ekadashi(const JulDays_UT sunrise_on_shuddha_ekadashi_or_next_one) const;
    bool got_atirikta_dvadashi(const JulDays_UT sunrise_on_shuddha_ekadashi_or_next_one) const;
};

} // namespace vp

#endif // CALC_H
